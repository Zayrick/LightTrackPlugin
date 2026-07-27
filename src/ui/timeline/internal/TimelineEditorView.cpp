#include "TimelineEditorInternal.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFrame>
#include <QKeyEvent>
#include <QMimeData>
#include <QResizeEvent>
#include <QScrollBar>

#include <utility>

namespace lighttrack::timeline_internal
{
LightTrackView::LightTrackView(QWidget* parent) :
    QGraphicsView(parent),
    light_scene(new LightTrackScene(this))
{
    setScene(light_scene);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setDragMode(QGraphicsView::NoDrag);
    setFrameShape(QFrame::NoFrame);
    setRenderHints(QPainter::TextAntialiasing);
    setCacheMode(QGraphicsView::CacheBackground);
    setViewportUpdateMode(
        QGraphicsView::BoundingRectViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    light_scene->SetPalette(palette());
    light_scene->SetViewportSize(viewport()->size());

    new OverlayScrollBar(this, Qt::Horizontal);
    new OverlayScrollBar(this, Qt::Vertical);

    connect(
        horizontalScrollBar(),
        &QScrollBar::valueChanged,
        this,
        [this](int value)
        {
            if(ruler != nullptr)
            {
                ruler->SetScrollOffset(value);
            }
        });
}

void LightTrackView::SetRuler(TimelineRulerWidget* ruler_widget)
{
    ruler = ruler_widget;
    SyncRuler();
}

void LightTrackView::SetEffects(const QVector<EffectGroup>& groups)
{
    light_scene->SetEffects(groups);
}

void LightTrackView::SetLanes(
    const QVector<TimelineLane>& lanes,
    const QString& empty_text)
{
    light_scene->SetLanes(lanes, empty_text);
    SyncRuler();
}

void LightTrackView::SetVisibleLanes(const QVector<int>& lane_indices)
{
    light_scene->SetVisibleLanes(lane_indices);
    SyncRuler();
}

void LightTrackView::SetMusicSpectrum(
    const QVector<qreal>& spectrum,
    qint64 duration_ms)
{
    light_scene->SetMusicSpectrum(spectrum, duration_ms);
    SyncRuler();
}

void LightTrackView::SetMusicPosition(qint64 position_ms)
{
    light_scene->SetMusicPosition(position_ms);
}

void LightTrackView::SetMinimumTimelineDuration(qint64 duration_ms)
{
    light_scene->SetMinimumContentDuration(duration_ms);
    SyncRuler();
}

QVector<TimelineClip> LightTrackView::TimelineClips() const
{
    return light_scene->TimelineClips();
}

QVector<TimelineClip> LightTrackView::PersistentTimelineClips() const
{
    return light_scene->PersistentTimelineClips();
}

void LightTrackView::ClearTimelineClips()
{
    light_scene->ClearTimelineClips();
}

ClipId LightTrackView::RestoreClip(
    const QString& effect_id,
    int lane,
    qint64 start_ms,
    qint64 end_ms,
    ClipId requested_id)
{
    return light_scene->RestoreClip(
        effect_id,
        lane,
        start_ms,
        end_ms,
        requested_id);
}

void LightTrackView::SetMusicSeekCallback(
    TimelineEditor::MusicSeekCallback callback)
{
    light_scene->SetMusicSeekCallback(std::move(callback));
}

void LightTrackView::SetClipSelectedCallback(
    TimelineEditor::ClipSelectedCallback callback)
{
    light_scene->SetClipSelectedCallback(std::move(callback));
}

void LightTrackView::SetClipRemovedCallback(
    TimelineEditor::ClipRemovedCallback callback)
{
    light_scene->SetClipRemovedCallback(std::move(callback));
}

void LightTrackView::SetTimelineChangedCallback(
    TimelineEditor::TimelineChangedCallback callback)
{
    light_scene->SetTimelineChangedCallback(std::move(callback));
}

void LightTrackView::SetHorizontalZoom(int pixels_per_second)
{
    const QPoint anchor = viewport()->rect().center();
    const qreal old_scene_x = mapToScene(anchor).x();
    const qreal old_pixels_per_second =
        light_scene->PixelsPerSecond();

    if(!light_scene->SetPixelsPerSecond(pixels_per_second))
    {
        return;
    }

    SyncRuler();
    horizontalScrollBar()->setValue(static_cast<int>(
        old_scene_x
            * light_scene->PixelsPerSecond()
            / old_pixels_per_second
        - anchor.x()));
}

void LightTrackView::keyPressEvent(QKeyEvent* event)
{
    if(event->key() == Qt::Key_Delete
        && light_scene->DeleteSelectedClips())
    {
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void LightTrackView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    light_scene->SetViewportSize(viewport()->size());
    SyncRuler();
}

void LightTrackView::changeEvent(QEvent* event)
{
    QGraphicsView::changeEvent(event);
    if(event->type() == QEvent::PaletteChange
        || event->type() == QEvent::ApplicationPaletteChange)
    {
        light_scene->SetPalette(palette());
        SyncRuler();
    }
}

void LightTrackView::dragEnterEvent(QDragEnterEvent* event)
{
    if(event->mimeData()->hasFormat(EFFECT_MIME))
    {
        event->acceptProposedAction();
        return;
    }
    QGraphicsView::dragEnterEvent(event);
}

void LightTrackView::dragMoveEvent(QDragMoveEvent* event)
{
    if(!event->mimeData()->hasFormat(EFFECT_MIME))
    {
        QGraphicsView::dragMoveEvent(event);
        return;
    }

    light_scene->PreviewEffectAt(
        QString::fromUtf8(
            event->mimeData()->data(EFFECT_MIME)),
        mapToScene(EventPosition(event)));
    event->acceptProposedAction();
}

void LightTrackView::dragLeaveEvent(QDragLeaveEvent* event)
{
    light_scene->ClearPreview();
    QGraphicsView::dragLeaveEvent(event);
}

void LightTrackView::dropEvent(QDropEvent* event)
{
    if(!event->mimeData()->hasFormat(EFFECT_MIME))
    {
        QGraphicsView::dropEvent(event);
        return;
    }

    const bool added = light_scene->AddEffectAt(
        QString::fromUtf8(
            event->mimeData()->data(EFFECT_MIME)),
        mapToScene(EventPosition(event)));
    light_scene->ClearPreview();
    if(added)
    {
        event->acceptProposedAction();
    }
}

void LightTrackView::SyncRuler()
{
    if(ruler == nullptr)
    {
        return;
    }

    ruler->SetContentWidth(light_scene->ContentWidth());
    ruler->SetPixelsPerSecond(
        light_scene->PixelsPerSecond());
    ruler->SetScrollOffset(horizontalScrollBar()->value());
}
}
