#include "integrations/openrgb/OpenRgbColorRouter.h"

#include "integrations/openrgb/OpenRgbRenderCoordinator.h"

#include "ColorUtils.h"
#include "ControllerZone.h"
#include "RGBEffect.h"
#include "OpenRGBPluginInterface.h"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace
{
using lighttrack::ClipId;
using lighttrack::openrgb::CurrentRenderingEffect;

class RoutedController final
{
public:
    using FrameCallback = std::function<void(RGBControllerInterface*, RGBEffect*)>;

    RoutedController(OpenRGBPluginAPIInterface* plugin_api,
        RGBControllerInterface* output, FrameCallback frame_callback) :
        plugin_api_(plugin_api), output_(output),
        frame_callback_(std::move(frame_callback))
    {
    }

    ~RoutedController()
    {
        for(const auto& replica : replicas_)
        {
            if(replica.second != nullptr)
            {
                replica.second->UnregisterUpdateCallback(this);
                plugin_api_->DeleteVirtualRGBController(replica.second);
            }
        }
    }

    void AddReplica(ClipId clip_id)
    {
        replicas_.emplace(clip_id, nullptr);
    }

    void Finalize()
    {
        RGBController_Setup setup{};
        setup.name = output_->GetName();
        setup.vendor = output_->GetVendor();
        setup.description = output_->GetDescription();
        setup.version = output_->GetVersion();
        setup.serial = output_->GetSerial();
        setup.location = output_->GetLocation();
        setup.type = DEVICE_TYPE_VIRTUAL;
        setup.flags = CONTROLLER_FLAG_VIRTUAL;

        std::size_t color_count = 0;
        for(unsigned int index = 0; index < output_->GetZoneCount(); ++index)
        {
            zone copy = output_->GetZone(index);
            // GetZone returns value-owned matrix/segment metadata, but its
            // LED and color pointers still refer to the physical controller.
            copy.leds = nullptr;
            copy.colors = nullptr;
            copy.start_idx = 0;
            copy.modes.clear();
            copy.active_mode = -1;
            color_count += (copy.flags & ZONE_FLAG_MANUALLY_CONFIGURABLE_SIZE_EFFECTS_ONLY)
                && copy.leds_count > 1 ? 1 : copy.leds_count;
            setup.zones.push_back(std::move(copy));
        }
        setup.leds.resize(color_count);
        setup.modes.resize(1);
        mode& direct = setup.modes.front();
        direct.name = "Direct";
        direct.colors.resize(color_count);
        direct.flags = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_BRIGHTNESS;
        direct.brightness_min = 0;
        direct.brightness_max = 100;
        direct.brightness = 100;
        direct.color_mode = MODE_COLORS_PER_LED;
        setup.active_mode = 0;

        // Each clip gets its own unregistered virtual controller. Effects
        // that read GetColor(0) now read only their own previous frame.
        for(auto& replica : replicas_)
        {
            replica.second = plugin_api_->CreateVirtualRGBController(&setup);
            if(replica.second == nullptr)
            {
                throw std::runtime_error("OpenRGB could not create a layer buffer");
            }
            replica.second->SetAllColors(ColorUtils::OFF());
            // UpdateLEDs signals synchronously on the rendering thread.
            // DeviceUpdateLEDs runs later on a host worker; using it here
            // would lose the effect identity and could deadlock on teardown.
            replica.second->RegisterUpdateCallback(
                [](void* context, unsigned int reason, void*)
                {
                    if(reason == RGBCONTROLLER_UPDATE_REASON_UPDATELEDS)
                    {
                        static_cast<RoutedController*>(context)->UpdateLEDs();
                    }
                }, this);
        }
    }

    std::unique_ptr<ControllerZone> CreateZone(
        ClipId clip_id, const ControllerZone& output_zone)
    {
        const auto replica = replicas_.find(clip_id);
        if(replica == replicas_.end() || replica->second == nullptr)
        {
            return {};
        }
        return std::make_unique<ControllerZone>(replica->second,
            output_zone.zone_idx, output_zone.reverse,
            static_cast<int>(output_zone.self_brightness), true,
            output_zone.is_segment, output_zone.segment_idx);
    }

    void ClearLayer(ClipId clip_id)
    {
        const auto replica = replicas_.find(clip_id);
        if(replica != replicas_.end() && replica->second != nullptr)
        {
            replica->second->SetAllColors(ColorUtils::OFF());
        }
    }

private:
    void UpdateLEDs()
    {
        RGBEffect* effect = CurrentRenderingEffect();
        if(effect != nullptr)
        {
            frame_callback_(output_, effect);
            return;
        }
        std::lock_guard<std::mutex> frame_guard(
            lighttrack::openrgb::ControllerFrameMutex());
        frame_callback_(output_, nullptr);
    }

    OpenRGBPluginAPIInterface* plugin_api_;
    RGBControllerInterface* output_;
    FrameCallback frame_callback_;
    std::map<ClipId, RGBControllerInterface*> replicas_;
};

}

namespace lighttrack::openrgb
{
class OpenRgbColorRouter::Impl
{
public:
    explicit Impl(OpenRGBPluginAPIInterface* plugin_api) : plugin_api_(plugin_api) {}

    struct LayerState;

    struct Route
    {
        LayerState* layer = nullptr;
        ControllerZone* output = nullptr;
        std::unique_ptr<ControllerZone> buffer;
    };

    struct LayerState
    {
        ClipId clip_id;
        int lane_level = 0;
        int sequence = 0;
        bool active = false;
        RGBEffect* effect = nullptr;
        std::vector<Route*> routes;
    };

    struct TargetState
    {
        ControllerZone* output = nullptr;
        std::optional<RGBColor> override_color;
        std::vector<Route*> routes;
    };

    void SetTargets(std::vector<ControllerZone*> targets)
    {
        Reset();
        for(ControllerZone* target : targets)
        {
            if(target != nullptr
                && target->controller != nullptr
                && targets_.find(target) == targets_.end())
            {
                targets_.emplace(
                    target,
                    TargetState{target, std::nullopt, {}});
            }
        }
    }

    void ConfigureLayers(
        std::vector<OpenRgbColorRouter::LayerDefinition> definitions)
    {
        controllers_.clear();
        routes_.clear();
        layers_.clear();
        effect_layers_.clear();
        for(auto& target : targets_)
        {
            target.second.routes.clear();
        }

        for(auto& definition : definitions)
        {
            if(!definition.clip_id.IsValid()
                || layers_.find(definition.clip_id) != layers_.end())
            {
                continue;
            }

            bool has_target = false;
            for(ControllerZone* target : definition.targets)
            {
                if(targets_.find(target) != targets_.end())
                {
                    has_target = true;
                    break;
                }
            }
            if(!has_target)
            {
                continue;
            }

            layers_.emplace(
                definition.clip_id,
                LayerState{
                    definition.clip_id,
                    definition.lane_level,
                    definition.sequence,
                    false,
                    nullptr,
                    {}});
        }

        for(auto& target : targets_)
        {
            RGBControllerInterface* output_controller =
                target.second.output->controller;
            if(controllers_.find(output_controller)
                == controllers_.end())
            {
                controllers_.emplace(
                    output_controller,
                    std::make_unique<RoutedController>(
                        plugin_api_,
                        output_controller,
                        [this](
                            RGBControllerInterface* output,
                            RGBEffect* effect)
                        {
                            OnVirtualFrame(output, effect);
                        }));
            }
        }

        for(const auto& definition : definitions)
        {
            const auto layer = layers_.find(definition.clip_id);
            if(layer == layers_.end())
            {
                continue;
            }

            std::set<RGBControllerInterface*> layer_controllers;
            for(ControllerZone* target : definition.targets)
            {
                const auto target_state = targets_.find(target);
                if(target_state != targets_.end())
                {
                    layer_controllers.insert(
                        target_state->second.output->controller);
                }
            }
            for(RGBControllerInterface* output : layer_controllers)
            {
                controllers_[output]->AddReplica(definition.clip_id);
            }
        }

        for(auto& controller : controllers_)
        {
            controller.second->Finalize();
        }

        for(const auto& definition : definitions)
        {
            const auto layer = layers_.find(definition.clip_id);
            if(layer == layers_.end())
            {
                continue;
            }

            std::set<ControllerZone*> routed_targets;
            for(ControllerZone* target : definition.targets)
            {
                auto target_state = targets_.find(target);
                if(target_state == targets_.end()
                    || !routed_targets.insert(target).second)
                {
                    continue;
                }

                RoutedController* controller =
                    controllers_[target->controller].get();
                auto buffer = controller->CreateZone(
                    definition.clip_id,
                    *target);
                if(!buffer)
                {
                    continue;
                }

                auto route = std::make_unique<Route>();
                route->layer = &layer->second;
                route->output = target;
                route->buffer = std::move(buffer);
                layer->second.routes.push_back(route.get());
                target_state->second.routes.push_back(route.get());
                routes_.push_back(std::move(route));
            }
        }

        for(auto& target : targets_)
        {
            std::sort(
                target.second.routes.begin(),
                target.second.routes.end(),
                [](const Route* left, const Route* right)
                {
                    if(left->layer->lane_level
                        != right->layer->lane_level)
                    {
                        return left->layer->lane_level
                            < right->layer->lane_level;
                    }
                    return left->layer->sequence
                        < right->layer->sequence;
                });
        }
    }

    void Reset()
    {
        output_enabled_ = false;
        controllers_.clear();
        routes_.clear();
        layers_.clear();
        effect_layers_.clear();
        targets_.clear();
    }

    bool HasLayer(ClipId clip_id) const
    {
        const auto layer = layers_.find(clip_id);
        return layer != layers_.end() && !layer->second.routes.empty();
    }

    std::vector<ControllerZone*> LayerZones(ClipId clip_id) const
    {
        std::vector<ControllerZone*> zones;
        const auto layer = layers_.find(clip_id);
        if(layer == layers_.end())
        {
            return zones;
        }

        zones.reserve(layer->second.routes.size());
        for(const Route* route : layer->second.routes)
        {
            zones.push_back(route->buffer.get());
        }
        return zones;
    }

    void BindEffect(ClipId clip_id, RGBEffect* effect)
    {
        auto layer = layers_.find(clip_id);
        if(layer == layers_.end())
        {
            return;
        }

        if(layer->second.effect != nullptr)
        {
            effect_layers_.erase(layer->second.effect);
        }
        layer->second.effect = effect;
        if(effect != nullptr)
        {
            effect_layers_[effect] = &layer->second;
        }

    }

    void SetLayerActive(ClipId clip_id, bool active)
    {
        auto layer = layers_.find(clip_id);
        if(layer == layers_.end()
            || layer->second.active == active)
        {
            return;
        }

        if(active)
        {
            for(auto& controller : controllers_)
            {
                controller.second->ClearLayer(clip_id);
            }
        }
        layer->second.active = active;

        std::vector<ControllerZone*> affected;
        affected.reserve(layer->second.routes.size());
        for(const Route* route : layer->second.routes)
        {
            affected.push_back(route->output);
        }
        PublishTargets(affected, false);
    }

    void SetOverrides(
        std::vector<OpenRgbColorRouter::TargetOverride> overrides)
    {
        std::map<ControllerZone*, RGBColor> next;
        for(const auto& value : overrides)
        {
            if(targets_.find(value.target) != targets_.end())
            {
                next[value.target] = value.color;
            }
        }

        std::vector<ControllerZone*> affected;
        for(auto& target : targets_)
        {
            const auto value = next.find(target.first);
            const std::optional<RGBColor> next_color =
                value == next.end()
                ? std::nullopt
                : std::optional<RGBColor>(value->second);
            if(target.second.override_color != next_color)
            {
                target.second.override_color = next_color;
                affected.push_back(target.first);
            }
        }

        PublishTargets(affected, !output_enabled_);
    }

    void StartOutput()
    {
        for(auto& layer : layers_)
        {
            layer.second.active = false;
        }
        output_enabled_ = true;
        PublishAll(false);
    }

    void RefreshIdleOutput()
    {
        if(!output_enabled_)
        {
            return;
        }

        // Targets are leaf zones/segments. Refresh only leaves without a
        // visible effect; an empty device or parent zone lane must never
        // clear an active child. PublishTargets batches the affected leaves
        // into one controller update while preserving active leaf colors.
        std::vector<ControllerZone*> idle_targets;
        idle_targets.reserve(targets_.size());
        for(const auto& target : targets_)
        {
            if(target.second.override_color.has_value()
                || TopRoute(target.second) == nullptr)
            {
                idle_targets.push_back(target.first);
            }
        }
        PublishTargets(idle_targets, false);
    }

    void StopOutput()
    {
        for(auto& layer : layers_)
        {
            layer.second.active = false;
        }
        output_enabled_ = false;
        PublishAll(true);
    }

    void Blackout()
    {
        StopOutput();
    }

private:
    const Route* TopRoute(const TargetState& target) const
    {
        for(auto route = target.routes.rbegin();
            route != target.routes.rend();
            ++route)
        {
            if((*route)->layer->active)
            {
                return *route;
            }
        }
        return nullptr;
    }

    void OnVirtualFrame(
        RGBControllerInterface* output,
        RGBEffect* rendering_effect)
    {
        if(!output_enabled_ || output == nullptr)
        {
            return;
        }

        const auto layer = effect_layers_.find(rendering_effect);
        if(layer != effect_layers_.end())
        {
            if(!layer->second->active
                || !LayerIsVisibleOnController(
                    *layer->second,
                    output))
            {
                return;
            }
        }

        PublishController(output, false);
    }

    bool LayerIsVisibleOnController(
        const LayerState& layer,
        RGBControllerInterface* output) const
    {
        for(const Route* route : layer.routes)
        {
            if(route->output->controller != output)
            {
                continue;
            }
            const auto target = targets_.find(route->output);
            if(target != targets_.end()
                && !target->second.override_color.has_value()
                && TopRoute(target->second) == route)
            {
                return true;
            }
        }
        return false;
    }

    void PublishAll(bool clear_unrouted)
    {
        std::vector<ControllerZone*> targets;
        targets.reserve(targets_.size());
        for(const auto& target : targets_)
        {
            targets.push_back(target.first);
        }
        PublishTargets(targets, clear_unrouted);
    }

    void PublishTargets(
        const std::vector<ControllerZone*>& targets,
        bool clear_unrouted)
    {
        if(targets.empty())
        {
            return;
        }

        std::set<RGBControllerInterface*> outputs;
        for(ControllerZone* target : targets)
        {
            const auto state = targets_.find(target);
            if(state != targets_.end()
                && WriteTarget(state->second, clear_unrouted))
            {
                outputs.insert(target->controller);
            }
        }
        Submit(outputs);
    }

    void PublishController(
        RGBControllerInterface* output,
        bool clear_unrouted)
    {
        bool changed = false;
        for(const auto& target : targets_)
        {
            if(target.second.output->controller == output)
            {
                changed =
                    WriteTarget(target.second, clear_unrouted)
                    || changed;
            }
        }
        if(changed)
        {
            Submit({output});
        }
    }

    bool WriteTarget(
        const TargetState& target,
        bool clear_unrouted)
    {
        if(target.output == nullptr
            || target.output->controller == nullptr)
        {
            return false;
        }

        if(target.override_color.has_value())
        {
            target.output->SetAllZoneLEDs(
                *target.override_color,
                100,
                0,
                0);
            return true;
        }

        if(output_enabled_)
        {
            const Route* route = TopRoute(target);
            if(route != nullptr)
            {
                const unsigned int count = std::min(
                    target.output->leds_count(),
                    route->buffer->leds_count());
                for(unsigned int index = 0; index < count; ++index)
                {
                    target.output->SetLED(
                        index,
                        route->buffer->GetLED(index),
                        100,
                        0,
                        0);
                }
                return true;
            }
        }

        if(output_enabled_ || clear_unrouted)
        {
            target.output->SetAllZoneLEDs(
                ColorUtils::OFF(),
                100,
                0,
                0);
            return true;
        }
        return false;
    }

    void Submit(const std::set<RGBControllerInterface*>& outputs) const
    {
        for(RGBControllerInterface* output : outputs)
        {
            ForceDirectMode(output);
            output->UpdateLEDs();
        }
    }

    static void ForceDirectMode(RGBControllerInterface* controller)
    {
        if(controller == nullptr)
        {
            return;
        }

        for(unsigned int index = 0;
            index < controller->GetModeCount();
            ++index)
        {
            if(controller->GetModeName(index) == "Direct")
            {
                if(controller->GetActiveMode()
                    != static_cast<int>(index))
                {
                    controller->SetActiveMode(index);
                }
                return;
            }
        }

        controller->SetCustomMode();
        controller->UpdateMode();
    }

    std::map<ControllerZone*, TargetState> targets_;
    std::map<ClipId, LayerState> layers_;
    std::map<RGBEffect*, LayerState*> effect_layers_;
    std::vector<std::unique_ptr<Route>> routes_;
    std::map<RGBControllerInterface*, std::unique_ptr<RoutedController>>
        controllers_;
    bool output_enabled_ = false;
    OpenRGBPluginAPIInterface* plugin_api_;
};

OpenRgbColorRouter::OpenRgbColorRouter(OpenRGBPluginAPIInterface* plugin_api) :
    impl_(std::make_unique<Impl>(plugin_api))
{
}

OpenRgbColorRouter::~OpenRgbColorRouter() = default;

void OpenRgbColorRouter::SetTargets(
    std::vector<ControllerZone*> targets)
{
    std::lock_guard<std::mutex> frame_guard(
        ControllerFrameMutex());
    impl_->SetTargets(std::move(targets));
}

void OpenRgbColorRouter::ConfigureLayers(
    std::vector<LayerDefinition> layers)
{
    std::lock_guard<std::mutex> frame_guard(
        ControllerFrameMutex());
    impl_->ConfigureLayers(std::move(layers));
}

void OpenRgbColorRouter::Reset()
{
    std::lock_guard<std::mutex> frame_guard(
        ControllerFrameMutex());
    impl_->Reset();
}

bool OpenRgbColorRouter::HasLayer(ClipId clip_id) const
{
    std::lock_guard<std::mutex> frame_guard(
        ControllerFrameMutex());
    return impl_->HasLayer(clip_id);
}

std::vector<ControllerZone*> OpenRgbColorRouter::LayerZones(
    ClipId clip_id) const
{
    std::lock_guard<std::mutex> frame_guard(
        ControllerFrameMutex());
    return impl_->LayerZones(clip_id);
}

void OpenRgbColorRouter::BindEffect(
    ClipId clip_id,
    RGBEffect* effect)
{
    std::lock_guard<std::mutex> frame_guard(
        ControllerFrameMutex());
    impl_->BindEffect(clip_id, effect);
}

void OpenRgbColorRouter::SetLayerActive(
    ClipId clip_id,
    bool active)
{
    std::lock_guard<std::mutex> frame_guard(
        ControllerFrameMutex());
    impl_->SetLayerActive(clip_id, active);
}

void OpenRgbColorRouter::SetOverrides(
    std::vector<TargetOverride> overrides)
{
    std::lock_guard<std::mutex> frame_guard(
        ControllerFrameMutex());
    impl_->SetOverrides(std::move(overrides));
}

void OpenRgbColorRouter::StartOutput()
{
    std::lock_guard<std::mutex> frame_guard(
        ControllerFrameMutex());
    impl_->StartOutput();
}

void OpenRgbColorRouter::RefreshIdleOutput()
{
    std::lock_guard<std::mutex> frame_guard(
        ControllerFrameMutex());
    impl_->RefreshIdleOutput();
}

void OpenRgbColorRouter::StopOutput()
{
    std::lock_guard<std::mutex> frame_guard(
        ControllerFrameMutex());
    impl_->StopOutput();
}

void OpenRgbColorRouter::Blackout()
{
    std::lock_guard<std::mutex> frame_guard(
        ControllerFrameMutex());
    impl_->Blackout();
}
}
