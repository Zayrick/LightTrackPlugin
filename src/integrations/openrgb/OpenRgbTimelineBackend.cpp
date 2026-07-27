#include "integrations/openrgb/OpenRgbTimelineBackend.h"

#include "integrations/openrgb/OpenRgbEffectCatalog.h"
#include "integrations/openrgb/OpenRgbEffectFactory.h"

#include "ColorUtils.h"
#include "ControllerZone.h"
#include "EffectManager.h"
#include "OpenRGBEffectPage.h"
#include "RGBController/RGBController.h"
#include "ResourceManagerInterface.h"

#include <QFrame>
#include <QPointer>
#include <QScrollArea>
#include <QSizePolicy>

#include <nlohmann/json.hpp>

#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using json = nlohmann::json;
using lighttrack::ClipId;
using lighttrack::EffectDescriptor;
using lighttrack::TimelineBackend;
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

class OpenRgbEffect final : public TimelineBackend::Effect
{
public:
    explicit OpenRgbEffect(std::unique_ptr<RGBEffect> value) :
        value_(std::move(value))
    {
    }

    RGBEffect* Get() const noexcept
    {
        return value_.get();
    }

private:
    std::unique_ptr<RGBEffect> value_;
};

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

OpenRgbEffect* AsOpenRgbEffect(TimelineBackend::Effect* effect)
{
    return dynamic_cast<OpenRgbEffect*>(effect);
}

const OpenRgbEffect* AsOpenRgbEffect(const TimelineBackend::Effect* effect)
{
    return dynamic_cast<const OpenRgbEffect*>(effect);
}

struct LaneEntry
{
    QString name;
    int level = 0;
    RGBController* controller = nullptr;
    int zone_index = -1;
    int segment_index = -1;
};

struct RuntimeTarget
{
    int lane = -1;
    ControllerZone* zone = nullptr;
};
}

namespace lighttrack::openrgb
{
class OpenRgbTimelineBackend::Impl
{
public:
    explicit Impl(ResourceManagerInterface* resource_manager) :
        resource_manager_(resource_manager)
    {
    }

    ~Impl()
    {
        ClearClips();
    }

    TimelineBackend::DeviceSnapshot ReloadDevices()
    {
        StopRuntime();
        runtime_targets_.clear();
        runtime_zones_.clear();
        lanes_.clear();

        TimelineBackend::DeviceSnapshot snapshot;
        if(resource_manager_ == nullptr)
        {
            snapshot.empty_message =
                QStringLiteral("OpenRGB resource manager unavailable");
            return snapshot;
        }

        std::vector<RGBController*>& controllers =
            resource_manager_->GetRGBControllers();
        if(controllers.empty())
        {
            snapshot.empty_message = QStringLiteral("No devices");
            return snapshot;
        }

        for(int controller_index = 0;
            controller_index < static_cast<int>(controllers.size());
            ++controller_index)
        {
            RGBController* controller = controllers[controller_index];
            if(controller == nullptr)
            {
                continue;
            }

            lanes_.push_back({
                QString::fromStdString(controller->GetName()),
                0,
                controller,
                -1,
                -1
            });

            for(int zone_index = 0;
                zone_index < static_cast<int>(controller->zones.size());
                ++zone_index)
            {
                const zone& zone_ref = controller->zones[zone_index];
                const int zone_lane = lanes_.size();
                lanes_.push_back({
                    QString::fromStdString(zone_ref.name),
                    1,
                    controller,
                    zone_index,
                    -1
                });

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
                    const int segment_lane = lanes_.size();
                    lanes_.push_back({
                        QString::fromStdString(segment_ref.name),
                        2,
                        controller,
                        zone_index,
                        segment_index
                    });
                    AddRuntimeTarget(
                        segment_lane,
                        controller,
                        zone_index,
                        segment_index);
                }
            }
        }

        snapshot.lanes.reserve(lanes_.size());
        for(int index = 0; index < lanes_.size(); ++index)
        {
            const LaneEntry& lane = lanes_[index];
            snapshot.lanes.push_back({
                QString::number(index),
                lane.name,
                lane.level
            });
        }
        return snapshot;
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

        if(lane.controller != nullptr)
        {
            serialized["controller"] = {
                {"name", lane.controller->GetName()},
                {"location", lane.controller->GetLocation()},
                {"serial", lane.controller->GetSerial()},
                {"description", lane.controller->GetDescription()},
                {"version", lane.controller->GetVersion()},
                {"vendor", lane.controller->GetVendor()}
            };
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
        catch(...)
        {
            return -1;
        }
    }

    TimelineBackend::EffectPtr CreateEffect(
        const QString& effect_id,
        QString* error) const
    {
        EffectDescriptor descriptor;
        if(!lighttrack::openrgb::FindEffect(effect_id, &descriptor))
        {
            SetError(
                error,
                QStringLiteral("Effect \"%1\" is unavailable.")
                    .arg(effect_id));
            return {};
        }

        std::unique_ptr<RGBEffect> effect =
            effect_factory_.Create(descriptor);
        if(!effect)
        {
            SetError(
                error,
                QStringLiteral("Could not create effect \"%1\".")
                    .arg(descriptor.name));
            return {};
        }

        return std::make_unique<OpenRgbEffect>(std::move(effect));
    }

    QByteArray ExportEffectSettings(
        const TimelineBackend::Effect& effect) const
    {
        const OpenRgbEffect* openrgb_effect =
            AsOpenRgbEffect(&effect);
        if(openrgb_effect == nullptr || openrgb_effect->Get() == nullptr)
        {
            throw std::runtime_error("Invalid effect instance");
        }

        return QByteArray::fromStdString(
            openrgb_effect->Get()->ToJson().dump());
    }

    bool ImportEffectSettings(
        TimelineBackend::Effect& effect,
        const QByteArray& settings,
        QString* error) const
    {
        OpenRgbEffect* openrgb_effect = AsOpenRgbEffect(&effect);
        if(openrgb_effect == nullptr || openrgb_effect->Get() == nullptr)
        {
            SetError(error, QStringLiteral("Invalid effect instance"));
            return false;
        }

        try
        {
            const json parsed = json::parse(
                settings.constData(),
                settings.constData() + settings.size());
            effect_factory_.ApplySettings(*openrgb_effect->Get(), parsed);
            return true;
        }
        catch(const std::exception& exception)
        {
            SetError(error, ExceptionMessage(exception));
        }
        catch(...)
        {
            SetError(error, QStringLiteral("Invalid effect settings"));
        }
        return false;
    }

    bool AttachEffect(
        ClipId clip_id,
        TimelineBackend::EffectPtr effect,
        QString* error)
    {
        if(!clip_id.IsValid())
        {
            SetError(error, QStringLiteral("Invalid timeline clip id"));
            return false;
        }

        OpenRgbEffect* openrgb_effect =
            AsOpenRgbEffect(effect.get());
        if(openrgb_effect == nullptr || openrgb_effect->Get() == nullptr)
        {
            SetError(error, QStringLiteral("Invalid effect instance"));
            return false;
        }

        if(clip_effects_.find(clip_id) != clip_effects_.end())
        {
            SetError(
                error,
                QStringLiteral("Timeline clip already has an effect"));
            return false;
        }

        clip_effects_.emplace(clip_id, std::move(effect));
        return true;
    }

    bool ReplaceEffects(
        std::vector<TimelineBackend::ClipEffect> effects,
        QString* error)
    {
        std::map<ClipId, TimelineBackend::EffectPtr> replacement;
        for(TimelineBackend::ClipEffect& binding : effects)
        {
            OpenRgbEffect* openrgb_effect =
                AsOpenRgbEffect(binding.effect.get());
            if(!binding.clip_id.IsValid()
                || openrgb_effect == nullptr
                || openrgb_effect->Get() == nullptr)
            {
                SetError(
                    error,
                    QStringLiteral(
                        "Invalid timeline effect replacement"));
                return false;
            }

            const auto inserted = replacement.emplace(
                binding.clip_id,
                std::move(binding.effect));
            if(!inserted.second)
            {
                SetError(
                    error,
                    QStringLiteral(
                        "Duplicate timeline clip id"));
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
        clip_effects_.swap(replacement);
        return true;
    }

    bool EnsureEffect(
        ClipId clip_id,
        const QString& effect_id,
        QString* error)
    {
        if(clip_effects_.find(clip_id) != clip_effects_.end())
        {
            return true;
        }

        TimelineBackend::EffectPtr effect =
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

        OpenRgbEffect* openrgb_effect =
            AsOpenRgbEffect(found->second.get());
        if(openrgb_effect == nullptr || openrgb_effect->Get() == nullptr)
        {
            return nullptr;
        }

        OpenRGBEffectPage* page =
            new BackendOwnedEffectPage(
                parent,
                openrgb_effect->Get());
        page->SetPreviewButtonVisible(false);
        page->setMinimumWidth(0);
        page->setSizePolicy(
            QSizePolicy::Ignored,
            QSizePolicy::Expanding);
        openrgb_effect->Get()->show();
        return page;
    }

    void RemoveClip(ClipId clip_id)
    {
        const auto found = clip_effects_.find(clip_id);
        if(found != clip_effects_.end())
        {
            DeactivateEffect(found->second.get());
            runtime_assignments_.erase(clip_id);
            clip_effects_.erase(found);
        }
        else
        {
            runtime_assignments_.erase(clip_id);
        }
    }

    void ClearClips()
    {
        StopRuntime();
        for(auto& entry : clip_effects_)
        {
            DeactivateEffect(entry.second.get());
        }
        runtime_assignments_.clear();
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

        runtime_running_ = true;
        SendBlackToTargets(AllRuntimeZones(), true);
        SyncRuntime(position_ms, clips);
    }

    void StopRuntime()
    {
        const bool was_running =
            runtime_running_ || !runtime_assignments_.empty();

        for(auto& entry : clip_effects_)
        {
            DeactivateEffect(entry.second.get());
        }

        runtime_assignments_.clear();
        runtime_running_ = false;

        if(was_running)
        {
            SendBlackToTargets(AllRuntimeZones(), true);
        }
    }

    void SyncRuntime(
        qint64 position_ms,
        const QVector<TimelineClip>& clips)
    {
        if(!runtime_running_)
        {
            return;
        }

        std::map<ClipId, TimelineClip> assigned_clips;
        std::map<ClipId, std::vector<ControllerZone*>> assignments;

        for(const RuntimeTarget& target : runtime_targets_)
        {
            if(target.zone == nullptr)
            {
                continue;
            }

            const TimelineClip* best_clip = nullptr;
            int best_level = -1;

            for(const TimelineClip& clip : clips)
            {
                if(clip.start_ms > position_ms
                    || position_ms >= clip.end_ms
                    || !LaneCoversTarget(
                        clip.lane_index,
                        target.lane))
                {
                    continue;
                }

                const int level = lanes_[clip.lane_index].level;
                if(level >= best_level)
                {
                    best_clip = &clip;
                    best_level = level;
                }
            }

            if(best_clip != nullptr)
            {
                assignments[best_clip->id].push_back(target.zone);
                assigned_clips[best_clip->id] = *best_clip;
            }
        }

        for(auto assignment = runtime_assignments_.begin();
            assignment != runtime_assignments_.end();)
        {
            if(assignments.find(assignment->first) == assignments.end())
            {
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

        std::set<ControllerZone*> active_targets;
        EffectManager* manager = EffectManager::Get();

        for(auto& assignment : assignments)
        {
            const TimelineClip& clip = assigned_clips[assignment.first];
            QString ignored_error;
            if(!EnsureEffect(
                assignment.first,
                clip.effect_id,
                &ignored_error))
            {
                continue;
            }

            OpenRgbEffect* holder = AsOpenRgbEffect(
                clip_effects_[assignment.first].get());
            RGBEffect* effect =
                holder == nullptr ? nullptr : holder->Get();
            if(effect == nullptr)
            {
                continue;
            }

            const auto previous =
                runtime_assignments_.find(assignment.first);
            if(previous == runtime_assignments_.end()
                || previous->second != assignment.second)
            {
                manager->Assign(assignment.second, effect);
                runtime_assignments_[assignment.first] =
                    assignment.second;
            }

            if(!manager->IsActive(effect))
            {
                manager->SetEffectActive(effect);
            }

            active_targets.insert(
                assignment.second.begin(),
                assignment.second.end());
        }

        std::vector<ControllerZone*> inactive_targets;
        for(const RuntimeTarget& target : runtime_targets_)
        {
            if(target.zone != nullptr
                && active_targets.find(target.zone)
                    == active_targets.end())
            {
                inactive_targets.push_back(target.zone);
            }
        }

        SendBlackToTargets(inactive_targets, false);
    }

private:
    static void SetError(QString* destination, const QString& value)
    {
        if(destination != nullptr)
        {
            *destination = value;
        }
    }

    bool ControllerMatches(
        RGBController* controller,
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

    void AddRuntimeTarget(
        int lane,
        RGBController* controller,
        int zone_index,
        int segment_index)
    {
        runtime_zones_.push_back(
            std::make_unique<ControllerZone>(
                controller,
                static_cast<unsigned int>(zone_index),
                false,
                100,
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

    void ForceDirectMode(RGBController* controller) const
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

    void SendBlackToTargets(
        const std::vector<ControllerZone*>& zones,
        bool force_mode) const
    {
        std::set<RGBController*> controllers;
        for(ControllerZone* controller_zone : zones)
        {
            if(controller_zone == nullptr
                || controller_zone->controller == nullptr)
            {
                continue;
            }

            controller_zone->SetAllZoneLEDs(
                ColorUtils::OFF(),
                100,
                0,
                0);
            controllers.insert(controller_zone->controller);
        }

        for(RGBController* controller : controllers)
        {
            if(force_mode)
            {
                ForceDirectMode(controller);
            }
            controller->UpdateLEDs();
        }
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

    void DeactivateEffect(TimelineBackend::Effect* effect) const
    {
        OpenRgbEffect* holder = AsOpenRgbEffect(effect);
        RGBEffect* runtime_effect =
            holder == nullptr ? nullptr : holder->Get();
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

    ResourceManagerInterface* resource_manager_ = nullptr;
    EffectFactory effect_factory_;
    QVector<LaneEntry> lanes_;
    std::vector<std::unique_ptr<ControllerZone>> runtime_zones_;
    QVector<RuntimeTarget> runtime_targets_;
    std::map<ClipId, TimelineBackend::EffectPtr> clip_effects_;
    std::map<ClipId, std::vector<ControllerZone*>>
        runtime_assignments_;
    bool runtime_running_ = false;
};

OpenRgbTimelineBackend::OpenRgbTimelineBackend(
    ResourceManagerInterface* resource_manager) :
    impl_(std::make_unique<Impl>(resource_manager))
{
}

OpenRgbTimelineBackend::~OpenRgbTimelineBackend() = default;

QVector<EffectGroup> OpenRgbTimelineBackend::Effects() const
{
    return LoadEffectCatalog();
}

bool OpenRgbTimelineBackend::FindEffect(
    const QString& effect_id,
    EffectDescriptor* descriptor) const
{
    return lighttrack::openrgb::FindEffect(
        effect_id,
        descriptor);
}

TimelineBackend::DeviceSnapshot
OpenRgbTimelineBackend::ReloadDevices()
{
    return impl_->ReloadDevices();
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

TimelineBackend::EffectPtr OpenRgbTimelineBackend::CreateEffect(
    const QString& effect_id,
    QString* error) const
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
    QString* error) const
{
    return impl_->ImportEffectSettings(effect, settings, error);
}

bool OpenRgbTimelineBackend::AttachEffect(
    ClipId clip_id,
    EffectPtr effect,
    QString* error)
{
    return impl_->AttachEffect(
        clip_id,
        std::move(effect),
        error);
}

bool OpenRgbTimelineBackend::ReplaceEffects(
    std::vector<ClipEffect> effects,
    QString* error)
{
    return impl_->ReplaceEffects(std::move(effects), error);
}

bool OpenRgbTimelineBackend::EnsureEffect(
    ClipId clip_id,
    const QString& effect_id,
    QString* error)
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

std::unique_ptr<TimelineBackend> CreateOpenRgbTimelineBackend(
    ResourceManagerInterface* resource_manager)
{
    return std::make_unique<OpenRgbTimelineBackend>(
        resource_manager);
}
}
