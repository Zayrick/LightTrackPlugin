#include "integrations/openrgb/OpenRgbColorRouter.h"

#include "integrations/openrgb/OpenRgbRenderCoordinator.h"
#include "integrations/openrgb/PlaybackDiagnostics.h"
#include "integrations/openrgb/LayerBufferController.h"
#include "integrations/openrgb/OutputWriter.h"

#include "ColorUtils.h"
#include "ControllerZone.h"
#include "RGBEffect.h"
#include "OpenRGBPluginInterface.h"

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
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
    // (output controller, rendering effect, clip whose layer drew a frame)
    using FrameCallback =
        std::function<void(RGBControllerInterface*, RGBEffect*, ClipId)>;

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
                const auto context = contexts_.find(replica.first);
                if(context != contexts_.end())
                {
                    replica.second->UnregisterUpdateCallback(
                        context->second.get());
                }
            }
        }
        // owned_ deletes the layer buffers.
    }

    void AddReplica(
        ClipId clip_id,
        const lighttrack::openrgb::LayerOrientation& orientation)
    {
        replicas_.emplace(clip_id, nullptr);
        orientations_[clip_id] = orientation;
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

        // Each clip gets its own in-memory layer buffer (no device thread).
        // Effects that read GetColor(0) read only their own previous frame.
        for(auto& replica : replicas_)
        {
            const lighttrack::openrgb::LayerOrientation orientation =
                orientations_[replica.first];
            RGBController_Setup replica_setup = setup;
            if(!orientation.IsIdentity())
            {
                for(zone& zone_setup : replica_setup.zones)
                {
                    lighttrack::openrgb::TransformMatrixMap(
                        zone_setup.matrix_map,
                        orientation);
                    for(segment& segment_setup : zone_setup.segments)
                    {
                        lighttrack::openrgb::TransformMatrixMap(
                            segment_setup.matrix_map,
                            orientation);
                    }
                }
            }
            auto& buffer = owned_[replica.first];
            buffer = std::make_unique<lighttrack::openrgb::LayerBufferController>(
                std::move(replica_setup));
            replica.second = buffer.get();
            replica.second->SetAllColors(ColorUtils::OFF());
            // UpdateLEDs signals synchronously on the rendering thread.
            // DeviceUpdateLEDs runs later on a host worker; using it here
            // would lose the effect identity and could deadlock on teardown.
            // The context carries the clip so the router knows which layer
            // just drew a frame.
            auto& context = contexts_[replica.first];
            context = std::make_unique<ReplicaContext>(
                ReplicaContext{this, replica.first});
            replica.second->RegisterUpdateCallback(
                [](void* context, unsigned int reason, void*)
                {
                    if(reason == RGBCONTROLLER_UPDATE_REASON_UPDATELEDS)
                    {
                        auto* replica_context =
                            static_cast<ReplicaContext*>(context);
                        replica_context->owner->UpdateLEDs(
                            replica_context->clip_id);
                    }
                }, context.get());
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
        auto zone = std::make_unique<ControllerZone>(replica->second,
            output_zone.zone_idx, output_zone.reverse,
            static_cast<int>(output_zone.self_brightness), true,
            output_zone.is_segment, output_zone.segment_idx);

        // Matrix zones are oriented through their matrix map; strips run
        // backwards when turned around (180 degrees) or mirrored.
        const zone_type type = zone->type();
        const bool is_matrix = type == ZONE_TYPE_MATRIX
            || type == ZONE_TYPE_MATRIX_LOOP_X
            || type == ZONE_TYPE_MATRIX_LOOP_Y;
        const auto orientation = orientations_.find(clip_id);
        if(!is_matrix && orientation != orientations_.end()
            && ((orientation->second.rotation == 180)
                != orientation->second.mirror))
        {
            zone->reverse = !zone->reverse;
        }
        return zone;
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
    struct ReplicaContext
    {
        RoutedController* owner;
        ClipId clip_id;
    };

    void UpdateLEDs(ClipId clip_id)
    {
        RGBEffect* effect = CurrentRenderingEffect();
        if(effect != nullptr)
        {
            frame_callback_(output_, effect, clip_id);
            return;
        }
        std::lock_guard<std::mutex> frame_guard(
            lighttrack::openrgb::ControllerFrameMutex());
        frame_callback_(output_, nullptr, clip_id);
    }

    OpenRGBPluginAPIInterface* plugin_api_;
    RGBControllerInterface* output_;
    FrameCallback frame_callback_;
    std::map<ClipId, RGBControllerInterface*> replicas_;
    std::map<ClipId, std::unique_ptr<lighttrack::openrgb::LayerBufferController>> owned_;
    std::map<ClipId, lighttrack::openrgb::LayerOrientation> orientations_;
    std::map<ClipId, std::unique_ptr<ReplicaContext>> contexts_;
};

}

namespace lighttrack::openrgb
{
void TransformMatrixMap(
    matrix_map_type& matrix,
    const LayerOrientation& orientation)
{
    const unsigned int width = matrix.width;
    const unsigned int height = matrix.height;
    if(orientation.IsIdentity()
        || width == 0
        || height == 0
        || matrix.map.size()
            < static_cast<std::size_t>(width) * height)
    {
        return;
    }

    std::vector<unsigned int> source = matrix.map;
    unsigned int source_width = width;
    unsigned int source_height = height;

    if(orientation.mirror)
    {
        for(unsigned int y = 0; y < source_height; ++y)
        {
            std::reverse(
                source.begin() + static_cast<std::ptrdiff_t>(y) * source_width,
                source.begin() + static_cast<std::ptrdiff_t>(y + 1) * source_width);
        }
    }

    // Rotate clockwise in 90 degree steps.
    const int steps = ((orientation.rotation / 90) % 4 + 4) % 4;
    for(int step = 0; step < steps; ++step)
    {
        const unsigned int rotated_width = source_height;
        const unsigned int rotated_height = source_width;
        std::vector<unsigned int> rotated(source.size());
        for(unsigned int y = 0; y < rotated_height; ++y)
        {
            for(unsigned int x = 0; x < rotated_width; ++x)
            {
                // Rotated (x, y) shows source (y, source_height - 1 - x).
                rotated[static_cast<std::size_t>(y) * rotated_width + x] =
                    source[static_cast<std::size_t>(source_height - 1 - x)
                        * source_width + y];
            }
        }
        source.swap(rotated);
        source_width = rotated_width;
        source_height = rotated_height;
    }

    matrix.width = source_width;
    matrix.height = source_height;
    matrix.map = std::move(source);
}

class OpenRgbColorRouter::Impl
{
public:
    Impl(OpenRGBPluginAPIInterface* plugin_api,
        lighttrack::openrgb::OutputWriter::Mode output_mode) :
        output_mode_(output_mode), plugin_api_(plugin_api)
    {
    }

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
        // Set once the layer's effect has drawn a frame since it became
        // active. Until then the layer is not shown, so starting a clip
        // never flashes its cleared (black) buffer.
        bool has_frame = false;
    };

    struct TargetState
    {
        ControllerZone* output = nullptr;
        std::optional<RGBColor> override_color;
        std::vector<Route*> routes;
        // An idle zone is blacked out once, then left alone so other
        // writers (e.g. Visual Map painting the same LEDs) are not fought.
        mutable bool cleared = false;
        // Output geometry, read once in SetTargets: asking the controller
        // during playback takes its AccessMutex, which a busy device holds.
        unsigned int start = 0;
        unsigned int count = 0;
        bool is_segment = false;
        float brightness = 1.0f;
        lighttrack::openrgb::OutputWriter* writer = nullptr;
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
                TargetState state{target, std::nullopt, {}};
                state.start = target->start_idx();
                state.count = target->leds_count();
                state.is_segment = target->is_segment;
                state.brightness = target->self_brightness / 100.0f;
                auto& writer = writers_[target->controller];
                if(!writer)
                {
                    writer = std::make_unique<
                        lighttrack::openrgb::OutputWriter>(
                            target->controller,
                            output_mode_);
                }
                state.writer = writer.get();
                targets_.emplace(target, state);
            }
        }
    }

    void ConfigureLayers(
        std::vector<OpenRgbColorRouter::LayerDefinition> definitions)
    {
        used_controllers_.clear();
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
                            RGBEffect* effect,
                            ClipId clip_id)
                        {
                            OnVirtualFrame(output, effect, clip_id);
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
                controllers_[output]->AddReplica(
                    definition.clip_id,
                    definition.orientation);
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
            if(!target.second.routes.empty())
            {
                used_controllers_.insert(target.second.output->controller);
            }
        }
    }

    void Reset()
    {
        settle_outputs_.clear();
        output_enabled_ = false;
        used_controllers_.clear();
        controllers_.clear();
        routes_.clear();
        layers_.clear();
        effect_layers_.clear();
        targets_.clear();
        // Sends what is still queued (e.g. the final blackout) first.
        writers_.clear();
    }

    bool WaitForOutput(int timeout_ms)
    {
        bool idle = true;
        for(auto& writer : writers_)
        {
            idle = writer.second->WaitIdle(timeout_ms) && idle;
        }
        return idle;
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
            for(const Route* route : layer->second.routes)
            {
                const auto target = targets_.find(route->output);
                if(target != targets_.end())
                {
                    // Clear once again if this clip ends without drawing.
                    target->second.cleared = false;
                }
            }
        }
        layer->second.active = active;
        layer->second.has_frame = false;

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
                // Apply the new state (or clear the old one) once.
                target.second.cleared = false;
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
        ResetIdleClears();
        output_enabled_ = true;
        PublishAll(false);
    }

    void ResetIdleClears()
    {
        for(auto& target : targets_)
        {
            target.second.cleared = false;
        }
    }

    void RefreshIdleOutput()
    {
        if(!output_enabled_)
        {
            return;
        }

        // Targets are leaf zones/segments. Leaves without a visible effect
        // are blacked out once (WriteTarget skips already-cleared ones), so
        // other writers painting them afterwards are left alone; an empty
        // device or parent zone lane must never clear an active child.
        // Overrides (lit/disabled lanes) are likewise applied once.
        std::vector<ControllerZone*> idle_targets;
        idle_targets.reserve(targets_.size());
        for(const auto& target : targets_)
        {
            if(target.second.override_color.has_value()
                || (TopRoute(target.second) == nullptr
                    && IsUsedController(target.first->controller)))
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
        ResetIdleClears();
        output_enabled_ = false;
        PublishAll(true);
    }

    void Blackout()
    {
        StopOutput();
    }

private:
    // Highest-priority active layer that has drawn a frame.
    const Route* TopRoute(const TargetState& target) const
    {
        for(auto route = target.routes.rbegin();
            route != target.routes.rend();
            ++route)
        {
            if((*route)->layer->active && (*route)->layer->has_frame)
            {
                return *route;
            }
        }
        return nullptr;
    }

    // An active layer on this target has not drawn its first frame yet.
    bool HasPendingRoute(const TargetState& target) const
    {
        for(const Route* route : target.routes)
        {
            if(route->layer->active && !route->layer->has_frame)
            {
                return true;
            }
        }
        return false;
    }

    void OnVirtualFrame(
        RGBControllerInterface* output,
        RGBEffect* rendering_effect,
        ClipId clip_id)
    {
        const auto drawn = layers_.find(clip_id);
        if(drawn != layers_.end() && drawn->second.active)
        {
            drawn->second.has_frame = true;
        }

        if(!output_enabled_ || output == nullptr)
        {
            return;
        }

        // Publish only the zones this clip covers and is visible on. Every
        // other zone of the device is unchanged (its own clip publishes it);
        // rewriting the whole device for every frame of every clip made
        // devices with many zones and many clips (e.g. a 32-channel
        // controller) cost the lock most of its time.
        if(drawn != layers_.end())
        {
            if(!drawn->second.active)
            {
                return;
            }
            bool changed = false;
            for(const Route* route : drawn->second.routes)
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
                    changed = WriteTarget(target->second, false) || changed;
                }
            }
            if(changed)
            {
                Submit({output});
            }
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

    // Devices without any clip are left alone during playback. Other
    // plugins (e.g. Visual Map virtual devices that forward to the same
    // physical LEDs) may be driving them, and writing black there would
    // fight the active effect and flicker.
    bool IsUsedController(RGBControllerInterface* controller) const
    {
        return used_controllers_.find(controller)
            != used_controllers_.end();
    }



    void PublishAll(bool clear_unrouted)
    {
        std::vector<ControllerZone*> targets;
        targets.reserve(targets_.size());
        for(const auto& target : targets_)
        {
            if(target.second.override_color.has_value()
                || IsUsedController(target.first->controller))
            {
                targets.push_back(target.first);
            }
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
            // Lit/disabled lanes are applied once, not on every frame: the
            // same LEDs may also be painted by another plugin (e.g. a Visual
            // Map device), and re-applying constantly makes them flicker.
            if(target.cleared)
            {
                return false;
            }
            FillTarget(target, *target.override_color);
            target.cleared = true;
            settle_outputs_.insert(target.output->controller);
            return true;
        }

        if(output_enabled_)
        {
            const Route* route = TopRoute(target);
            if(route != nullptr)
            {
                target.cleared = false;
                const unsigned int count = std::min(
                    target.count,
                    route->buffer->leds_count());
                for(unsigned int index = 0; index < count; ++index)
                {
                    target.writer->SetLed(
                        target.start + index,
                        Adjust(target, route->buffer->GetLED(index)));
                }
                return true;
            }
        }

        if(output_enabled_ && HasPendingRoute(target))
        {
            // A clip just started here but has not drawn yet: keep showing
            // the previous frame instead of flashing black in between.
            return false;
        }

        if(output_enabled_ || clear_unrouted)
        {
            if(target.cleared)
            {
                return false;
            }
            FillTarget(target, ColorUtils::OFF());
            target.cleared = true;
            settle_outputs_.insert(target.output->controller);
            return true;
        }
        return false;
    }

    static RGBColor Adjust(const TargetState& target, RGBColor color)
    {
        return target.brightness >= 1.0f
            ? color
            : ColorUtils::apply_adjustments(color, target.brightness, 0, 0);
    }

    // Same result as ControllerZone::SetAllZoneLEDs.
    static void FillTarget(const TargetState& target, RGBColor color)
    {
        const RGBColor adjusted = Adjust(target, color);
        if(target.is_segment)
        {
            for(unsigned int index = 0; index < target.count; ++index)
            {
                target.writer->SetLed(target.start + index, adjusted);
            }
            return;
        }
        target.writer->FillZone(
            target.output->zone_idx,
            target.start,
            target.count,
            adjusted);
    }

    // Hands the composed frames to the device writers. One-off frames
    // (clears, overrides) are resent shortly afterwards by the writer:
    // OpenRGB drops an UpdateLEDs request that arrives while the device
    // thread is still sending the previous frame, which otherwise leaves
    // the last frame lit - most often on slow devices like Visual Map.
    void Submit(const std::set<RGBControllerInterface*>& outputs) const
    {
        for(RGBControllerInterface* output : outputs)
        {
            const auto writer = writers_.find(output);
            if(writer != writers_.end())
            {
                writer->second->Commit(
                    settle_outputs_.find(output) != settle_outputs_.end());
            }
        }
        settle_outputs_.clear();
    }

    std::map<ControllerZone*, TargetState> targets_;
    std::set<RGBControllerInterface*> used_controllers_;
    std::map<ClipId, LayerState> layers_;
    std::map<RGBEffect*, LayerState*> effect_layers_;
    std::vector<std::unique_ptr<Route>> routes_;
    std::map<RGBControllerInterface*, std::unique_ptr<RoutedController>>
        controllers_;
    bool output_enabled_ = false;
    // Controllers that just got a one-off clear, resent after Submit().
    mutable std::set<RGBControllerInterface*> settle_outputs_;
    std::map<RGBControllerInterface*,
        std::unique_ptr<lighttrack::openrgb::OutputWriter>> writers_;
    lighttrack::openrgb::OutputWriter::Mode output_mode_;
    OpenRGBPluginAPIInterface* plugin_api_;
};

OpenRgbColorRouter::OpenRgbColorRouter(
    OpenRGBPluginAPIInterface* plugin_api,
    OutputMode output_mode) :
    impl_(std::make_unique<Impl>(
        plugin_api,
        output_mode == OutputMode::Threaded
            ? OutputWriter::Mode::Threaded
            : OutputWriter::Mode::Synchronous))
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

bool OpenRgbColorRouter::WaitForOutput(int timeout_ms)
{
    std::lock_guard<std::mutex> frame_guard(
        ControllerFrameMutex());
    return impl_->WaitForOutput(timeout_ms);
}
}
