#include "integrations/openrgb/OpenRgbColorRouter.h"

#include "integrations/openrgb/OpenRgbRenderCoordinator.h"

#include "ColorUtils.h"
#include "ControllerZone.h"
#include "RGBEffect.h"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace
{
using lighttrack::ClipId;
using lighttrack::openrgb::CurrentRenderingEffect;

class RoutedController final : public RGBController
{
public:
    using FrameCallback =
        std::function<void(RGBController*, RGBEffect*)>;

    RoutedController(
        RGBController* output,
        FrameCallback frame_callback) :
        output_(output),
        frame_callback_(std::move(frame_callback))
    {
        if(output_ != nullptr)
        {
            name = output_->name;
            vendor = output_->vendor;
            description = output_->description;
            version = output_->version;
            serial = output_->serial;
            location = output_->location;
            type = DEVICE_TYPE_VIRTUAL;
        }
    }

    void AddReplica(ClipId clip_id)
    {
        if(output_ == nullptr
            || replicas_.find(clip_id) != replicas_.end())
        {
            return;
        }

        Replica replica;
        replica.zone_offset = zones.size();
        replica.color_offset = ColorCount();

        for(const zone& source : output_->zones)
        {
            zone copy;
            copy.name = source.name;
            copy.type = source.type;
            copy.leds_count = source.leds_count;
            copy.leds_min = source.leds_min;
            copy.leds_max = source.leds_max;
            copy.segments = source.segments;
            copy.flags = source.flags;

            if(source.matrix_map != nullptr)
            {
                auto values = std::make_unique<unsigned int[]>(
                    source.matrix_map->width
                    * source.matrix_map->height);
                std::copy_n(
                    source.matrix_map->map,
                    source.matrix_map->width
                        * source.matrix_map->height,
                    values.get());

                auto matrix = std::make_unique<matrix_map_type>();
                matrix->width = source.matrix_map->width;
                matrix->height = source.matrix_map->height;
                matrix->map = values.get();
                copy.matrix_map = matrix.get();
                matrix_values_.push_back(std::move(values));
                matrices_.push_back(std::move(matrix));
            }

            replica.color_count += ZoneColorCount(source);
            zones.push_back(std::move(copy));
        }

        replicas_.emplace(clip_id, replica);
    }

    void Finalize()
    {
        leds.resize(ColorCount());
        SetupColors();
        std::fill(colors.begin(), colors.end(), ColorUtils::OFF());

        modes.resize(1);
        modes[0].name = "Direct";
        modes[0].colors = colors;
        modes[0].flags =
            MODE_FLAG_HAS_PER_LED_COLOR
            | MODE_FLAG_HAS_BRIGHTNESS;
        modes[0].brightness_min = 0;
        modes[0].brightness_max = 100;
        modes[0].brightness = 100;
        modes[0].color_mode = MODE_COLORS_PER_LED;
        active_mode = 0;
    }

    std::unique_ptr<ControllerZone> CreateZone(
        ClipId clip_id,
        const ControllerZone& output_zone)
    {
        const auto replica = replicas_.find(clip_id);
        if(replica == replicas_.end())
        {
            return {};
        }

        const std::size_t zone_index =
            replica->second.zone_offset
            + output_zone.zone_idx;
        if(zone_index >= zones.size())
        {
            return {};
        }

        return std::make_unique<ControllerZone>(
            this,
            static_cast<unsigned int>(zone_index),
            output_zone.reverse,
            static_cast<int>(output_zone.self_brightness),
            output_zone.is_segment,
            output_zone.segment_idx);
    }

    void BindEffect(ClipId clip_id, RGBEffect* effect)
    {
        const auto replica = replicas_.find(clip_id);
        if(replica == replicas_.end())
        {
            return;
        }

        for(auto binding = effect_replicas_.begin();
            binding != effect_replicas_.end();)
        {
            if(binding->second.clip_id == clip_id)
            {
                binding = effect_replicas_.erase(binding);
            }
            else
            {
                ++binding;
            }
        }

        if(effect != nullptr)
        {
            effect_replicas_[effect] = {
                clip_id,
                replica->second.color_offset,
                replica->second.color_count
            };
        }
    }

    void ClearLayer(ClipId clip_id)
    {
        const auto replica = replicas_.find(clip_id);
        if(replica == replicas_.end())
        {
            return;
        }

        const std::size_t begin = replica->second.color_offset;
        const std::size_t end = std::min(
            colors.size(),
            begin + replica->second.color_count);
        std::fill(
            colors.begin() + begin,
            colors.begin() + end,
            ColorUtils::OFF());
    }

    RGBColor GetLED(unsigned int led) override
    {
        RGBEffect* effect = CurrentRenderingEffect();
        const auto replica = effect_replicas_.find(effect);
        if(replica != effect_replicas_.end()
            && led < replica->second.color_count)
        {
            return colors[replica->second.color_offset + led];
        }
        return RGBController::GetLED(led);
    }

    void UpdateLEDs() override
    {
        if(!frame_callback_)
        {
            return;
        }

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

    void SetupZones() override
    {
    }

    void ResizeZone(int, int) override
    {
    }

    void DeviceUpdateLEDs() override
    {
    }

    void UpdateZoneLEDs(int) override
    {
    }

    void UpdateSingleLED(int) override
    {
    }

    void DeviceUpdateMode() override
    {
    }

private:
    struct Replica
    {
        std::size_t zone_offset = 0;
        std::size_t color_offset = 0;
        std::size_t color_count = 0;
    };

    struct EffectReplica
    {
        ClipId clip_id;
        std::size_t color_offset = 0;
        std::size_t color_count = 0;
    };

    std::size_t ColorCount() const
    {
        std::size_t count = 0;
        for(const zone& value : zones)
        {
            count += ZoneColorCount(value);
        }
        return count;
    }

    static std::size_t ZoneColorCount(const zone& value)
    {
        if((value.flags & ZONE_FLAG_RESIZE_EFFECTS_ONLY) != 0
            && value.leds_count > 1)
        {
            return 1;
        }
        return value.leds_count;
    }

    RGBController* output_ = nullptr;
    FrameCallback frame_callback_;
    std::map<ClipId, Replica> replicas_;
    std::map<RGBEffect*, EffectReplica> effect_replicas_;
    std::vector<std::unique_ptr<unsigned int[]>> matrix_values_;
    std::vector<std::unique_ptr<matrix_map_type>> matrices_;
};
}

namespace lighttrack::openrgb
{
class OpenRgbColorRouter::Impl
{
public:
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
            RGBController* output_controller =
                target.second.output->controller;
            if(controllers_.find(output_controller)
                == controllers_.end())
            {
                controllers_.emplace(
                    output_controller,
                    std::make_unique<RoutedController>(
                        output_controller,
                        [this](
                            RGBController* output,
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

            std::set<RGBController*> layer_controllers;
            for(ControllerZone* target : definition.targets)
            {
                const auto target_state = targets_.find(target);
                if(target_state != targets_.end())
                {
                    layer_controllers.insert(
                        target_state->second.output->controller);
                }
            }
            for(RGBController* output : layer_controllers)
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

        for(auto& controller : controllers_)
        {
            controller.second->BindEffect(clip_id, effect);
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
        RGBController* output,
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
        RGBController* output) const
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

        std::set<RGBController*> outputs;
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
        RGBController* output,
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

    void Submit(const std::set<RGBController*>& outputs) const
    {
        for(RGBController* output : outputs)
        {
            ForceDirectMode(output);
            output->UpdateLEDs();
        }
    }

    static void ForceDirectMode(RGBController* controller)
    {
        if(controller == nullptr)
        {
            return;
        }

        for(unsigned int index = 0;
            index < controller->modes.size();
            ++index)
        {
            if(controller->modes[index].name == "Direct")
            {
                if(controller->GetMode()
                    != static_cast<int>(index))
                {
                    controller->SetMode(index);
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
    std::map<RGBController*, std::unique_ptr<RoutedController>>
        controllers_;
    bool output_enabled_ = false;
};

OpenRgbColorRouter::OpenRgbColorRouter() :
    impl_(std::make_unique<Impl>())
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
