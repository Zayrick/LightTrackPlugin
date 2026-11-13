#include "TimelineEditorInternal.h"

#include "integrations/openrgb/PerformanceStats.h"

#include <QAction>
#include <QCursor>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFrame>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMimeData>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScrollBar>

#include <utility>

namespace lighttrack::timeline_internal
{
namespace
{
QPoint MousePosition(const QMouseEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}
}

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

    page_turn_animation = new QPropertyAnimation(
        horizontalScrollBar(),
        "value",
        this);
    page_turn_animation->setDuration(PAGE_TURN_DURATION_MS);
    page_turn_animation->setEasingCurve(QEasingCurve::InOutCubic);

    connect(
        light_scene,
        &QGraphicsScene::sceneRectChanged,
        this,
        [this](const QRectF&) { SyncRuler(); });

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
    effect_groups = groups;
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

void LightTrackView::SetMusicPeaks(const WaveformPeaks& peaks)
{
    light_scene->SetMusicPeaks(peaks);
}

void LightTrackView::SetWaveformStyle(WaveformStyle style)
{
    light_scene->SetWaveformStyle(style);
}

void LightTrackView::SetMusicPosition(
    qint64 position_ms,
    bool turn_page)
{
    light_scene->SetMusicPosition(position_ms);
    if(!follow_playhead)
    {
        return;
    }

    if(position_ms <= 0)
    {
        page_turn_animation->stop();
        horizontalScrollBar()->setValue(
            horizontalScrollBar()->minimum());
        return;
    }

    if(turn_page)
    {
        MaybeTurnPlaybackPage(position_ms);
    }
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
    ClipId requested_id,
    quint64 group_id)
{
    return light_scene->RestoreClip(
        effect_id,
        lane,
        start_ms,
        end_ms,
        requested_id,
        group_id);
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

void LightTrackView::SetClipActivatedCallback(
    TimelineEditor::ClipActivatedCallback callback)
{
    light_scene->SetClipActivatedCallback(std::move(callback));
}

void LightTrackView::BeginPlacement(const QString& effect_id)
{
    placing_effect_id = effect_id;
    viewport()->setMouseTracking(true);
    viewport()->setCursor(Qt::CrossCursor);
    setFocus(Qt::OtherFocusReason);
    const QPoint position = viewport()->mapFromGlobal(QCursor::pos());
    if(viewport()->rect().contains(position))
    {
        light_scene->PreviewEffectAt(
            placing_effect_id,
            mapToScene(position));
    }
}

void LightTrackView::CancelPlacement()
{
    if(placing_effect_id.isEmpty())
    {
        return;
    }
    placing_effect_id.clear();
    light_scene->ClearPreview();
    viewport()->unsetCursor();
}

void LightTrackView::mousePressEvent(QMouseEvent* event)
{
    if(!IsPlacing())
    {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    swallow_release = true;
    if(event->button() == Qt::LeftButton)
    {
        const QString effect_id = placing_effect_id;
        const bool added = light_scene->AddEffectAt(
            effect_id,
            mapToScene(MousePosition(event)));
        // Hold Shift to keep placing the same effect.
        if(added && !(event->modifiers() & Qt::ShiftModifier))
        {
            CancelPlacement();
        }
    }
    else
    {
        // The context menu that follows this right-click is skipped.
        cancelled_by_right_click = event->button() == Qt::RightButton;
        CancelPlacement();
    }
    event->accept();
}

void LightTrackView::mouseMoveEvent(QMouseEvent* event)
{
    if(IsPlacing())
    {
        light_scene->PreviewEffectAt(
            placing_effect_id,
            mapToScene(MousePosition(event)));
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void LightTrackView::mouseReleaseEvent(QMouseEvent* event)
{
    if(swallow_release)
    {
        swallow_release = false;
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void LightTrackView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if(IsPlacing())
    {
        mousePressEvent(event);
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void LightTrackView::paintEvent(QPaintEvent* event)
{
    namespace perf = lighttrack::openrgb::performance;
    const perf::Clock::time_point start = perf::Clock::now();
    QGraphicsView::paintEvent(event);
    perf::RecordUi("timeline paint", perf::MsSince(start));
}

void LightTrackView::leaveEvent(QEvent* event)
{
    if(IsPlacing())
    {
        light_scene->ClearPreview();
    }
    QGraphicsView::leaveEvent(event);
}

void PopulateEffectMenu(
    QMenu* menu,
    const QVector<EffectGroup>& groups,
    const std::function<void(const QString&)>& on_chosen)
{
    for(const EffectGroup& group : groups)
    {
        if(group.effects.isEmpty())
        {
            continue;
        }
        AddEffectActions(menu->addMenu(group.name), group, on_chosen);
    }
}

void AddEffectActions(
    QMenu* category,
    const EffectGroup& group,
    const std::function<void(const QString&)>& on_chosen)
{
    {
        for(const EffectDescriptor& effect : group.effects)
        {
            QPixmap swatch(12, 12);
            swatch.fill(Qt::transparent);
            {
                QPainter painter(&swatch);
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setPen(Qt::NoPen);
                painter.setBrush(effect.color);
                painter.drawEllipse(QRectF(1.0, 1.0, 10.0, 10.0));
            }
            QAction* action =
                category->addAction(QIcon(swatch), effect.name);
            const QString effect_id = effect.id;
            QObject::connect(
                action,
                &QAction::triggered,
                category,
                [on_chosen, effect_id]() { on_chosen(effect_id); });
        }
    }
}

void LightTrackView::SetClipRemovedCallback(
    TimelineEditor::ClipRemovedCallback callback)
{
    light_scene->SetClipRemovedCallback(std::move(callback));
}

void LightTrackView::SetClipDuplicatedCallback(
    TimelineEditor::ClipDuplicatedCallback callback)
{
    light_scene->SetClipDuplicatedCallback(std::move(callback));
}

void LightTrackView::SetTimelineChangedCallback(
    TimelineEditor::TimelineChangedCallback callback)
{
    light_scene->SetTimelineChangedCallback(std::move(callback));
}

void LightTrackView::SetSnappingEnabled(bool enabled)
{
    light_scene->SetSnappingEnabled(enabled);
}

void LightTrackView::SetHorizontalZoom(int pixels_per_second)
{
    page_turn_animation->stop();
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

void LightTrackView::MaybeTurnPlaybackPage(qint64 position_ms)
{
    QScrollBar* scroll_bar = horizontalScrollBar();
    if(page_turn_animation->state() == QAbstractAnimation::Running
        || scroll_bar->maximum() <= scroll_bar->minimum())
    {
        return;
    }

    const int viewport_width = viewport()->width();
    if(viewport_width <= 0)
    {
        return;
    }

    const qreal playhead_scene_x = qBound<qreal>(
        0.0,
        position_ms * light_scene->PixelsPerSecond() / 1000.0,
        light_scene->ContentWidth());
    const qreal playhead_view_x =
        mapFromScene(QPointF(playhead_scene_x, 0.0)).x();
    if(playhead_view_x
        < viewport_width * PAGE_TURN_TRIGGER_RATIO)
    {
        return;
    }

    const int current_value = scroll_bar->value();
    const int target_value = qBound(
        scroll_bar->minimum(),
        current_value + qRound(
            playhead_view_x
            - viewport_width * PAGE_TURN_LANDING_RATIO),
        scroll_bar->maximum());
    if(target_value <= current_value)
    {
        return;
    }

    page_turn_animation->setStartValue(current_value);
    page_turn_animation->setEndValue(target_value);
    page_turn_animation->start();
}

void LightTrackView::keyPressEvent(QKeyEvent* event)
{
    if(event->key() == Qt::Key_Escape && IsPlacing())
    {
        CancelPlacement();
        event->accept();
        return;
    }
    // Ctrl+G groups the selected clips, Ctrl+Shift+G ungroups them.
    if(event->key() == Qt::Key_G
        && (event->modifiers() & Qt::ControlModifier))
    {
        const bool done = (event->modifiers() & Qt::ShiftModifier)
            ? light_scene->UngroupSelectedClips()
            : light_scene->GroupSelectedClips();
        if(done)
        {
            event->accept();
            return;
        }
    }
    if(event->matches(QKeySequence::Copy)
        && light_scene->CopySelectedClips())
    {
        event->accept();
        return;
    }
    if(event->matches(QKeySequence::Paste)
        && light_scene->PasteCopiedClips())
    {
        event->accept();
        return;
    }
    if(event->key() == Qt::Key_Delete
        && light_scene->DeleteSelectedClips())
    {
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void LightTrackView::contextMenuEvent(QContextMenuEvent* event)
{
    if(cancelled_by_right_click || IsPlacing())
    {
        cancelled_by_right_click = false;
        CancelPlacement();
        event->accept();
        return;
    }
    light_scene->SelectClipAt(mapToScene(event->pos()));

    QMenu menu(this);
    QAction* copy_action =
        menu.addAction(QStringLiteral("Copy"));
    copy_action->setShortcut(QKeySequence::Copy);
    copy_action->setEnabled(light_scene->HasSelectedClips());

    QAction* paste_action =
        menu.addAction(QStringLiteral("Paste"));
    paste_action->setShortcut(QKeySequence::Paste);
    paste_action->setEnabled(
        light_scene->CanPasteCopiedClips());

    QAction* group_action =
        menu.addAction(QStringLiteral("Group"));
    group_action->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    group_action->setEnabled(light_scene->CanGroupSelection());
    QAction* ungroup_action =
        menu.addAction(QStringLiteral("Ungroup"));
    ungroup_action->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+G")));
    ungroup_action->setEnabled(light_scene->CanUngroupSelection());
    QObject::connect(group_action, &QAction::triggered, this,
        [this]() { light_scene->GroupSelectedClips(); });
    QObject::connect(ungroup_action, &QAction::triggered, this,
        [this]() { light_scene->UngroupSelectedClips(); });
    menu.addSeparator();

    // Add an effect exactly where the user right-clicked.
    const QPointF click_scene_pos = mapToScene(event->pos());
    QMenu* add_menu = menu.addMenu(QStringLiteral("Add effect"));
    PopulateEffectMenu(
        add_menu,
        effect_groups,
        [this, click_scene_pos](const QString& effect_id)
        {
            light_scene->AddEffectAt(effect_id, click_scene_pos);
        });

    menu.addSeparator();
    QAction* delete_action =
        menu.addAction(QStringLiteral("Delete"));
    delete_action->setShortcut(QKeySequence::Delete);
    delete_action->setEnabled(light_scene->HasSelectedClips());

    QAction* selected_action = menu.exec(event->globalPos());
    if(selected_action == copy_action)
    {
        light_scene->CopySelectedClips();
    }
    else if(selected_action == paste_action)
    {
        light_scene->PasteCopiedClips();
    }
    else if(selected_action == delete_action)
    {
        light_scene->DeleteSelectedClips();
    }
    event->accept();
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
