#pragma once

#include "../TimelineEditor.h"
#include "../TimelineMetrics.h"
#include "../../UiHelpers.h"

#include <QAbstractScrollArea>
#include <QColor>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHash>
#include <QIcon>
#include <QPalette>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QTreeWidget>
#include <QVector>
#include <QWidget>

class QDragMoveEvent;
class QDropEvent;
class QDragEnterEvent;
class QDragLeaveEvent;
class QEvent;
class QGraphicsEllipseItem;
class QGraphicsLineItem;
class QGraphicsSceneHoverEvent;
class QGraphicsSceneMouseEvent;
class QKeyEvent;
class QLabel;
class QMouseEvent;
class QPainter;
class QPaintEvent;
class QResizeEvent;
class QScrollBar;
class QStyleOptionGraphicsItem;
class QStyleOptionViewItem;
class QTreeWidgetItem;
class QWheelEvent;

namespace lighttrack::timeline_internal
{
inline constexpr const char* EFFECT_MIME = "application/x-lighttrack-effect";

inline constexpr qreal ROW_HEIGHT = 36.0;
inline constexpr qreal EFFECT_ROW_HEIGHT = 28.0;
inline constexpr qreal CLIP_HEIGHT = 32.0;
inline constexpr qreal CLIP_MIN_WIDTH = 72.0;
inline constexpr qreal CLIP_DEFAULT_WIDTH = 130.0;
inline constexpr qreal TIMELINE_TICK_TARGET_WIDTH = 90.0;
inline constexpr qreal RESIZE_HANDLE_WIDTH = 9.0;
inline constexpr qreal PLAYHEAD_HANDLE_RADIUS = 6.0;
inline constexpr qreal SNAP_DISTANCE = 8.0;

using lighttrack::timeline_metrics::EFFECTS_PANEL_WIDTH;
using lighttrack::timeline_metrics::GAP;
using lighttrack::timeline_metrics::GRID_WIDTH;
using lighttrack::timeline_metrics::LABEL_WIDTH;
using lighttrack::timeline_metrics::RULER_HEIGHT;
using lighttrack::timeline_metrics::TIMELINE_MIN_WIDTH;
using lighttrack::timeline_metrics::TIMELINE_ZOOM_MAX;
using lighttrack::timeline_metrics::TIMELINE_ZOOM_MIN;
using lighttrack::ui::HeaderLabel;

enum EffectListRole
{
    EffectIdRole = Qt::UserRole + 1,
    LaneIndexRole
};

struct LaneInfo
{
    QString name;
    QRectF rect;
    int level = 0;
};

QColor WithAlpha(QColor color, int alpha);
QColor TextLineColor(
    const QPalette& palette,
    int light_alpha,
    int dark_alpha);
qint64 TimelineTickIntervalMs(qreal pixels_per_second);
QString FormatTimelineTime(
    qint64 time_ms,
    qint64 tick_interval_ms);
void PaintClipCard(
    QPainter* painter,
    const QRectF& rect,
    const EffectDescriptor& effect,
    bool preview,
    bool selected = false);
QPoint EventPosition(const QDropEvent* event);
QPoint EventPosition(const QDragMoveEvent* event);
class OverlayScrollBar final : public QWidget
{
public:
    explicit OverlayScrollBar(
        QAbstractScrollArea* scroll_area,
        Qt::Orientation orientation);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    static constexpr int HIT_WIDTH = 14;
    static constexpr int THUMB_WIDTH = 5;
    static constexpr int EDGE_MARGIN = 3;
    static constexpr int TRACK_PADDING = 2;
    static constexpr int MIN_THUMB_LENGTH = 28;
    static constexpr int REVEAL_DISTANCE = 26;
    static constexpr int HIDE_DELAY_MS = 650;
    bool IsScrollable() const;
    qreal AxisPosition(const QPoint& point) const;
    qreal TrackLength() const;
    qreal ThumbLength() const;
    QRectF ThumbRect() const;
    void SetValueFromThumbPosition(qreal position);
    void UpdateGeometry();
    void UpdatePointerProximity(const QPoint& viewport_position);
    void UpdatePointerProximityFromCursor();
    void Reveal();
    void RevealTemporarily();
    void FadeTo(qreal target);
    QAbstractScrollArea* scroll_area;
    QScrollBar* scroll_bar;
    Qt::Orientation orientation;
    QTimer fade_timer;
    QTimer hide_timer;
    qreal opacity = 0.0;
    qreal target_opacity = 0.0;
    qreal drag_offset = 0.0;
    qreal wheel_remainder = 0.0;
    bool pointer_near = false;
    bool dragging = false;
};

class ClipPreviewItem final : public QGraphicsItem
{
public:
    ClipPreviewItem();
    QRectF boundingRect() const override;
    void SetPreview(qreal clip_width, const EffectDescriptor& preview_effect);
    void paint(
        QPainter* painter,
        const QStyleOptionGraphicsItem*,
        QWidget*) override;

private:
    qreal width = CLIP_DEFAULT_WIDTH;
    EffectDescriptor effect;
};

class MusicSpectrumItem final : public QGraphicsItem
{
public:
    MusicSpectrumItem();
    QRectF boundingRect() const override;
    void SetSpectrum(
        const QVector<qreal>& new_levels,
        qreal new_width,
        qreal new_height);
    void paint(
        QPainter* painter,
        const QStyleOptionGraphicsItem*,
        QWidget*) override;

private:
    QVector<qreal> levels;
    qreal width = 0.0;
    qreal height = 0.0;
};

class TimelineClipItem final : public QGraphicsItem
{
public:
    TimelineClipItem(ClipId id, const EffectDescriptor& effect);
    QRectF boundingRect() const override;
    void paint(
        QPainter* painter,
        const QStyleOptionGraphicsItem* option,
        QWidget*) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent*) override;
    ClipId Id() const;
    const EffectDescriptor& Effect() const;
    int LaneIndex() const;
    void SetLaneIndex(int index);
    qreal ClipWidth() const;
    void SetClipWidth(qreal clip_width);
    bool IsResizeHandle(const QPointF& pos) const;
    bool IsLeftResizeHandle(const QPointF& pos) const;
    bool IsRightResizeHandle(const QPointF& pos) const;

private:
    ClipId id;
    EffectDescriptor effect;
    int lane_index = 0;
    qreal width = CLIP_DEFAULT_WIDTH;
};

class LightTrackScene final : public QGraphicsScene
{
public:
    explicit LightTrackScene(QObject* parent = nullptr);
    void SetEffects(const QVector<EffectGroup>& groups);
    void SetPalette(const QPalette& new_palette);
    void SetViewportSize(const QSize& size);
    void SetLanes(
        const QVector<TimelineLane>& entries,
        const QString& empty_text);
    void SetVisibleLanes(const QVector<int>& indices);
    void SetMusicSpectrum(
        const QVector<qreal>& spectrum,
        qint64 duration_ms);
    void SetMusicPosition(qint64 position_ms);
    void SetMinimumContentDuration(qint64 duration_ms);
    void SetMusicSeekCallback(TimelineEditor::MusicSeekCallback callback);
    void SetClipSelectedCallback(
        TimelineEditor::ClipSelectedCallback callback);
    void SetClipRemovedCallback(
        TimelineEditor::ClipRemovedCallback callback);
    void SetTimelineChangedCallback(
        TimelineEditor::TimelineChangedCallback callback);
    void ClearTimelineClips();
    ClipId RestoreClip(
        const QString& effect_id,
        int lane,
        qint64 start_ms,
        qint64 end_ms,
        ClipId requested_id);
    void SetSnappingEnabled(bool enabled);
    bool SetPixelsPerSecond(qreal value);
    qreal PixelsPerSecond() const;
    qreal ContentWidth() const;
    QVector<TimelineClip> TimelineClips() const;
    QVector<TimelineClip> PersistentTimelineClips() const;
    bool PreviewEffectAt(const QString& effect_id, const QPointF& pos);
    bool AddEffectAt(const QString& effect_id, const QPointF& pos);
    void ClearPreview();
    bool DeleteSelectedClips();

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    enum DragMode
    {
        NoDrag,
        ClipMove,
        ClipResizeLeft,
        ClipResizeRight,
        PlayheadSeek
    };
    template<typename ItemType>
    ItemType* Track(ItemType* item);
    const EffectDescriptor* FindEffect(const QString& id) const;
    bool ContainsClipId(ClipId id) const;
    ClipId AllocateClipId(ClipId requested);
    QVector<TimelineClip> BuildClipSnapshots(bool persistent) const;
    void ClearLayoutItems();
    void ClearClips();
    void RebuildLayout();
    void RefreshInheritedClips();
    void PaintRow(
        QPainter* painter,
        const QRectF& row_rect,
        const QColor& fill,
        const QColor& border,
        const QColor& grid,
        const QRectF& exposed) const;
    void RelayoutClips();
    bool IsDescendantLane(int lane, int ancestor_lane) const;
    void PaintGhostClip(
        QPainter* painter,
        const QRectF& exposed,
        int lane,
        qreal x,
        qreal width,
        const EffectDescriptor& effect,
        bool preview_ghost) const;
    void PaintInheritedClips(
        QPainter* painter,
        const QRectF& exposed) const;
    TimelineClipItem* ClipAt(const QPointF& pos) const;
    int LaneAt(const QPointF& pos) const;
    qreal ClipY(int lane) const;
    qreal ClampClipX(int lane, qreal x, qreal width) const;
    qreal SnapClipX(
        qreal x,
        qreal width,
        const TimelineClipItem* ignored_clip) const;
    qreal SnapEdgeX(
        qreal x,
        const TimelineClipItem* ignored_clip) const;
    qreal SnapDelta(
        const QVector<qreal>& moving_edges,
        const TimelineClipItem* ignored_clip) const;
    qreal MusicPixelWidth() const;
    void AddMusicItems();
    void UpdatePlayhead();
    bool IsPlayheadHandle(const QPointF& pos) const;
    void SeekMusicAt(qreal x);
    void UpdateClipMove(const QPointF& pos);
    void UpdateClipResize(const QPointF& pos);
    void ShowPreview(
        int lane,
        qreal x,
        qreal width,
        const EffectDescriptor& effect);
    void HidePreview();
    TimelineClipItem* AddClip(
        const EffectDescriptor& effect,
        int lane,
        qreal x,
        qreal width,
        bool select,
        bool notify_change,
        ClipId requested_id);
    qreal DefaultClipWidth() const;
    void RemoveClip(TimelineClipItem* clip);
    void NotifyClipSelected(TimelineClipItem* clip);
    void NotifyTimelineChanged();
    void CancelDrag();
    QHash<QString, EffectDescriptor> effects;
    QPalette palette;
    QSize viewport_size;
    QVector<TimelineLane> lane_entries;
    QVector<int> visible_lane_indices;
    QVector<LaneInfo> lanes;
    QVector<QGraphicsItem*> layout_items;
    QVector<TimelineClipItem*> clips;
    QString empty_message;
    QVector<qreal> music_spectrum;
    TimelineEditor::MusicSeekCallback music_seek_callback;
    TimelineEditor::ClipSelectedCallback clip_selected_callback;
    TimelineEditor::ClipRemovedCallback clip_removed_callback;
    TimelineEditor::TimelineChangedCallback timeline_changed_callback;
    qint64 music_duration_ms = 0;
    qint64 music_position_ms = 0;
    qint64 minimum_content_duration_ms = 0;
    qreal pixels_per_second = GRID_WIDTH;
    bool snapping_enabled = true;
    quint64 next_clip_id = 0;
    DragMode drag_mode = NoDrag;
    QPointF drag_offset;
    QPointF drag_scene_start;
    qreal clip_start_x = 0.0;
    qreal clip_start_width = CLIP_DEFAULT_WIDTH;
    int clip_start_lane = -1;
    int last_preview_lane = -1;
    qreal last_preview_x = 0.0;
    qreal last_preview_width = CLIP_DEFAULT_WIDTH;
    EffectDescriptor last_preview_effect;
    TimelineClipItem* active_clip = nullptr;
    ClipPreviewItem* preview = nullptr;
    MusicSpectrumItem* music_item = nullptr;
    QGraphicsLineItem* playhead_item = nullptr;
    QGraphicsEllipseItem* playhead_handle = nullptr;
};

class TimelineRulerWidget final : public QWidget
{
public:
    explicit TimelineRulerWidget(QWidget* parent = nullptr);
    void SetContentWidth(qreal width);
    void SetPixelsPerSecond(qreal value);
    void SetScrollOffset(int offset);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    qreal content_width = TIMELINE_MIN_WIDTH;
    qreal pixels_per_second = GRID_WIDTH;
    int horizontal_offset = 0;
};

class LaneListWidget final : public QTreeWidget
{
public:
    explicit LaneListWidget(QWidget* parent = nullptr);
    void SetLanes(const QVector<TimelineLane>& lanes);
    void SetVisibilityChangedCallback(
        std::function<void(const QVector<int>&)> callback);
    QVector<int> VisibleLaneIndices() const;

protected:
    void drawRow(
        QPainter* painter,
        const QStyleOptionViewItem& option,
        const QModelIndex& index) const override;

private:
    void AppendVisibleLaneIndices(
        const QTreeWidgetItem* item,
        QVector<int>& indices) const;
    void NotifyVisibleLanesChanged();
    bool rebuilding = false;
    std::function<void(const QVector<int>&)>
        visibility_changed_callback;
};

class EffectsListWidget final : public QTreeWidget
{
public:
    explicit EffectsListWidget(QWidget* parent = nullptr);
    void SetEffects(const QVector<EffectGroup>& groups);

protected:
    void startDrag(Qt::DropActions) override;

private:
    QIcon ColorIcon(const QColor& color) const;
};

class LightTrackView final : public QGraphicsView
{
public:
    explicit LightTrackView(QWidget* parent = nullptr);
    void SetRuler(TimelineRulerWidget* ruler_widget);
    void SetEffects(const QVector<EffectGroup>& groups);
    void SetLanes(
        const QVector<TimelineLane>& lanes,
        const QString& empty_text);
    void SetVisibleLanes(const QVector<int>& lane_indices);
    void SetMusicSpectrum(
        const QVector<qreal>& spectrum,
        qint64 duration_ms);
    void SetMusicPosition(qint64 position_ms);
    void SetMinimumTimelineDuration(qint64 duration_ms);
    QVector<TimelineClip> TimelineClips() const;
    QVector<TimelineClip> PersistentTimelineClips() const;
    void ClearTimelineClips();
    ClipId RestoreClip(
        const QString& effect_id,
        int lane,
        qint64 start_ms,
        qint64 end_ms,
        ClipId requested_id);
    void SetMusicSeekCallback(
        TimelineEditor::MusicSeekCallback callback);
    void SetClipSelectedCallback(
        TimelineEditor::ClipSelectedCallback callback);
    void SetClipRemovedCallback(
        TimelineEditor::ClipRemovedCallback callback);
    void SetTimelineChangedCallback(
        TimelineEditor::TimelineChangedCallback callback);
    void SetHorizontalZoom(int pixels_per_second);
    void SetSnappingEnabled(bool enabled);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void SyncRuler();
    LightTrackScene* light_scene;
    TimelineRulerWidget* ruler = nullptr;
};
}
