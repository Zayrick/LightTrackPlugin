#include "TimelineEditorInternal.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <utility>

namespace lighttrack::timeline_internal
{
LightTrackScene::LightTrackScene(QObject* parent) :
    QGraphicsScene(parent)
{
}

void LightTrackScene::SetEffects(const QVector<EffectGroup>& groups)
{
    effects.clear();
    for(const EffectGroup& group : groups)
    {
        for(EffectDescriptor effect : group.effects)
        {
            if(effect.category.isEmpty())
            {
                effect.category = group.name;
            }
            effects.insert(effect.id, std::move(effect));
        }
    }
}

void LightTrackScene::SetPalette(const QPalette& new_palette)
{
    palette = new_palette;
    RebuildLayout();
}

void LightTrackScene::SetViewportSize(const QSize& size)
{
    if(viewport_size == size)
    {
        return;
    }

    viewport_size = size;
    RebuildLayout();
}

void LightTrackScene::SetLanes(
    const QVector<TimelineLane>& entries,
    const QString& empty_text)
{
    CancelDrag();
    ClearClips();
    lane_entries = entries;
    visible_lane_indices.clear();
    visible_lane_indices.reserve(lane_entries.size());
    for(int i = 0; i < lane_entries.size(); i++)
    {
        visible_lane_indices.push_back(i);
    }
    empty_message = empty_text;
    RebuildLayout();
}

void LightTrackScene::SetVisibleLanes(const QVector<int>& indices)
{
    QVector<int> filtered_indices;
    filtered_indices.reserve(indices.size());

    for(int index : indices)
    {
        if(index >= 0
            && index < lane_entries.size()
            && !filtered_indices.contains(index))
        {
            filtered_indices.push_back(index);
        }
    }

    if(filtered_indices.size() == visible_lane_indices.size()
        && std::equal(
            filtered_indices.cbegin(),
            filtered_indices.cend(),
            visible_lane_indices.cbegin()))
    {
        return;
    }

    CancelDrag();
    visible_lane_indices = filtered_indices;
    RebuildLayout();
}

void LightTrackScene::SetMusicSpectrum(
    const QVector<qreal>& spectrum,
    qint64 duration_ms)
{
    music_spectrum = spectrum;
    music_duration_ms = qMax<qint64>(0, duration_ms);
    music_position_ms = 0;
    RebuildLayout();
}

void LightTrackScene::SetMusicPosition(qint64 position_ms)
{
    music_position_ms = qMax<qint64>(0, position_ms);
    if(music_duration_ms > 0)
    {
        music_position_ms =
            qMin(music_position_ms, music_duration_ms);
    }
    UpdatePlayhead();
}

void LightTrackScene::SetMinimumContentDuration(qint64 duration_ms)
{
    const qint64 clamped_duration_ms = qMax<qint64>(0, duration_ms);
    if(minimum_content_duration_ms == clamped_duration_ms)
    {
        return;
    }

    minimum_content_duration_ms = clamped_duration_ms;
    RebuildLayout();
}

void LightTrackScene::SetMusicSeekCallback(TimelineEditor::MusicSeekCallback callback)
{
    music_seek_callback = std::move(callback);
}

void LightTrackScene::SetClipSelectedCallback(
    TimelineEditor::ClipSelectedCallback callback)
{
    clip_selected_callback = std::move(callback);
}

void LightTrackScene::SetClipRemovedCallback(
    TimelineEditor::ClipRemovedCallback callback)
{
    clip_removed_callback = std::move(callback);
}

void LightTrackScene::SetTimelineChangedCallback(
    TimelineEditor::TimelineChangedCallback callback)
{
    timeline_changed_callback = std::move(callback);
}

void LightTrackScene::ClearTimelineClips()
{
    CancelDrag();
    ClearClips();
    RefreshInheritedClips();
}

ClipId LightTrackScene::RestoreClip(
    const QString& effect_id,
    int lane,
    qint64 start_ms,
    qint64 end_ms,
    ClipId requested_id)
{
    const EffectDescriptor* effect = FindEffect(effect_id);
    if(effect == nullptr
        || lane < 0
        || lane >= lanes.size()
        || end_ms <= start_ms
        || pixels_per_second <= 0.0)
    {
        return {};
    }

    const qreal x = start_ms * pixels_per_second / 1000.0;
    const qreal width = qMax(
        CLIP_MIN_WIDTH,
        (end_ms - start_ms) * pixels_per_second / 1000.0);
    TimelineClipItem* clip =
        AddClip(*effect, lane, x, width, false, false, requested_id);
    return clip == nullptr ? ClipId{} : clip->Id();
}

void LightTrackScene::SetSnappingEnabled(bool enabled)
{
    snapping_enabled = enabled;
}

bool LightTrackScene::SetPixelsPerSecond(qreal value)
{
    value = qBound(
        TIMELINE_ZOOM_MIN,
        value,
        TIMELINE_ZOOM_MAX);
    if(qFuzzyCompare(pixels_per_second, value))
    {
        return false;
    }

    CancelDrag();
    const qreal ratio = value / pixels_per_second;
    for(TimelineClipItem* clip : clips)
    {
        clip->SetClipWidth(clip->ClipWidth() * ratio);
        clip->setPos(clip->pos().x() * ratio, clip->pos().y());
    }

    pixels_per_second = value;
    RebuildLayout();
    return true;
}

qreal LightTrackScene::PixelsPerSecond() const
{
    return pixels_per_second;
}

qreal LightTrackScene::ContentWidth() const
{
    return sceneRect().width();
}

QVector<TimelineClip> LightTrackScene::TimelineClips() const
{
    return BuildClipSnapshots(false);
}

QVector<TimelineClip> LightTrackScene::PersistentTimelineClips() const
{
    return BuildClipSnapshots(true);
}

bool LightTrackScene::PreviewEffectAt(const QString& effect_id, const QPointF& pos)
{
    const EffectDescriptor* effect = FindEffect(effect_id);
    if(effect == nullptr)
    {
        HidePreview();
        return false;
    }

    const int lane = LaneAt(pos);
    if(lane < 0)
    {
        HidePreview();
        return false;
    }

    const qreal clip_width = DefaultClipWidth();
    const qreal x =
        ClampClipX(
            lane,
            SnapClipX(
                pos.x() - clip_width / 2.0,
                clip_width,
                nullptr),
            clip_width);
    ShowPreview(lane, x, clip_width, *effect);
    return true;
}

bool LightTrackScene::AddEffectAt(const QString& effect_id, const QPointF& pos)
{
    const EffectDescriptor* effect = FindEffect(effect_id);
    if(effect == nullptr)
    {
        return false;
    }

    const int lane = LaneAt(pos);
    if(lane < 0)
    {
        return false;
    }

    const qreal clip_width = DefaultClipWidth();
    const qreal x = SnapClipX(
        pos.x() - clip_width / 2.0,
        clip_width,
        nullptr);
    return AddClip(
               *effect,
               lane,
               x,
               clip_width,
               true,
               true,
               {})
        != nullptr;
}

void LightTrackScene::ClearPreview()
{
    HidePreview();
}

bool LightTrackScene::DeleteSelectedClips()
{
    QVector<TimelineClipItem*> selected_clips;
    for(TimelineClipItem* clip : clips)
    {
        if(clip->isSelected())
        {
            selected_clips.push_back(clip);
        }
    }

    for(TimelineClipItem* clip : selected_clips)
    {
        RemoveClip(clip);
    }

    if(!selected_clips.empty())
    {
        NotifyTimelineChanged();
    }

    return !selected_clips.empty();
}

void LightTrackScene::drawBackground(QPainter* painter, const QRectF& rect)
{
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->fillRect(rect, palette.color(QPalette::Base));

    QColor music_row_color = palette.color(QPalette::Window);
    if(!music_row_color.isValid())
    {
        music_row_color = palette.color(QPalette::Base);
    }

    PaintRow(
        painter,
        QRectF(0.0, 0.0, sceneRect().width(), ROW_HEIGHT),
        music_row_color,
        TextLineColor(palette, 52, 82),
        TextLineColor(palette, 24, 42),
        rect);

    if(lane_entries.empty())
    {
        if(!empty_message.isEmpty())
        {
            QFont font;
            font.setPointSize(10);
            painter->setRenderHint(QPainter::TextAntialiasing, true);
            painter->setFont(font);
            painter->setPen(TextLineColor(palette, 160, 170));
            painter->drawText(
                QRectF(
                    12.0,
                    ROW_HEIGHT + 14.0,
                    sceneRect().width() - 24.0,
                    ROW_HEIGHT),
                Qt::AlignLeft | Qt::AlignTop,
                empty_message);
        }
        return;
    }

    for(int lane_index : visible_lane_indices)
    {
        PaintRow(
            painter,
            lanes[lane_index].rect,
            palette.color(QPalette::Base),
            TextLineColor(palette, 38, 72),
            TextLineColor(palette, 20, 38),
            rect);
    }

    PaintInheritedClips(painter, rect);
}

void LightTrackScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if(event->button() != Qt::LeftButton)
    {
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    if(IsPlayheadHandle(event->scenePos()))
    {
        clearSelection();
        NotifyClipSelected(nullptr);
        drag_mode = PlayheadSeek;
        SeekMusicAt(event->scenePos().x());
        event->accept();
        return;
    }

    TimelineClipItem* clip = ClipAt(event->scenePos());
    if(clip == nullptr)
    {
        clearSelection();
        NotifyClipSelected(nullptr);
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    const QPointF local_pos =
        clip->mapFromScene(event->scenePos());
    clearSelection();
    clip->setSelected(true);
    NotifyClipSelected(clip);

    active_clip = clip;
    if(clip->IsLeftResizeHandle(local_pos))
    {
        drag_mode = ClipResizeLeft;
    }
    else if(clip->IsRightResizeHandle(local_pos))
    {
        drag_mode = ClipResizeRight;
    }
    else
    {
        drag_mode = ClipMove;
    }

    drag_offset = local_pos;
    drag_scene_start = event->scenePos();
    clip_start_x = clip->pos().x();
    clip_start_width = clip->ClipWidth();
    clip_start_lane = clip->LaneIndex();
    active_clip->setOpacity(0.88);
    active_clip->setZValue(80.0);

    if(drag_mode == ClipMove)
    {
        UpdateClipMove(event->scenePos());
    }
    else
    {
        UpdateClipResize(event->scenePos());
    }

    event->accept();
}

void LightTrackScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if(drag_mode == ClipMove)
    {
        UpdateClipMove(event->scenePos());
        event->accept();
        return;
    }

    if(drag_mode == ClipResizeLeft
        || drag_mode == ClipResizeRight)
    {
        UpdateClipResize(event->scenePos());
        event->accept();
        return;
    }

    if(drag_mode == PlayheadSeek)
    {
        SeekMusicAt(event->scenePos().x());
        event->accept();
        return;
    }

    QGraphicsScene::mouseMoveEvent(event);
}

void LightTrackScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if(drag_mode == PlayheadSeek)
    {
        drag_mode = NoDrag;
        event->accept();
        return;
    }

    if(drag_mode == ClipMove
        || drag_mode == ClipResizeLeft
        || drag_mode == ClipResizeRight)
    {
        bool changed = false;
        if(active_clip != nullptr)
        {
            changed =
                active_clip->LaneIndex() != clip_start_lane
                || !qFuzzyCompare(
                    active_clip->pos().x() + 1.0,
                    clip_start_x + 1.0)
                || !qFuzzyCompare(
                    active_clip->ClipWidth() + 1.0,
                    clip_start_width + 1.0);
            active_clip->setOpacity(1.0);
            active_clip->setZValue(35.0);
        }

        HidePreview();
        active_clip = nullptr;
        drag_mode = NoDrag;
        if(changed)
        {
            NotifyTimelineChanged();
        }
        event->accept();
        return;
    }

    QGraphicsScene::mouseReleaseEvent(event);
}

template<typename ItemType>
ItemType* LightTrackScene::Track(ItemType* item)
{
    item->setAcceptedMouseButtons(Qt::NoButton);
    layout_items.push_back(item);
    return item;
}

const EffectDescriptor* LightTrackScene::FindEffect(const QString& id) const
{
    const auto found = effects.constFind(id);
    return found == effects.cend() ? nullptr : &found.value();
}

bool LightTrackScene::ContainsClipId(ClipId id) const
{
    return std::any_of(
        clips.cbegin(),
        clips.cend(),
        [id](const TimelineClipItem* clip)
        {
            return clip->Id() == id;
        });
}

ClipId LightTrackScene::AllocateClipId(ClipId requested)
{
    if(requested.IsValid())
    {
        if(ContainsClipId(requested))
        {
            return {};
        }

        next_clip_id =
            qMax(next_clip_id, requested.Value());
        return requested;
    }

    do
    {
        ++next_clip_id;
        if(next_clip_id == 0)
        {
            ++next_clip_id;
        }
    }
    while(ContainsClipId(ClipId(next_clip_id)));

    return ClipId(next_clip_id);
}

QVector<TimelineClip> LightTrackScene::BuildClipSnapshots(bool persistent) const
{
    QVector<TimelineClip> states;
    if(pixels_per_second <= 0.0)
    {
        return states;
    }

    states.reserve(clips.size());
    for(TimelineClipItem* clip : clips)
    {
        const qreal start_x = clip->pos().x();
        const qreal start_value =
            start_x * 1000.0 / pixels_per_second;
        const qreal end_value =
            (start_x + clip->ClipWidth())
            * 1000.0 / pixels_per_second;

        states.push_back({
            clip->Id(),
            clip->Effect().id,
            clip->LaneIndex(),
            qMax<qint64>(
                0,
                persistent
                    ? qRound64(start_value)
                    : static_cast<qint64>(std::floor(start_value))),
            qMax<qint64>(
                0,
                persistent
                    ? qRound64(end_value)
                    : static_cast<qint64>(std::ceil(end_value)))
        });
    }

    return states;
}

void LightTrackScene::ClearLayoutItems()
{
    for(QGraphicsItem* item : layout_items)
    {
        removeItem(item);
        delete item;
    }
    layout_items.clear();

    if(preview != nullptr)
    {
        removeItem(preview);
        delete preview;
        preview = nullptr;
    }

    music_item = nullptr;
    playhead_item = nullptr;
    playhead_handle = nullptr;
}

void LightTrackScene::ClearClips()
{
    for(TimelineClipItem* clip : clips)
    {
        if(clip_removed_callback)
        {
            clip_removed_callback(clip->Id());
        }
        removeItem(clip);
        delete clip;
    }
    clips.clear();
    active_clip = nullptr;
    NotifyClipSelected(nullptr);
}

void LightTrackScene::RebuildLayout()
{
    ClearLayoutItems();
    lanes.clear();

    const int lane_count = qMax(1, visible_lane_indices.size());
    const qreal content_height =
        ROW_HEIGHT + lane_count * ROW_HEIGHT;
    const qreal minimum_content_width =
        minimum_content_duration_ms * pixels_per_second / 1000.0;
    const qreal scene_width = qMax(
        qMax(
            qMax<qreal>(TIMELINE_MIN_WIDTH, viewport_size.width()),
            MusicPixelWidth()),
        minimum_content_width);
    const qreal scene_height =
        qMax<qreal>(content_height, viewport_size.height());

    setSceneRect(0.0, 0.0, scene_width, scene_height);

    for(const TimelineLane& lane : lane_entries)
    {
        lanes.push_back({lane.name, QRectF(), lane.level});
    }

    for(
        int visible_row = 0;
        visible_row < visible_lane_indices.size();
        visible_row++)
    {
        const int lane_index = visible_lane_indices[visible_row];
        lanes[lane_index].rect = QRectF(
            0.0,
            ROW_HEIGHT + visible_row * ROW_HEIGHT,
            scene_width,
            ROW_HEIGHT);
    }

    invalidate(sceneRect(), QGraphicsScene::BackgroundLayer);
    AddMusicItems();
    RelayoutClips();
}

void LightTrackScene::RefreshInheritedClips()
{
    invalidate(sceneRect(), QGraphicsScene::BackgroundLayer);
}

void LightTrackScene::PaintRow(
    QPainter* painter,
    const QRectF& row_rect,
    const QColor& fill,
    const QColor& border,
    const QColor& grid,
    const QRectF& exposed) const
{
    if(!row_rect.intersects(exposed))
    {
        return;
    }

    painter->fillRect(row_rect.intersected(exposed), fill);
    painter->setPen(QPen(border));
    painter->drawRect(
        row_rect.adjusted(0.0, 0.0, -0.5, -0.5));

    if(pixels_per_second <= 0.0)
    {
        return;
    }

    const qint64 tick_interval_ms =
        TimelineTickIntervalMs(pixels_per_second);
    const qreal tick_width =
        tick_interval_ms * pixels_per_second / 1000.0;
    painter->setPen(QPen(grid));
    const qint64 first_tick = static_cast<qint64>(
        std::ceil(qMax(tick_width, exposed.left()) / tick_width));

    for(qint64 tick = first_tick;; tick++)
    {
        const qreal x = tick * tick_width;
        if(x >= row_rect.right() || x > exposed.right())
        {
            break;
        }
        painter->drawLine(
            QPointF(x, row_rect.top() + 1.0),
            QPointF(x, row_rect.bottom() - 1.0));
    }
}

void LightTrackScene::RelayoutClips()
{
    if(lanes.empty())
    {
        return;
    }

    for(TimelineClipItem* clip : clips)
    {
        if(clip->scene() == nullptr)
        {
            addItem(clip);
        }

        const int lane =
            qBound(0, clip->LaneIndex(), lanes.size() - 1);
        clip->SetLaneIndex(lane);
        if(!lanes[lane].rect.isValid()
            || lanes[lane].rect.isEmpty())
        {
            clip->setVisible(false);
            continue;
        }

        clip->setVisible(true);
        const qreal x = ClampClipX(
            lane,
            clip->pos().x(),
            clip->ClipWidth());
        clip->setPos(x, ClipY(lane));
    }

    RefreshInheritedClips();
}

bool LightTrackScene::IsDescendantLane(int lane, int ancestor_lane) const
{
    if(lane <= ancestor_lane
        || lane >= lanes.size()
        || ancestor_lane >= lanes.size())
    {
        return false;
    }

    const int ancestor_level = lanes[ancestor_lane].level;
    for(int i = ancestor_lane + 1; i <= lane; i++)
    {
        if(lanes[i].level <= ancestor_level)
        {
            return false;
        }
    }

    return true;
}

void LightTrackScene::PaintGhostClip(
    QPainter* painter,
    const QRectF& exposed,
    int lane,
    qreal x,
    qreal width,
    const EffectDescriptor& effect,
    bool preview_ghost) const
{
    const QRectF ghost_rect(x, ClipY(lane), width, CLIP_HEIGHT);
    if(!ghost_rect.intersects(exposed))
    {
        return;
    }

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(
        WithAlpha(effect.color, preview_ghost ? 36 : 44));
    painter->setPen(QPen(
        WithAlpha(
            effect.color.darker(115),
            preview_ghost ? 100 : 125),
        1.0,
        preview_ghost ? Qt::DashLine : Qt::SolidLine));
    painter->drawRoundedRect(ghost_rect, 5.0, 5.0);

    QFont font = painter->font();
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(
        WithAlpha(Qt::white, preview_ghost ? 88 : 110));
    painter->drawText(
        ghost_rect.adjusted(9.0, 0.0, -9.0, 0.0),
        Qt::AlignVCenter | Qt::AlignLeft,
        QFontMetrics(font).elidedText(
            effect.name,
            Qt::ElideRight,
            static_cast<int>(ghost_rect.width() - 18.0)));
}

void LightTrackScene::PaintInheritedClips(
    QPainter* painter,
    const QRectF& exposed) const
{
    for(int lane = 0; lane < lanes.size(); lane++)
    {
        if(!lanes[lane].rect.intersects(exposed))
        {
            continue;
        }

        for(
            int ancestor_lane = 0;
            ancestor_lane < lane;
            ancestor_lane++)
        {
            if(!IsDescendantLane(lane, ancestor_lane))
            {
                continue;
            }

            for(TimelineClipItem* clip : clips)
            {
                if(clip->LaneIndex() == ancestor_lane)
                {
                    PaintGhostClip(
                        painter,
                        exposed,
                        lane,
                        clip->pos().x(),
                        clip->ClipWidth(),
                        clip->Effect(),
                        false);
                }
            }

            if(active_clip == nullptr
                && last_preview_lane == ancestor_lane)
            {
                PaintGhostClip(
                    painter,
                    exposed,
                    lane,
                    last_preview_x,
                    last_preview_width,
                    last_preview_effect,
                    true);
            }
        }
    }
}

TimelineClipItem* LightTrackScene::ClipAt(const QPointF& pos) const
{
    return dynamic_cast<TimelineClipItem*>(
        itemAt(pos, QTransform()));
}

int LightTrackScene::LaneAt(const QPointF& pos) const
{
    for(int i = 0; i < lanes.size(); i++)
    {
        if(lanes[i].rect.contains(pos))
        {
            return i;
        }
    }
    return -1;
}

qreal LightTrackScene::ClipY(int lane) const
{
    return lanes[lane].rect.top()
        + (lanes[lane].rect.height() - CLIP_HEIGHT) / 2.0;
}

qreal LightTrackScene::ClampClipX(int lane, qreal x, qreal width) const
{
    const QRectF rect = lanes[lane].rect;
    return qBound(rect.left(), x, rect.right() - width);
}

qreal LightTrackScene::SnapClipX(
    qreal x,
    qreal width,
    const TimelineClipItem* ignored_clip) const
{
    return x + SnapDelta(
        {x, x + width},
        ignored_clip);
}

qreal LightTrackScene::SnapEdgeX(
    qreal x,
    const TimelineClipItem* ignored_clip) const
{
    return x + SnapDelta({x}, ignored_clip);
}

qreal LightTrackScene::SnapDelta(
    const QVector<qreal>& moving_edges,
    const TimelineClipItem* ignored_clip) const
{
    if(!snapping_enabled || moving_edges.empty())
    {
        return 0.0;
    }

    qreal best_delta = 0.0;
    qreal best_distance = SNAP_DISTANCE + 1.0;
    const auto consider_target =
        [&best_delta, &best_distance](
            qreal moving_edge,
            qreal target)
        {
            const qreal delta = target - moving_edge;
            const qreal distance = qAbs(delta);
            if(distance <= SNAP_DISTANCE
                && distance < best_distance)
            {
                best_delta = delta;
                best_distance = distance;
            }
        };

    for(const TimelineClipItem* clip : clips)
    {
        if(clip == ignored_clip || !clip->isVisible())
        {
            continue;
        }

        const qreal clip_left = clip->pos().x();
        const qreal clip_right = clip_left + clip->ClipWidth();
        for(qreal moving_edge : moving_edges)
        {
            consider_target(moving_edge, clip_left);
            consider_target(moving_edge, clip_right);
        }
    }

    if(best_distance <= SNAP_DISTANCE)
    {
        return best_delta;
    }

    const qint64 interval_ms =
        TimelineTickIntervalMs(pixels_per_second);
    const qreal interval_width =
        interval_ms * pixels_per_second / 1000.0;
    if(interval_width <= 0.0)
    {
        return 0.0;
    }

    for(qreal moving_edge : moving_edges)
    {
        const qreal target =
            qRound64(moving_edge / interval_width)
            * interval_width;
        consider_target(moving_edge, target);
    }
    return best_distance <= SNAP_DISTANCE
        ? best_delta
        : 0.0;
}

qreal LightTrackScene::MusicPixelWidth() const
{
    if(music_duration_ms <= 0)
    {
        return 0.0;
    }
    return music_duration_ms * pixels_per_second / 1000.0;
}

void LightTrackScene::AddMusicItems()
{
    const qreal music_width = MusicPixelWidth();
    if(!music_spectrum.empty() && music_width > 0.0)
    {
        MusicSpectrumItem* item = new MusicSpectrumItem();
        addItem(item);
        Track(item);
        item->SetSpectrum(
            music_spectrum,
            music_width,
            ROW_HEIGHT);
        music_item = item;
    }

    if(music_duration_ms > 0)
    {
        playhead_item = Track(addLine(
            0.0,
            0.0,
            0.0,
            sceneRect().height(),
            QPen(QColor(255, 255, 255, 220), 2.0)));
        playhead_item->setZValue(95.0);
        playhead_handle = Track(addEllipse(
            -PLAYHEAD_HANDLE_RADIUS,
            ROW_HEIGHT / 2.0 - PLAYHEAD_HANDLE_RADIUS,
            PLAYHEAD_HANDLE_RADIUS * 2.0,
            PLAYHEAD_HANDLE_RADIUS * 2.0,
            QPen(QColor(255, 255, 255, 235), 2.0),
            QBrush(QColor(64, 188, 255))));
        playhead_handle->setZValue(100.0);
        UpdatePlayhead();
    }
}

void LightTrackScene::UpdatePlayhead()
{
    if(playhead_item == nullptr)
    {
        return;
    }

    const qreal x = qBound<qreal>(
        0.0,
        music_position_ms * pixels_per_second / 1000.0,
        sceneRect().right());
    playhead_item->setLine(
        x,
        0.0,
        x,
        sceneRect().height());

    if(playhead_handle != nullptr)
    {
        playhead_handle->setRect(
            x - PLAYHEAD_HANDLE_RADIUS,
            ROW_HEIGHT / 2.0 - PLAYHEAD_HANDLE_RADIUS,
            PLAYHEAD_HANDLE_RADIUS * 2.0,
            PLAYHEAD_HANDLE_RADIUS * 2.0);
    }
}

bool LightTrackScene::IsPlayheadHandle(const QPointF& pos) const
{
    return music_duration_ms > 0
        && playhead_handle != nullptr
        && playhead_handle->rect()
            .adjusted(-4.0, -4.0, 4.0, 4.0)
            .contains(pos);
}

void LightTrackScene::SeekMusicAt(qreal x)
{
    if(music_duration_ms <= 0)
    {
        return;
    }

    const qint64 position_ms = qBound<qint64>(
        0,
        static_cast<qint64>(
            x * 1000.0 / pixels_per_second),
        music_duration_ms);
    SetMusicPosition(position_ms);
    if(music_seek_callback)
    {
        music_seek_callback(position_ms);
    }
}

void LightTrackScene::UpdateClipMove(const QPointF& pos)
{
    if(active_clip == nullptr)
    {
        return;
    }

    const int target_lane = LaneAt(pos);
    if(target_lane < 0)
    {
        HidePreview();
        return;
    }

    const qreal width = active_clip->ClipWidth();
    const qreal x = ClampClipX(
        target_lane,
        SnapClipX(
            pos.x() - drag_offset.x(),
            width,
            active_clip),
        width);
    active_clip->SetLaneIndex(target_lane);
    active_clip->setPos(x, ClipY(target_lane));
    ShowPreview(
        target_lane,
        x,
        width,
        active_clip->Effect());
}

void LightTrackScene::UpdateClipResize(const QPointF& pos)
{
    if(active_clip == nullptr)
    {
        return;
    }

    const int lane = active_clip->LaneIndex();
    qreal x = clip_start_x;
    qreal width = clip_start_width;

    if(drag_mode == ClipResizeLeft)
    {
        const qreal right = clip_start_x + clip_start_width;
        const qreal unsnapped_x = qBound(
            lanes[lane].rect.left(),
            clip_start_x + pos.x() - drag_scene_start.x(),
            right - CLIP_MIN_WIDTH);
        x = qBound(
            lanes[lane].rect.left(),
            SnapEdgeX(unsnapped_x, active_clip),
            right - CLIP_MIN_WIDTH);
        width = right - x;
    }
    else
    {
        const qreal minimum_right =
            clip_start_x + CLIP_MIN_WIDTH;
        const qreal unsnapped_right = qBound(
            minimum_right,
            clip_start_x
                + clip_start_width
                + pos.x()
                - drag_scene_start.x(),
            lanes[lane].rect.right());
        const qreal right = qBound(
            minimum_right,
            SnapEdgeX(unsnapped_right, active_clip),
            lanes[lane].rect.right());
        width = right - clip_start_x;
    }

    active_clip->SetClipWidth(width);
    active_clip->setPos(x, ClipY(lane));
    ShowPreview(lane, x, width, active_clip->Effect());
}

void LightTrackScene::ShowPreview(
    int lane,
    qreal x,
    qreal width,
    const EffectDescriptor& effect)
{
    if(preview == nullptr)
    {
        preview = new ClipPreviewItem();
        addItem(preview);
    }

    preview->SetPreview(width, effect);
    preview->setPos(x, ClipY(lane));
    preview->setVisible(true);
    last_preview_lane = lane;
    last_preview_x = x;
    last_preview_width = width;
    last_preview_effect = effect;
    RefreshInheritedClips();
}

void LightTrackScene::HidePreview()
{
    if(preview != nullptr)
    {
        preview->setVisible(false);
    }

    last_preview_lane = -1;
    last_preview_x = 0.0;
    last_preview_width = CLIP_DEFAULT_WIDTH;
    RefreshInheritedClips();
}

TimelineClipItem* LightTrackScene::AddClip(
    const EffectDescriptor& effect,
    int lane,
    qreal x,
    qreal width,
    bool select,
    bool notify_change,
    ClipId requested_id)
{
    if(lane < 0 || lane >= lanes.size())
    {
        return nullptr;
    }

    const ClipId id = AllocateClipId(requested_id);
    if(!id.IsValid())
    {
        return nullptr;
    }

    TimelineClipItem* clip =
        new TimelineClipItem(id, effect);
    clip->SetLaneIndex(lane);
    clip->SetClipWidth(width);
    if(lanes[lane].rect.isValid()
        && !lanes[lane].rect.isEmpty())
    {
        clip->setPos(
            ClampClipX(lane, x, width),
            ClipY(lane));
    }
    else
    {
        clip->setPos(qMax<qreal>(0.0, x), 0.0);
        clip->setVisible(false);
    }
    addItem(clip);
    clips.push_back(clip);

    if(select)
    {
        clearSelection();
        clip->setSelected(true);
        NotifyClipSelected(clip);
    }

    RefreshInheritedClips();
    if(notify_change)
    {
        NotifyTimelineChanged();
    }
    return clip;
}

qreal LightTrackScene::DefaultClipWidth() const
{
    return qMax(
        CLIP_MIN_WIDTH,
        CLIP_DEFAULT_WIDTH * pixels_per_second / GRID_WIDTH);
}

void LightTrackScene::RemoveClip(TimelineClipItem* clip)
{
    if(active_clip == clip)
    {
        active_clip = nullptr;
        HidePreview();
        drag_mode = NoDrag;
    }

    clips.removeOne(clip);
    if(clip_removed_callback)
    {
        clip_removed_callback(clip->Id());
    }

    removeItem(clip);
    delete clip;
    NotifyClipSelected(nullptr);
    RefreshInheritedClips();
}

void LightTrackScene::NotifyClipSelected(TimelineClipItem* clip)
{
    if(clip_selected_callback)
    {
        clip_selected_callback(
            clip == nullptr
                ? std::optional<ClipId>{}
                : std::optional<ClipId>{clip->Id()});
    }
}

void LightTrackScene::NotifyTimelineChanged()
{
    if(timeline_changed_callback)
    {
        timeline_changed_callback();
    }
}

void LightTrackScene::CancelDrag()
{
    if(active_clip != nullptr)
    {
        active_clip->setOpacity(1.0);
        active_clip->setZValue(35.0);
        active_clip = nullptr;
    }

    HidePreview();
    drag_mode = NoDrag;
}

TimelineRulerWidget::TimelineRulerWidget(QWidget* parent) :
    QWidget(parent)
{
    setFixedHeight(static_cast<int>(RULER_HEIGHT));
}

void TimelineRulerWidget::SetContentWidth(qreal width)
{
    if(qFuzzyCompare(content_width, width))
    {
        return;
    }
    content_width = width;
    update();
}

void TimelineRulerWidget::SetPixelsPerSecond(qreal value)
{
    if(qFuzzyCompare(pixels_per_second, value))
    {
        return;
    }
    pixels_per_second = qBound(
        TIMELINE_ZOOM_MIN,
        value,
        TIMELINE_ZOOM_MAX);
    update();
}

void TimelineRulerWidget::SetScrollOffset(int offset)
{
    if(horizontal_offset == offset)
    {
        return;
    }
    horizontal_offset = offset;
    update();
}

void TimelineRulerWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QColor line_color =
        TextLineColor(palette(), 90, 120);
    const QColor text_color =
        TextLineColor(palette(), 160, 180);
    const qreal label_width =
        qMin<qreal>(LABEL_WIDTH, width());
    const qreal timeline_width =
        qMax<qreal>(0.0, width() - label_width);
    const QRectF frame_rect(
        0.5,
        0.5,
        qMax(0, width() - 1),
        qMax(0, height() - 1));

    painter.setPen(QPen(line_color));
    painter.drawRect(frame_rect);
    painter.drawLine(
        QPointF(label_width - 0.5, 0.0),
        QPointF(label_width - 0.5, height()));

    QFont label_font = painter.font();
    label_font.setBold(true);
    painter.setFont(label_font);
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(
        QRectF(
            8.0,
            0.0,
            qMax<qreal>(0.0, label_width - 16.0),
            height()),
        Qt::AlignVCenter | Qt::AlignLeft,
        QStringLiteral("Device"));

    painter.save();
    painter.setClipRect(
        QRectF(label_width, 0.0, timeline_width, height()));
    painter.setFont(font());

    const qint64 tick_interval_ms =
        TimelineTickIntervalMs(pixels_per_second);
    const qreal tick_width =
        tick_interval_ms * pixels_per_second / 1000.0;
    const qint64 first_tick = qMax<qint64>(
        0,
        static_cast<qint64>(
            std::floor(
                static_cast<qreal>(horizontal_offset)
                / tick_width)));
    const qint64 last_tick = qMin(
        static_cast<qint64>(
            std::floor(content_width / tick_width)),
        static_cast<qint64>(
            std::ceil(
                (horizontal_offset + timeline_width)
                / tick_width)));

    for(qint64 tick = first_tick; tick <= last_tick; tick++)
    {
        const qreal x = tick * tick_width;
        const qreal view_x =
            label_width + x - horizontal_offset;

        painter.setPen(QPen(line_color));
        painter.drawLine(
            QPointF(view_x, 0.0),
            QPointF(view_x, height()));
        painter.setPen(text_color);
        painter.drawText(
            QRectF(
                view_x + 6.0,
                0.0,
                qMax<qreal>(0.0, tick_width - 12.0),
                height()),
            Qt::AlignVCenter | Qt::AlignLeft,
            FormatTimelineTime(
                tick * tick_interval_ms,
                tick_interval_ms));
    }

    painter.restore();
}
}
