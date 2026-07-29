#pragma once

#include "core/TimelineTypes.h"
#include "core/effects/EffectDescriptor.h"

#include <QVector>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

namespace lighttrack
{
class TimelineEditorPrivate;

class TimelineEditor final : public QWidget
{
public:
    using MusicSeekCallback = std::function<void(qint64)>;
    using ClipSelectedCallback = std::function<void(std::optional<ClipId>)>;
    using ClipRemovedCallback = std::function<void(ClipId)>;
    using ClipDuplicatedCallback =
        std::function<void(ClipId, ClipId)>;
    using TimelineChangedCallback = std::function<void()>;
    using LaneRenamedCallback =
        std::function<bool(int, const QString&)>;
    using LaneStateChangedCallback =
        std::function<bool(int, bool)>;

    explicit TimelineEditor(QWidget* parent = nullptr);
    ~TimelineEditor() override;

    TimelineEditor(const TimelineEditor&) = delete;
    TimelineEditor& operator=(const TimelineEditor&) = delete;

    void SetEffects(const QVector<EffectGroup>& groups);
    void SetLanes(const QVector<TimelineLane>& lanes, const QString& empty_text = {});
    void SetMusicSpectrum(const QVector<qreal>& spectrum, qint64 duration_ms);
    void SetMusicPosition(
        qint64 position_ms,
        bool turn_page = false);
    void SetMinimumTimelineDuration(qint64 duration_ms);
    void SetHorizontalZoom(int pixels_per_second);
    void SetSnappingEnabled(bool enabled);

    QVector<TimelineClip> TimelineClips() const;
    QVector<TimelineClip> PersistentTimelineClips() const;
    std::optional<TimelineClip> FindClip(ClipId id) const;
    void ClearTimelineClips();
    ClipId RestoreClip(
        const QString& effect_id,
        int lane_index,
        qint64 start_ms,
        qint64 end_ms,
        ClipId requested_id = {});

    void SetMusicSeekCallback(MusicSeekCallback callback);
    void SetClipSelectedCallback(ClipSelectedCallback callback);
    void SetClipRemovedCallback(ClipRemovedCallback callback);
    void SetClipDuplicatedCallback(
        ClipDuplicatedCallback callback);
    void SetTimelineChangedCallback(TimelineChangedCallback callback);
    void SetLaneRenamedCallback(LaneRenamedCallback callback);
    void SetLaneHighlightedCallback(
        LaneStateChangedCallback callback);
    void SetLaneDisabledCallback(
        LaneStateChangedCallback callback);

private:
    std::unique_ptr<TimelineEditorPrivate> d;
};
}
