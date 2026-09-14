#include "integrations/openrgb/OpenRgbTimelineBackend.h"

#include "integrations/openrgb/OpenRgbEffectCatalog.h"
#include "integrations/openrgb/OpenRgbEffectFactory.h"
#include "integrations/openrgb/OpenRgbColorRouter.h"

#include "ColorUtils.h"
#include "ControllerZone.h"
#include "EffectManager.h"
#include "OpenRGBEffectPage.h"
#include "RGBControllerInterface.h"
#include "OpenRGBPluginInterface.h"

#include <QFrame>
#include <QPointer>
#include <QScrollArea>
#include <QSizePolicy>

#include <nlohmann/json.hpp>

#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using json = nlohmann::json;
using lighttrack::ClipId;
using lighttrack::EffectDescriptor;
using lighttrack::openrgb::OpenRgbTimelineBackend;
using lighttrack::TimelineClip;
using lighttrack::TimelineLane;

std::string ToUtf8String(const QString& value)
{
    const QByteArray utf8 = value.toUtf8();
    return std::string(
        utf8.constData(),
        static_cast<std::size_t>(utf8.size()));
}

QString FromUtf8String(const std::string& value)
{
    return QString::fromUtf8(
        value.data(),
        static_cast<int>(value.size()));
}

QString ExceptionMessage(const std::exception& exception)
{
    return QString::fromUtf8(exception.what());
}

// OpenRGBEffectPage reparents the RGBEffect into its widget tree. The
// timeline backend remains the sole owner, so detach it before QWidget's
// child cleanup runs.
class BackendOwnedEffectPage final : public OpenRGBEffectPage
{
public:
    BackendOwnedEffectPage(QWidget* parent, RGBEffect* effect) :
        OpenRGBEffectPage(parent, effect),
        effect_(effect)
    {
    }

    ~BackendOwnedEffectPage() override
    {
        if(effect_ != nullptr)
        {
            // The upstream page installs a context-less lambda that captures
            // its UI. Disconnect before the derived destructor detaches the
            // backend-owned sender and before either base destructor runs.
            QObject::disconnect(
                effect_.data(),
                nullptr,
                nullptr,
                nullptr);
            effect_->hide();
            effect_->setParent(nullptr);
        }
    }

private:
    QPointer<RGBEffect> effect_;
};

struct LaneEntry
{
    QString name;
    QString native_name;
    int level = 0;
    RGBControllerInterface* controller = nullptr;
    int zone_index = -1;
    int segment_index = -1;
    QString state_key;
    bool highlighted = false;
    bool disabled = false;
    int led_count = 0;
};

struct LaneState
{
    QString name;
    bool highlighted = false;
    bool disabled = false;
};

struct RuntimeTarget
{
    int lane = -1;
    ControllerZone* zone = nullptr;
};
}

namespace lighttrack::openrgb
{
OpenRgbTimelineBackend::Effect::Effect(
    std::unique_ptr<RGBEffect> value) :
    value_(std::move(value))
{
}

OpenRgbTimelineBackend::Effect::~Effect() = default;

RGBEffect* OpenRgbTimelineBackend::Effect::Get() const noexcept
{
    return value_.get();
}

class OpenRgbTimelineBackend::Impl
{
public:
    explicit Impl(OpenRGBPluginAPIInterface* plugin_api) :
        plugin_api_(plugin_api),
        color_router_(plugin_api)
    {
    }

    ~Impl()
    {
        ClearClips();
        color_router_.Blackout();
        color_router_.Reset();
    }

    void PrepareForDeviceReload()
    {
        // OpenRGB calls this synchronously from its detection-start callback,
        // while every cached controller is still alive. Stop and join all
        // effect workers before the host starts deleting controllers.
        DiscardRuntimeForDeviceReload();
    }

    OpenRgbTimelineBackend::DeviceSnapshot ReloadDevices()
    {
        // OpenRGB deletes hardware controllers before publishing the refreshed
        // device list.  At this point every cached ControllerZone may therefore
        // contain a dangling controller pointer.  Detach effects and discard
        // the cached targets without trying to write a final frame to them.
        DiscardRuntimeForDeviceReload();
        runtime_targets_.clear();
        runtime_zones_.clear();
        lanes_.clear();

        OpenRgbTimelineBackend::DeviceSnapshot snapshot;
        if(plugin_api_ == nullptr)
        {
            snapshot.empty_message =
                QStringLiteral("OpenRGB plugin API unavailable");
            return snapshot;
        }

        const std::vector<RGBControllerInterface*> controllers =
            plugin_api_->GetRGBControllers();
        if(controllers.empty())
        {
            snapshot.empty_message = QStringLiteral("No devices");
            return snapshot;
        }

        for(int controller_index = 0;
            controller_index < static_cast<int>(controllers.size());
            ++controller_index)
        {
            RGBControllerInterface* controller = controllers[controller_index];
            if(controller == nullptr)
            {
                continue;
            }

            AddLane(
                QString::fromStdString(controller->GetName()),
                0,
                controller,
                -1,
                -1,
                0);

            for(int zone_index = 0;
                zone_index < static_cast<int>(controller->GetZoneCount());
                ++zone_index)
            {
                const zone zone_ref = controller->GetZone(zone_index);
                const int zone_lane = AddLane(
                    QString::fromStdString(zone_ref.name),
                    1,
                    controller,
                    zone_index,
                    -1,
                    static_cast<int>(zone_ref.leds_count));

                if(zone_ref.segments.empty())
                {
                    AddRuntimeTarget(
                        zone_lane,
                        controller,
                        zone_index,
                        -1);
                    continue;
                }

                for(int segment_index = 0;
                    segment_index
                        < static_cast<int>(zone_ref.segments.size());
                    ++segment_index)
                {
                    const segment& segment_ref =
                        zone_ref.segments[segment_index];
                    const int segment_lane = AddLane(
                        QString::fromStdString(segment_ref.name),
                        2,
                        controller,
                        zone_index,
                        segment_index,
                        static_cast<int>(
                            segment_ref.leds_count));
                    AddRuntimeTarget(
                        segment_lane,
                        controller,
                        zone_index,
                        segment_index);
                }
            }
        }

        color_router_.SetTargets(AllRuntimeZones());
        RefreshOverrideOutput();
        return CurrentDeviceSnapshot();
    }

    OpenRgbTimelineBackend::DeviceSnapshot CurrentDeviceSnapshot() const
    {
        OpenRgbTimelineBackend::DeviceSnapshot snapshot;
        if(lanes_.isEmpty())
        {
            snapshot.empty_message = plugin_api_ == nullptr
                ? QStringLiteral("OpenRGB plugin API unavailable")
                : QStringLiteral("No devices");
        }
        snapshot.lanes.reserve(lanes_.size());
        for(int index = 0; index < lanes_.size(); ++index)
        {
            const LaneEntry& lane = lanes_[index];
            snapshot.lanes.push_back({
                QString::number(index),
                lane.name,
                lane.level,
                lane.highlighted,
                lane.disabled,
                lane.led_count
            });
        }
        return snapshot;
    }

    QVector<LayoutLaneSnapshot> CaptureLaneStates() const
    {
        QVector<LayoutLaneSnapshot> states;
        states.reserve(lanes_.size());
        for(int index = 0; index < lanes_.size(); ++index)
        {
            const LaneEntry& lane = lanes_[index];
            states.push_back({
                SerializeLane(index),
                lane.name,
                lane.highlighted,
                lane.disabled
            });
        }
        return states;
    }

    OpenRgbTimelineBackend::DeviceSnapshot ResetLaneStates()
    {
        lane_states_.clear();
        for(LaneEntry& lane : lanes_)
        {
            lane.name = lane.native_name;
            lane.highlighted = false;
            lane.disabled = false;
        }
        RefreshOverrideOutput();
        return CurrentDeviceSnapshot();
    }

    OpenRgbTimelineBackend::DeviceSnapshot RestoreLaneStates(
        const QVector<LayoutLaneSnapshot>& states,
        QStringList& warnings)
    {
        // Resolve all targets before changing names used by legacy matching.
        QVector<int> resolved_lanes;
        resolved_lanes.reserve(states.size());
        for(const LayoutLaneSnapshot& state : states)
        {
            const int index = ResolveLane(state.serialized_lane);
            resolved_lanes.push_back(index);
            if(index < 0)
            {
                warnings.push_back(QString(
                    "Skipped settings for \"%1\": its device or zone is unavailable.")
                    .arg(state.name));
            }
        }

        for(int index = 0; index < states.size(); ++index)
        {
            if(resolved_lanes[index] < 0)
            {
                continue;
            }
            const LayoutLaneSnapshot& state = states[index];
            LaneEntry& lane = lanes_[resolved_lanes[index]];
            lane.name = state.name;
            lane.highlighted = state.highlighted;
            lane.disabled = state.disabled;
            lane_states_[lane.state_key] = {
                state.name,
                state.highlighted,
                state.disabled
            };
        }
        RefreshOverrideOutput();
        return CurrentDeviceSnapshot();
    }

    bool RenameLane(int lane_index, const QString& name)
    {
        const QString trimmed_name = name.trimmed();
        if(lane_index < 0
            || lane_index >= lanes_.size()
            || trimmed_name.isEmpty())
        {
            return false;
        }

        LaneEntry& lane = lanes_[lane_index];
        lane.name = trimmed_name;
        lane_states_[lane.state_key].name = trimmed_name;
        return true;
    }

    bool SetLaneHighlighted(int lane_index, bool highlighted)
    {
        if(lane_index < 0 || lane_index >= lanes_.size())
        {
            return false;
        }

        LaneEntry& lane = lanes_[lane_index];
        lane.highlighted = highlighted;
        lane_states_[lane.state_key].highlighted = highlighted;
        RefreshOverrideOutput();
        return true;
    }

    bool SetLaneDisabled(int lane_index, bool disabled)
    {
        if(lane_index < 0 || lane_index >= lanes_.size())
        {
            return false;
        }

        LaneEntry& lane = lanes_[lane_index];
        lane.disabled = disabled;
        lane_states_[lane.state_key].disabled = disabled;
        RefreshOverrideOutput();
        return true;
    }

    QByteArray SerializeLane(int lane_index) const
    {
        if(lane_index < 0 || lane_index >= lanes_.size())
        {
            throw std::runtime_error(
                "Timeline clip references an invalid lane");
        }

        const LaneEntry& lane = lanes_[lane_index];
        json serialized;
        serialized["fallbackIndex"] = lane_index;
        serialized["name"] = ToUtf8String(lane.name);
        serialized["level"] = lane.level;
        serialized["zoneIndex"] = lane.zone_index;
        serialized["segmentIndex"] = lane.segment_index;

        if(!lane.state_key.isEmpty())
        {
            // Queued layout restoration runs after the host may have removed
            // this controller. Use the identity captured during discovery.
            json identity = json::parse(ToUtf8String(lane.state_key));
            identity.erase("zoneIndex");
            identity.erase("segmentIndex");
            serialized["controller"] = std::move(identity);
        }

        return QByteArray::fromStdString(serialized.dump());
    }

    int ResolveLane(const QByteArray& serialized_lane) const
    {
        try
        {
            const json lane = json::parse(
                serialized_lane.constData(),
                serialized_lane.constData() + serialized_lane.size());
            return ResolveLane(lane);
        }
        catch(const std::exception&)
        {
            return -1;
        }
    }

    OpenRgbTimelineBackend::EffectPtr CreateEffect(
        const QString& effect_id,
        QString& error) const
    {
        EffectDescriptor descriptor;
        if(!lighttrack::openrgb::FindEffect(effect_id, descriptor))
        {
            error = QStringLiteral("Effect \"%1\" is unavailable.")
                .arg(effect_id);
            return {};
        }

        std::unique_ptr<RGBEffect> effect =
            effect_factory_.Create(descriptor);
        if(!effect)
        {
            error = QStringLiteral("Could not create effect \"%1\".")
                .arg(descriptor.name);
            return {};
        }

        return std::make_unique<OpenRgbTimelineBackend::Effect>(
            std::move(effect));
    }

    QByteArray ExportEffectSettings(
        const OpenRgbTimelineBackend::Effect& effect) const
    {
        if(effect.Get() == nullptr)
        {
            throw std::runtime_error("Invalid effect instance");
        }

        return QByteArray::fromStdString(
            effect.Get()->ToJson().dump());
    }

    bool ImportEffectSettings(
        OpenRgbTimelineBackend::Effect& effect,
        const QByteArray& settings,
        QString& error) const
    {
        if(effect.Get() == nullptr)
        {
            error = QStringLiteral("Invalid effect instance");
            return false;
        }

        try
        {
            const json parsed = json::parse(
                settings.constData(),
                settings.constData() + settings.size());
            effect_factory_.ApplySettings(*effect.Get(), parsed);
            return true;
        }
        catch(const std::exception& exception)
        {
            error = ExceptionMessage(exception);
        }
        return false;
    }

    bool AttachEffect(
        ClipId clip_id,
        OpenRgbTimelineBackend::EffectPtr effect,
        QString& error)
    {
        if(!clip_id.IsValid())
        {
            error = QStringLiteral("Invalid timeline clip id");
            return false;
        }

        if(effect == nullptr || effect->Get() == nullptr)
        {
            error = QStringLiteral("Invalid effect instance");
            return false;
        }

        if(clip_effects_.find(clip_id) != clip_effects_.end())
        {
            error = QStringLiteral("Timeline clip already has an effect");
            return false;
        }

        RGBEffect* runtime_effect = effect->Get();
        clip_effects_.emplace(clip_id, std::move(effect));
        color_router_.BindEffect(clip_id, runtime_effect);
        return true;
    }

    bool ReplaceEffects(
        std::vector<OpenRgbTimelineBackend::ClipEffect> effects,
        QString& error)
    {
        std::map<ClipId, OpenRgbTimelineBackend::EffectPtr> replacement;
        for(OpenRgbTimelineBackend::ClipEffect& binding : effects)
        {
            if(!binding.clip_id.IsValid()
                || binding.effect == nullptr
                || binding.effect->Get() == nullptr)
            {
                error = QStringLiteral(
                    "Invalid timeline effect replacement");
                return false;
            }

            const auto inserted = replacement.emplace(
                binding.clip_id,
                std::move(binding.effect));
            if(!inserted.second)
            {
                error = QStringLiteral("Duplicate timeline clip id");
                return false;
            }
        }

        // No live state changes until the complete replacement has passed
        // validation and owns every effect.
        StopRuntime();
        for(auto& entry : clip_effects_)
        {
            DeactivateEffect(entry.second.get());
        }
        runtime_assignments_.clear();
        configured_clips_.clear();
        color_router_.ConfigureLayers({});
        clip_effects_.swap(replacement);
        return true;
    }

    bool EnsureEffect(
        ClipId clip_id,
        const QString& effect_id,
        QString& error)
    {
        if(clip_effects_.find(clip_id) != clip_effects_.end())
        {
            return true;
        }

        OpenRgbTimelineBackend::EffectPtr effect =
            CreateEffect(effect_id, error);
        return effect != nullptr
            && AttachEffect(clip_id, std::move(effect), error);
    }

    QByteArray ExportClipSettings(ClipId clip_id) const
    {
        const auto found = clip_effects_.find(clip_id);
        if(found == clip_effects_.end())
        {
            throw std::runtime_error(
                "Timeline clip has no effect instance");
        }
        return ExportEffectSettings(*found->second);
    }

    QWidget* CreateSettingsPage(ClipId clip_id, QWidget* parent)
    {
        const auto found = clip_effects_.find(clip_id);
        if(found == clip_effects_.end())
        {
            return nullptr;
        }

        RGBEffect* effect = found->second->Get();
        if(effect == nullptr)
        {
            return nullptr;
        }

        OpenRGBEffectPage* page =
            new BackendOwnedEffectPage(
                parent,
                effect);
        page->SetPreviewButtonVisible(false);
        page->setMinimumWidth(0);
        page->setSizePolicy(
            QSizePolicy::Ignored,
            QSizePolicy::Expanding);
        effect->show();
        return page;
    }

    void RemoveClip(ClipId clip_id)
    {
        const auto found = clip_effects_.find(clip_id);
        if(found != clip_effects_.end())
        {
            color_router_.SetLayerActive(clip_id, false);
            DeactivateEffect(found->second.get());
            color_router_.BindEffect(clip_id, nullptr);
            clip_effects_.erase(found);
        }
        runtime_assignments_.erase(clip_id);
    }

    void ClearClips()
    {
        StopRuntime();
        for(auto& entry : clip_effects_)
        {
            DeactivateEffect(entry.second.get());
        }
        runtime_assignments_.clear();
        configured_clips_.clear();
        color_router_.ConfigureLayers({});
        clip_effects_.clear();
    }

    void StartRuntime(
        qint64 position_ms,
        const QVector<TimelineClip>& clips)
    {
        if(runtime_running_)
        {
            SyncRuntime(position_ms, clips);
            return;
        }

        ConfigureRoutes(clips);
        runtime_running_ = true;
        color_router_.StartOutput();
        SyncRuntime(position_ms, clips);
    }

    void StopRuntime()
    {
        const bool was_running =
            runtime_running_ || !runtime_assignments_.empty();
        runtime_running_ = false;

        if(was_running)
        {
            color_router_.StopOutput();
        }

        for(auto& entry : clip_effects_)
        {
            DeactivateEffect(entry.second.get());
        }

        runtime_assignments_.clear();
    }

    void SyncRuntime(
        qint64 position_ms,
        const QVector<TimelineClip>& clips)
    {
        if(!runtime_running_)
        {
            return;
        }

        if(!RoutesMatch(clips))
        {
            color_router_.StopOutput();
            for(auto& entry : clip_effects_)
            {
                DeactivateEffect(entry.second.get());
            }
            runtime_assignments_.clear();
            ConfigureRoutes(clips);
            color_router_.StartOutput();
        }

        std::map<ClipId, const TimelineClip*> active_clips;
        for(const TimelineClip& clip : clips)
        {
            if(clip.start_ms > position_ms
                || position_ms >= clip.end_ms
                || !color_router_.HasLayer(clip.id))
            {
                continue;
            }
            active_clips[clip.id] = &clip;
        }

        for(auto assignment = runtime_assignments_.begin();
            assignment != runtime_assignments_.end();)
        {
            if(active_clips.find(assignment->first)
                == active_clips.end())
            {
                color_router_.SetLayerActive(
                    assignment->first,
                    false);
                const auto effect =
                    clip_effects_.find(assignment->first);
                if(effect != clip_effects_.end())
                {
                    DeactivateEffect(effect->second.get());
                }
                assignment = runtime_assignments_.erase(assignment);
            }
            else
            {
                ++assignment;
            }
        }

        EffectManager* manager = EffectManager::Get();
        for(const auto& active : active_clips)
        {
            if(runtime_assignments_.find(active.first)
                != runtime_assignments_.end())
            {
                continue;
            }

            const TimelineClip& clip = *active.second;
            QString ignored_error;
            if(!EnsureEffect(
                active.first,
                clip.effect_id,
                ignored_error))
            {
                continue;
            }

            RGBEffect* effect =
                clip_effects_[active.first]->Get();
            if(effect == nullptr)
            {
                continue;
            }

            std::vector<ControllerZone*> zones =
                color_router_.LayerZones(active.first);
            if(zones.empty())
            {
                continue;
            }

            color_router_.BindEffect(active.first, effect);
            color_router_.SetLayerActive(active.first, true);
            manager->Assign(zones, effect);
            runtime_assignments_[active.first] = std::move(zones);
            manager->SetEffectActive(effect);
        }
    }

private:
    void DiscardRuntimeForDeviceReload()
    {
        for(auto& entry : clip_effects_)
        {
            DeactivateEffect(entry.second.get());
        }

        runtime_assignments_.clear();
        configured_clips_.clear();
        runtime_running_ = false;
        color_router_.Reset();
    }

    bool RoutesMatch(const QVector<TimelineClip>& clips) const
    {
        if(clips.size() != configured_clips_.size())
        {
            return false;
        }

        for(int index = 0; index < clips.size(); ++index)
        {
            if(clips[index].id != configured_clips_[index].id
                || clips[index].effect_id
                    != configured_clips_[index].effect_id
                || clips[index].lane_index
                    != configured_clips_[index].lane_index)
            {
                return false;
            }
        }
        return true;
    }

    void ConfigureRoutes(const QVector<TimelineClip>& clips)
    {
        std::vector<OpenRgbColorRouter::LayerDefinition> definitions;
        definitions.reserve(clips.size());

        for(int sequence = 0; sequence < clips.size(); ++sequence)
        {
            const TimelineClip& clip = clips[sequence];
            if(!clip.id.IsValid()
                || clip.lane_index < 0
                || clip.lane_index >= lanes_.size())
            {
                continue;
            }

            OpenRgbColorRouter::LayerDefinition definition;
            definition.clip_id = clip.id;
            definition.lane_level = lanes_[clip.lane_index].level;
            definition.sequence = sequence;
            for(const RuntimeTarget& target : runtime_targets_)
            {
                if(target.zone != nullptr
                    && LaneCoversTarget(
                        clip.lane_index,
                        target.lane))
                {
                    definition.targets.push_back(target.zone);
                }
            }
            if(!definition.targets.empty())
            {
                definitions.push_back(std::move(definition));
            }
        }

        color_router_.ConfigureLayers(std::move(definitions));
        configured_clips_ = clips;
        for(const auto& effect : clip_effects_)
        {
            color_router_.BindEffect(
                effect.first,
                effect.second->Get());
        }
    }

    bool ControllerMatches(
        RGBControllerInterface* controller,
        const json& saved_controller) const
    {
        if(controller == nullptr || !saved_controller.is_object())
        {
            return false;
        }

        const auto matches =
            [&saved_controller](
                const char* key,
                const std::string& actual)
            {
                return !saved_controller.contains(key)
                    || (saved_controller[key].is_string()
                        && saved_controller[key].get<std::string>()
                            == actual);
            };

        if(!matches("name", controller->GetName())
            || !matches("serial", controller->GetSerial())
            || !matches(
                "description",
                controller->GetDescription())
            || !matches("version", controller->GetVersion())
            || !matches("vendor", controller->GetVendor()))
        {
            return false;
        }

        if(saved_controller.contains("location"))
        {
            if(!saved_controller["location"].is_string())
            {
                return false;
            }

            const std::string saved_location =
                saved_controller["location"].get<std::string>();
            const std::string current_location =
                controller->GetLocation();
            const bool hid_location =
                saved_location.rfind("HID: ", 0) == 0
                || current_location.rfind("HID: ", 0) == 0;

            if(!hid_location
                && saved_location != current_location)
            {
                return false;
            }
        }

        return true;
    }

    int ResolveLane(const json& lane) const
    {
        if(!lane.is_object())
        {
            return -1;
        }

        const int zone_index = lane.value("zoneIndex", -1);
        const int segment_index =
            lane.value("segmentIndex", -1);

        if(lane.contains("controller")
            && lane["controller"].is_object())
        {
            for(int index = 0; index < lanes_.size(); ++index)
            {
                const LaneEntry& candidate = lanes_[index];
                if(ControllerMatches(
                        candidate.controller,
                        lane["controller"])
                    && candidate.zone_index == zone_index
                    && candidate.segment_index == segment_index)
                {
                    return index;
                }
            }

            if(lane.contains("name") && lane["name"].is_string())
            {
                const QString saved_name = FromUtf8String(
                    lane["name"].get<std::string>());
                const int saved_level = lane.value("level", -1);

                for(int index = 0; index < lanes_.size(); ++index)
                {
                    const LaneEntry& candidate = lanes_[index];
                    if(ControllerMatches(
                            candidate.controller,
                            lane["controller"])
                        && candidate.name == saved_name
                        && candidate.level == saved_level)
                    {
                        return index;
                    }
                }
            }

            return -1;
        }

        const int fallback_index =
            lane.value("fallbackIndex", -1);
        return fallback_index >= 0
                && fallback_index < lanes_.size()
            ? fallback_index
            : -1;
    }

    QString LaneStateKey(
        RGBControllerInterface* controller,
        int zone_index,
        int segment_index) const
    {
        if(controller == nullptr)
        {
            return {};
        }

        const json identity = {
            {"name", controller->GetName()},
            {"location", controller->GetLocation()},
            {"serial", controller->GetSerial()},
            {"description", controller->GetDescription()},
            {"version", controller->GetVersion()},
            {"vendor", controller->GetVendor()},
            {"zoneIndex", zone_index},
            {"segmentIndex", segment_index}
        };
        return FromUtf8String(identity.dump());
    }

    int AddLane(
        const QString& native_name,
        int level,
        RGBControllerInterface* controller,
        int zone_index,
        int segment_index,
        int led_count)
    {
        LaneEntry lane;
        lane.name = native_name;
        lane.native_name = native_name;
        lane.level = level;
        lane.controller = controller;
        lane.zone_index = zone_index;
        lane.segment_index = segment_index;
        lane.led_count = led_count;
        lane.state_key = LaneStateKey(
            controller,
            zone_index,
            segment_index);

        const auto saved = lane_states_.find(lane.state_key);
        if(saved != lane_states_.end())
        {
            if(!saved->second.name.isEmpty())
            {
                lane.name = saved->second.name;
            }
            lane.highlighted = saved->second.highlighted;
            lane.disabled = saved->second.disabled;
        }

        lanes_.push_back(std::move(lane));
        return lanes_.size() - 1;
    }

    void AddRuntimeTarget(
        int lane,
        RGBControllerInterface* controller,
        int zone_index,
        int segment_index)
    {
        bool has_direct = false;
        for(unsigned int index = 0; index < controller->GetModeCount(); ++index)
        {
            if(controller->GetModeName(index) == "Direct")
            {
                has_direct = true;
                break;
            }
        }
        runtime_zones_.push_back(
            std::make_unique<ControllerZone>(
                controller,
                static_cast<unsigned int>(zone_index),
                false,
                100,
                has_direct,
                segment_index >= 0,
                segment_index));
        runtime_targets_.push_back(
            {lane, runtime_zones_.back().get()});
    }

    std::vector<ControllerZone*> AllRuntimeZones() const
    {
        std::vector<ControllerZone*> zones;
        zones.reserve(runtime_targets_.size());
        for(const RuntimeTarget& target : runtime_targets_)
        {
            if(target.zone != nullptr)
            {
                zones.push_back(target.zone);
            }
        }
        return zones;
    }

    bool IsTargetHighlighted(int target_lane) const
    {
        if(target_lane < 0 || target_lane >= lanes_.size())
        {
            return false;
        }
        for(int lane = 0; lane <= target_lane; ++lane)
        {
            if(lanes_[lane].highlighted
                && LaneCoversTarget(lane, target_lane))
            {
                return true;
            }
        }
        return false;
    }

    bool IsTargetDisabled(int target_lane) const
    {
        if(target_lane < 0 || target_lane >= lanes_.size())
        {
            return false;
        }
        for(int lane = 0; lane <= target_lane; ++lane)
        {
            if(lanes_[lane].disabled
                && LaneCoversTarget(lane, target_lane))
            {
                return true;
            }
        }
        return false;
    }

    void RefreshOverrideOutput()
    {
        std::vector<OpenRgbColorRouter::TargetOverride> overrides;
        for(const RuntimeTarget& target : runtime_targets_)
        {
            if(target.zone == nullptr)
            {
                continue;
            }

            if(IsTargetDisabled(target.lane))
            {
                overrides.push_back({
                    target.zone,
                    ColorUtils::OFF()
                });
            }
            else if(IsTargetHighlighted(target.lane))
            {
                overrides.push_back({
                    target.zone,
                    ToRGBColor(255, 255, 255)
                });
            }
        }
        color_router_.SetOverrides(std::move(overrides));
    }

    bool LaneCoversTarget(int lane, int target_lane) const
    {
        if(lane < 0
            || target_lane < 0
            || lane >= lanes_.size()
            || target_lane >= lanes_.size()
            || lane > target_lane)
        {
            return false;
        }

        if(lane == target_lane)
        {
            return true;
        }

        const int ancestor_level = lanes_[lane].level;
        if(lanes_[target_lane].level <= ancestor_level)
        {
            return false;
        }

        for(int index = lane + 1; index <= target_lane; ++index)
        {
            if(lanes_[index].level <= ancestor_level)
            {
                return false;
            }
        }
        return true;
    }

    void DeactivateEffect(OpenRgbTimelineBackend::Effect* effect) const
    {
        RGBEffect* runtime_effect =
            effect == nullptr ? nullptr : effect->Get();
        if(runtime_effect == nullptr)
        {
            return;
        }

        EffectManager* manager = EffectManager::Get();
        if(manager->IsActive(runtime_effect))
        {
            manager->SetEffectUnActive(runtime_effect);
        }
        manager->RemoveMapping(runtime_effect);
    }

    OpenRGBPluginAPIInterface* plugin_api_ = nullptr;
    EffectFactory effect_factory_;
    OpenRgbColorRouter color_router_;
    QVector<LaneEntry> lanes_;
    std::map<QString, LaneState> lane_states_;
    std::vector<std::unique_ptr<ControllerZone>> runtime_zones_;
    QVector<RuntimeTarget> runtime_targets_;
    std::map<ClipId, OpenRgbTimelineBackend::EffectPtr> clip_effects_;
    std::map<ClipId, std::vector<ControllerZone*>>
        runtime_assignments_;
    QVector<TimelineClip> configured_clips_;
    bool runtime_running_ = false;
};

OpenRgbTimelineBackend::OpenRgbTimelineBackend(
    OpenRGBPluginAPIInterface* plugin_api) :
    impl_(std::make_unique<Impl>(plugin_api))
{
}

OpenRgbTimelineBackend::~OpenRgbTimelineBackend() = default;

QVector<EffectGroup> OpenRgbTimelineBackend::Effects() const
{
    return LoadEffectCatalog();
}

bool OpenRgbTimelineBackend::FindEffect(
    const QString& effect_id,
    EffectDescriptor& descriptor) const
{
    return lighttrack::openrgb::FindEffect(
        effect_id,
        descriptor);
}

OpenRgbTimelineBackend::DeviceSnapshot
OpenRgbTimelineBackend::ReloadDevices()
{
    return impl_->ReloadDevices();
}

QVector<LayoutLaneSnapshot> OpenRgbTimelineBackend::CaptureLaneStates() const
{
    return impl_->CaptureLaneStates();
}

OpenRgbTimelineBackend::DeviceSnapshot OpenRgbTimelineBackend::ResetLaneStates()
{
    return impl_->ResetLaneStates();
}

OpenRgbTimelineBackend::DeviceSnapshot OpenRgbTimelineBackend::RestoreLaneStates(
    const QVector<LayoutLaneSnapshot>& states,
    QStringList& warnings)
{
    return impl_->RestoreLaneStates(states, warnings);
}

QByteArray OpenRgbTimelineBackend::SerializeLane(
    int lane_index) const
{
    return impl_->SerializeLane(lane_index);
}

int OpenRgbTimelineBackend::ResolveLane(
    const QByteArray& serialized_lane) const
{
    return impl_->ResolveLane(serialized_lane);
}

bool OpenRgbTimelineBackend::RenameLane(
    int lane_index,
    const QString& name)
{
    return impl_->RenameLane(lane_index, name);
}

bool OpenRgbTimelineBackend::SetLaneHighlighted(
    int lane_index,
    bool highlighted)
{
    return impl_->SetLaneHighlighted(lane_index, highlighted);
}

bool OpenRgbTimelineBackend::SetLaneDisabled(
    int lane_index,
    bool disabled)
{
    return impl_->SetLaneDisabled(lane_index, disabled);
}

void OpenRgbTimelineBackend::PrepareForDeviceReload()
{
    impl_->PrepareForDeviceReload();
}

OpenRgbTimelineBackend::EffectPtr OpenRgbTimelineBackend::CreateEffect(
    const QString& effect_id,
    QString& error) const
{
    return impl_->CreateEffect(effect_id, error);
}

QByteArray OpenRgbTimelineBackend::ExportEffectSettings(
    const Effect& effect) const
{
    return impl_->ExportEffectSettings(effect);
}

bool OpenRgbTimelineBackend::ImportEffectSettings(
    Effect& effect,
    const QByteArray& settings,
    QString& error) const
{
    return impl_->ImportEffectSettings(effect, settings, error);
}

bool OpenRgbTimelineBackend::AttachEffect(
    ClipId clip_id,
    EffectPtr effect,
    QString& error)
{
    return impl_->AttachEffect(
        clip_id,
        std::move(effect),
        error);
}

bool OpenRgbTimelineBackend::ReplaceEffects(
    std::vector<ClipEffect> effects,
    QString& error)
{
    return impl_->ReplaceEffects(std::move(effects), error);
}

bool OpenRgbTimelineBackend::EnsureEffect(
    ClipId clip_id,
    const QString& effect_id,
    QString& error)
{
    return impl_->EnsureEffect(clip_id, effect_id, error);
}

QByteArray OpenRgbTimelineBackend::ExportClipSettings(
    ClipId clip_id) const
{
    return impl_->ExportClipSettings(clip_id);
}

QWidget* OpenRgbTimelineBackend::CreateSettingsPage(
    ClipId clip_id,
    QWidget* parent)
{
    return impl_->CreateSettingsPage(clip_id, parent);
}

void OpenRgbTimelineBackend::RemoveClip(ClipId clip_id)
{
    impl_->RemoveClip(clip_id);
}

void OpenRgbTimelineBackend::ClearClips()
{
    impl_->ClearClips();
}

void OpenRgbTimelineBackend::StartRuntime(
    qint64 position_ms,
    const QVector<TimelineClip>& clips)
{
    impl_->StartRuntime(position_ms, clips);
}

void OpenRgbTimelineBackend::SyncRuntime(
    qint64 position_ms,
    const QVector<TimelineClip>& clips)
{
    impl_->SyncRuntime(position_ms, clips);
}

void OpenRgbTimelineBackend::StopRuntime()
{
    impl_->StopRuntime();
}

}
