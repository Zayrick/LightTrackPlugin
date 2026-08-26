#pragma once

#include "core/TimelineTypes.h"
#include "core/effects/EffectDescriptor.h"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <memory>
#include <vector>

class ResourceManagerInterface;
class RGBEffect;
class QWidget;

namespace lighttrack::openrgb
{
class OpenRgbTimelineBackend
{
public:
    class Effect
    {
    public:
        explicit Effect(std::unique_ptr<RGBEffect> value);
        ~Effect();

        Effect(const Effect&) = delete;
        Effect& operator=(const Effect&) = delete;

        RGBEffect* Get() const noexcept;

    private:
        std::unique_ptr<RGBEffect> value_;
    };

    using EffectPtr = std::unique_ptr<Effect>;

    struct ClipEffect
    {
        ClipId clip_id;
        EffectPtr effect;
    };

    struct DeviceSnapshot
    {
        QVector<TimelineLane> lanes;
        QString empty_message;
    };

    explicit OpenRgbTimelineBackend(ResourceManagerInterface* resource_manager);
    ~OpenRgbTimelineBackend();

    OpenRgbTimelineBackend(const OpenRgbTimelineBackend&) = delete;
    OpenRgbTimelineBackend& operator=(const OpenRgbTimelineBackend&) = delete;

    QVector<EffectGroup> Effects() const;
    bool FindEffect(
        const QString& effect_id,
        EffectDescriptor& descriptor) const;

    DeviceSnapshot ReloadDevices();
    QByteArray SerializeLane(int lane_index) const;
    int ResolveLane(const QByteArray& serialized_lane) const;
    bool RenameLane(
        int lane_index,
        const QString& name);
    bool SetLaneHighlighted(
        int lane_index,
        bool highlighted);
    bool SetLaneDisabled(
        int lane_index,
        bool disabled);
    void PrepareForDeviceReload();

    EffectPtr CreateEffect(
        const QString& effect_id,
        QString& error) const;
    QByteArray ExportEffectSettings(const Effect& effect) const;
    bool ImportEffectSettings(
        Effect& effect,
        const QByteArray& settings,
        QString& error) const;

    bool AttachEffect(
        ClipId clip_id,
        EffectPtr effect,
        QString& error);
    bool ReplaceEffects(
        std::vector<ClipEffect> effects,
        QString& error);
    bool EnsureEffect(
        ClipId clip_id,
        const QString& effect_id,
        QString& error);
    QByteArray ExportClipSettings(ClipId clip_id) const;
    QWidget* CreateSettingsPage(
        ClipId clip_id,
        QWidget* parent = nullptr);

    void RemoveClip(ClipId clip_id);
    void ClearClips();

    void StartRuntime(
        qint64 position_ms,
        const QVector<TimelineClip>& clips);
    void SyncRuntime(
        qint64 position_ms,
        const QVector<TimelineClip>& clips);
    void StopRuntime();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
