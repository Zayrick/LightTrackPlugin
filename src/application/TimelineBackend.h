#pragma once

#include "core/TimelineTypes.h"
#include "core/effects/EffectDescriptor.h"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <memory>
#include <vector>

class QWidget;

namespace lighttrack
{
class TimelineBackend
{
public:
    class Effect
    {
    public:
        virtual ~Effect() = default;

        Effect(const Effect&) = delete;
        Effect& operator=(const Effect&) = delete;

    protected:
        Effect() = default;
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

    virtual ~TimelineBackend() = default;

    virtual QVector<EffectGroup> Effects() const = 0;
    virtual bool FindEffect(
        const QString& effect_id,
        EffectDescriptor* descriptor = nullptr) const = 0;

    virtual DeviceSnapshot ReloadDevices() = 0;
    virtual QByteArray SerializeLane(int lane_index) const = 0;
    virtual int ResolveLane(const QByteArray& serialized_lane) const = 0;

    virtual EffectPtr CreateEffect(
        const QString& effect_id,
        QString* error = nullptr) const = 0;
    virtual QByteArray ExportEffectSettings(const Effect& effect) const = 0;
    virtual bool ImportEffectSettings(
        Effect& effect,
        const QByteArray& settings,
        QString* error = nullptr) const = 0;

    virtual bool AttachEffect(
        ClipId clip_id,
        EffectPtr effect,
        QString* error = nullptr) = 0;
    virtual bool ReplaceEffects(
        std::vector<ClipEffect> effects,
        QString* error = nullptr) = 0;
    virtual bool EnsureEffect(
        ClipId clip_id,
        const QString& effect_id,
        QString* error = nullptr) = 0;
    virtual QByteArray ExportClipSettings(ClipId clip_id) const = 0;
    virtual QWidget* CreateSettingsPage(
        ClipId clip_id,
        QWidget* parent = nullptr) = 0;

    virtual void RemoveClip(ClipId clip_id) = 0;
    virtual void ClearClips() = 0;

    virtual void StartRuntime(
        qint64 position_ms,
        const QVector<TimelineClip>& clips) = 0;
    virtual void SyncRuntime(
        qint64 position_ms,
        const QVector<TimelineClip>& clips) = 0;
    virtual void StopRuntime() = 0;
};
}
