#include "TimelineEditorInternal.h"

#include <QGraphicsSceneHoverEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

namespace lighttrack::timeline_internal
{
ClipPreviewItem::ClipPreviewItem()
{
    setAcceptedMouseButtons(Qt::NoButton);
    setZValue(20.0);
    setVisible(false);
}

QRectF ClipPreviewItem::boundingRect() const
{
    return QRectF(
        0.0, 0.0, qMax(CLIP_MIN_DISPLAY_WIDTH, width), CLIP_HEIGHT)
        .adjusted(-1.0, -1.0, 1.0, 1.0);
}

void ClipPreviewItem::SetPreview(qreal clip_width, const EffectDescriptor& preview_effect)
{
    prepareGeometryChange();
    width = clip_width;
    effect = preview_effect;
    update();
}

void ClipPreviewItem::paint(
    QPainter* painter,
    const QStyleOptionGraphicsItem*,
    QWidget*)
{
    PaintClipCard(
        painter,
        QRectF(
            0.0, 0.0, qMax(CLIP_MIN_DISPLAY_WIDTH, width), CLIP_HEIGHT),
        effect,
        true);
}

MusicSpectrumItem::MusicSpectrumItem()
{
    setAcceptedMouseButtons(Qt::NoButton);
    setZValue(12.0);
}

QRectF MusicSpectrumItem::boundingRect() const
{
    return QRectF(0.0, 0.0, width, height);
}

void MusicSpectrumItem::SetSpectrum(
    const QVector<qreal>& new_levels,
    qreal new_width,
    qreal new_height)
{
    prepareGeometryChange();
    levels = new_levels;
    width = new_width;
    height = new_height;
    update();
}

void MusicSpectrumItem::paint(
    QPainter* painter,
    const QStyleOptionGraphicsItem*,
    QWidget*)
{
    if(levels.empty() || width <= 0.0 || height <= 0.0)
    {
        return;
    }

    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(64, 188, 255, 92));

    const qreal center_y = height / 2.0;
    const qreal step = width / levels.size();
    const qreal bar_width = qMax<qreal>(1.0, step * 0.72);

    for(int i = 0; i < levels.size(); i++)
    {
        const qreal x = i * step + (step - bar_width) / 2.0;
        const qreal bar_height =
            qMax<qreal>(2.0, levels[i] * height * 0.42);
        painter->drawRect(QRectF(
            x,
            center_y - bar_height,
            bar_width,
            bar_height * 2.0));
    }
}

TimelineClipItem::TimelineClipItem(ClipId id, const EffectDescriptor& effect) :
    id(id),
    effect(effect)
{
    setAcceptHoverEvents(true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setCursor(Qt::OpenHandCursor);
    setZValue(35.0);
}

QRectF TimelineClipItem::boundingRect() const
{
    // Keep very short clips reachable without changing their timeline duration.
    return QRectF(
        0.0, 0.0, qMax(CLIP_MIN_DISPLAY_WIDTH, width), CLIP_HEIGHT);
}

void TimelineClipItem::paint(
    QPainter* painter,
    const QStyleOptionGraphicsItem* option,
    QWidget*)
{
    const bool selected =
        option != nullptr && (option->state & QStyle::State_Selected);
    PaintClipCard(painter, boundingRect(), effect, false, selected);
}

void TimelineClipItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    setCursor(
        IsResizeHandle(event->pos())
            ? Qt::SizeHorCursor
            : Qt::OpenHandCursor);
}

void TimelineClipItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*)
{
    setCursor(Qt::OpenHandCursor);
}

ClipId TimelineClipItem::Id() const
{
    return id;
}

const EffectDescriptor& TimelineClipItem::Effect() const
{
    return effect;
}

int TimelineClipItem::LaneIndex() const
{
    return lane_index;
}

void TimelineClipItem::SetLaneIndex(int index)
{
    lane_index = index;
}

qreal TimelineClipItem::ClipWidth() const
{
    return width;
}

void TimelineClipItem::SetClipWidth(qreal clip_width)
{
    prepareGeometryChange();
    width = clip_width;
    update();
}

bool TimelineClipItem::IsResizeHandle(const QPointF& pos) const
{
    return IsLeftResizeHandle(pos) || IsRightResizeHandle(pos);
}

bool TimelineClipItem::IsLeftResizeHandle(const QPointF& pos) const
{
    const qreal handle_width =
        qMin(RESIZE_HANDLE_WIDTH, boundingRect().width() / 3.0);
    return pos.x() <= handle_width;
}

bool TimelineClipItem::IsRightResizeHandle(const QPointF& pos) const
{
    const qreal display_width = boundingRect().width();
    const qreal handle_width =
        qMin(RESIZE_HANDLE_WIDTH, display_width / 3.0);
    return pos.x() >= display_width - handle_width;
}
}
