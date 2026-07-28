#pragma once

#include "application/TimelineBackend.h"

#include <memory>

class ResourceManagerInterface;

namespace lighttrack::openrgb
{
class OpenRgbTimelineBackend final : public TimelineBackend
{
public:
    explicit OpenRgbTimelineBackend(ResourceManagerInterface* resource_manager);
    ~OpenRgbTimelineBackend() override;

    OpenRgbTimelineBackend(const OpenRgbTimelineBackend&) = delete;
    OpenRgbTimelineBackend& operator=(const OpenRgbTimelineBackend&) = delete;

    QVector<EffectGroup> Effects() const override;
    bool FindEffect(
        const QString& effect_id,
        EffectDescriptor* descriptor = nullptr) const override;

    DeviceSnapshot ReloadDevices() override;
    QByteArray SerializeLane(int lane_index) const override;
    int ResolveLane(const QByteArray& serialized_lane) const override;
    bool RenameLane(
        int lane_index,
        const QString& name) override;
    bool SetLaneHighlighted(
        int lane_index,
        bool highlighted) override;
    bool SetLaneDisabled(
        int lane_index,
        bool disabled) override;

    EffectPtr CreateEffect(
        const QString& effect_id,
        QString* error = nullptr) const override;
    QByteArray ExportEffectSettings(const Effect& effect) const override;
    bool ImportEffectSettings(
        Effect& effect,
        const QByteArray& settings,
        QString* error = nullptr) const override;

    bool AttachEffect(
        ClipId clip_id,
        EffectPtr effect,
        QString* error = nullptr) override;
    bool ReplaceEffects(
        std::vector<ClipEffect> effects,
        QString* error = nullptr) override;
    bool EnsureEffect(
        ClipId clip_id,
        const QString& effect_id,
        QString* error = nullptr) override;
    QByteArray ExportClipSettings(ClipId clip_id) const override;
    QWidget* CreateSettingsPage(
        ClipId clip_id,
        QWidget* parent = nullptr) override;

    void RemoveClip(ClipId clip_id) override;
    void ClearClips() override;

    void StartRuntime(
        qint64 position_ms,
        const QVector<TimelineClip>& clips) override;
    void SyncRuntime(
        qint64 position_ms,
        const QVector<TimelineClip>& clips) override;
    void StopRuntime() override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<TimelineBackend> CreateOpenRgbTimelineBackend(
    ResourceManagerInterface* resource_manager);
}
