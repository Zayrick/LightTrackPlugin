#include "TimelineEditorInternal.h"

#include <QGraphicsSceneHoverEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPolygonF>
#include <QStyleOptionGraphicsItem>

#include <cmath>

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
    // Paint only the exposed part; the item spans the whole song.
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
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

void MusicSpectrumItem::SetPeaks(const WaveformPeaks& new_peaks)
{
    peaks = new_peaks;
    update();
}

void MusicSpectrumItem::SetStyle(WaveformStyle new_style)
{
    if(style == new_style)
    {
        return;
    }
    style = new_style;
    update();
}

void MusicSpectrumItem::SetPlayedWidth(qreal played_width)
{
    const qreal clamped = qBound<qreal>(0.0, played_width, width);
    if(qFuzzyCompare(played + 1.0, clamped + 1.0))
    {
        return;
    }
    const qreal previous = played;
    played = clamped;
    if(style == WaveformStyle::Progress)
    {
        // Only the strip between the old and new playhead changes.
        const qreal left = qMin(previous, played) - 4.0;
        const qreal right = qMax(previous, played) + 4.0;
        update(QRectF(left, 0.0, right - left, height));
    }
}

void MusicSpectrumItem::MinMax(
    int channel,
    qreal x0,
    qreal x1,
    qreal& low,
    qreal& high) const
{
    low = 0.0;
    high = 0.0;
    if(width <= 0.0)
    {
        return;
    }

    if(!peaks.IsEmpty())
    {
        const int count = peaks.BucketCount();
        const qreal scale = count / width;
        const int first = qBound(0, static_cast<int>(x0 * scale), count - 1);
        const int last = qBound(first, static_cast<int>(x1 * scale), count - 1);
        const QVector<float>& lows = peaks.min[channel];
        const QVector<float>& highs = peaks.max[channel];
        low = lows[first];
        high = highs[first];
        for(int index = first + 1; index <= last; ++index)
        {
            low = qMin<qreal>(low, lows[index]);
            high = qMax<qreal>(high, highs[index]);
        }
        return;
    }

    // Fallback: symmetric envelope from the mono levels.
    const qreal level = Peak(channel, x0, x1);
    low = -level;
    high = level;
}

qreal MusicSpectrumItem::Peak(int channel, qreal x0, qreal x1) const
{
    if(!peaks.IsEmpty())
    {
        qreal low = 0.0;
        qreal high = 0.0;
        MinMax(channel, x0, x1, low, high);
        return qMax(-low, high);
    }

    const int count = levels.size();
    if(count == 0 || width <= 0.0)
    {
        return 0.0;
    }

    const qreal scale = count / width;
    const qreal first = x0 * scale;
    const qreal last = x1 * scale;
    if(last - first < 1.0)
    {
        // Zoomed in: interpolate between neighbouring levels so the shape
        // stays smooth instead of stepping.
        const qreal position = qBound<qreal>(
            0.0,
            (first + last) / 2.0 - 0.5,
            count - 1.0);
        const int index = static_cast<int>(position);
        const int next = qMin(index + 1, count - 1);
        const qreal t = position - index;
        return levels[index] * (1.0 - t) + levels[next] * t;
    }

    // Zoomed out: keep the loudest level so peaks do not vanish.
    const int begin = qBound(0, static_cast<int>(first), count - 1);
    const int end = qBound(begin, static_cast<int>(last), count - 1);
    qreal peak = 0.0;
    for(int index = begin; index <= end; ++index)
    {
        peak = qMax(peak, levels[index]);
    }
    return peak;
}

void MusicSpectrumItem::PaintStudio(
    QPainter* painter,
    const Band& band,
    qreal x0,
    qreal x1) const
{
    // DAW style: a colored band with the real waveform (lowest to highest
    // sample of every pixel column) drawn over it.
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->fillRect(
        QRectF(x0, band.top, x1 - x0, band.height),
        QColor(40, 104, 58, 210));

    const qreal center = band.Center();
    const qreal amplitude = band.Amplitude();
    painter->setPen(QPen(QColor(108, 226, 132), 1.0));
    const int first = static_cast<int>(std::floor(x0));
    const int last = static_cast<int>(std::ceil(qMin(x1, width)));
    for(int x = first; x < last; ++x)
    {
        qreal low = 0.0;
        qreal high = 0.0;
        MinMax(band.channel, x, x + 1.0, low, high);
        const qreal top = center - high * amplitude;
        const qreal bottom = center - low * amplitude;
        painter->drawLine(
            QPointF(x + 0.5, top),
            QPointF(x + 0.5, qMax(bottom, top + 1.0)));
    }
}

void MusicSpectrumItem::PaintSmooth(
    QPainter* painter,
    const Band& band,
    qreal x0,
    qreal x1,
    bool fill) const
{
    constexpr qreal step = 2.0;
    const qreal center_y = band.Center();
    const qreal amplitude = band.Amplitude();

    QPolygonF top;
    QPolygonF bottom;
    for(qreal x = x0; x <= x1 + step; x += step)
    {
        const qreal clamped_x = qMin(x, width);
        const qreal level = qMax<qreal>(
            0.02,
            Peak(band.channel, clamped_x - step / 2.0, clamped_x + step / 2.0));
        top.append(QPointF(clamped_x, center_y - level * amplitude));
        bottom.append(QPointF(clamped_x, center_y + level * amplitude));
        if(clamped_x >= width)
        {
            break;
        }
    }

    painter->setRenderHint(QPainter::Antialiasing, true);
    if(fill)
    {
        QPolygonF shape = top;
        for(int index = bottom.size() - 1; index >= 0; --index)
        {
            shape.append(bottom[index]);
        }
        QLinearGradient gradient(0.0, band.top, 0.0, band.top + band.height);
        gradient.setColorAt(0.0, QColor(64, 188, 255, 120));
        gradient.setColorAt(0.5, QColor(64, 188, 255, 45));
        gradient.setColorAt(1.0, QColor(64, 188, 255, 120));
        painter->setPen(Qt::NoPen);
        painter->setBrush(gradient);
        painter->drawPolygon(shape);
    }
    else
    {
        // Faint center line so quiet passages are still visible.
        painter->setPen(QPen(QColor(64, 188, 255, 60), 1.0));
        painter->drawLine(
            QPointF(x0, center_y),
            QPointF(qMin(x1, width), center_y));
    }

    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(
        QColor(110, 205, 255, fill ? 200 : 230),
        fill ? 1.0 : 1.4));
    painter->drawPolyline(top);
    painter->drawPolyline(bottom);
}

void MusicSpectrumItem::PaintBars(
    QPainter* painter,
    const Band& band,
    qreal x0,
    qreal x1,
    bool progress) const
{
    // Fixed on-screen pitch so bars never turn into blocks when zoomed.
    constexpr qreal pitch = 4.0;
    constexpr qreal bar_width = 2.0;
    const qreal center_y = band.Center();
    const qreal amplitude = band.Amplitude();

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    const QColor played_color(64, 188, 255, 230);
    const QColor bar_color(64, 188, 255, 170);
    const QColor unplayed_color(150, 170, 190, 80);

    const int first = qMax(0, static_cast<int>(x0 / pitch) - 1);
    const int last = static_cast<int>(qMin(x1, width) / pitch) + 1;
    for(int index = first; index <= last; ++index)
    {
        const qreal left = index * pitch;
        if(left >= width)
        {
            break;
        }
        const qreal level = qMax<qreal>(
            0.04,
            Peak(band.channel, left, left + pitch));
        const qreal half_height = qMax<qreal>(1.0, level * amplitude);
        painter->setBrush(
            !progress
                ? bar_color
                : (left + bar_width / 2.0 <= played
                    ? played_color
                    : unplayed_color));
        painter->drawRoundedRect(
            QRectF(
                left + (pitch - bar_width) / 2.0,
                center_y - half_height,
                bar_width,
                half_height * 2.0),
            bar_width / 2.0,
            bar_width / 2.0);
    }
}

void MusicSpectrumItem::PaintClassic(
    QPainter* painter,
    const Band& band,
    qreal x0,
    qreal x1) const
{
    if(levels.isEmpty())
    {
        return;
    }
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(64, 188, 255, 92));

    const qreal center_y = band.Center();
    const qreal step = width / levels.size();
    const qreal bar_width = qMax<qreal>(1.0, step * 0.72);
    const int first = qMax(0, static_cast<int>(x0 / step) - 1);
    const int last = qMin(
        static_cast<int>(levels.size()) - 1,
        static_cast<int>(x1 / step) + 1);

    for(int i = first; i <= last; i++)
    {
        const qreal x = i * step + (step - bar_width) / 2.0;
        const qreal bar_height =
            qMax<qreal>(1.0, levels[i] * band.Amplitude());
        painter->drawRect(QRectF(
            x,
            center_y - bar_height,
            bar_width,
            bar_height * 2.0));
    }
}

void MusicSpectrumItem::paint(
    QPainter* painter,
    const QStyleOptionGraphicsItem* option,
    QWidget*)
{
    if((levels.empty() && peaks.IsEmpty()) || width <= 0.0 || height <= 0.0)
    {
        return;
    }

    const QRectF exposed = option != nullptr
        && !option->exposedRect.isEmpty()
        ? option->exposedRect
        : boundingRect();
    const qreal x0 = qMax<qreal>(0.0, exposed.left());
    const qreal x1 = qMin(width, exposed.right());
    if(x1 <= x0)
    {
        return;
    }

    painter->save();
    // Left channel on top, right channel below.
    constexpr qreal gap = 1.0;
    const qreal band_height = (height - gap) / 2.0;
    for(int channel = 0; channel < 2; ++channel)
    {
        Band band;
        band.channel = channel;
        band.top = channel * (band_height + gap);
        band.height = band_height;
        switch(style)
        {
        case WaveformStyle::Studio:
            PaintStudio(painter, band, x0, x1);
            break;
        case WaveformStyle::Smooth:
            PaintSmooth(painter, band, x0, x1, true);
            break;
        case WaveformStyle::Line:
            PaintSmooth(painter, band, x0, x1, false);
            break;
        case WaveformStyle::Bars:
            PaintBars(painter, band, x0, x1, false);
            break;
        case WaveformStyle::Progress:
            PaintBars(painter, band, x0, x1, true);
            break;
        case WaveformStyle::Classic:
            PaintClassic(painter, band, x0, x1);
            break;
        }
    }
    // Divider between the channels.
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QPen(QColor(0, 0, 0, 110), gap));
    painter->drawLine(
        QPointF(x0, band_height + gap / 2.0),
        QPointF(x1, band_height + gap / 2.0));
    painter->restore();
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

    if(group_id != 0)
    {
        // Link icon in the top-right corner marks grouped clips.
        const QRectF card = boundingRect();
        if(card.width() >= 22.0)
        {
            painter->save();
            QFont icon_font(lighttrack::ui::LucideFontFamily());
            icon_font.setPixelSize(11);
            painter->setFont(icon_font);
            painter->setPen(QColor(255, 255, 255, 220));
            painter->drawText(
                QRectF(card.right() - 16.0, card.top() + 2.0, 14.0, 14.0),
                Qt::AlignCenter,
                QString(QChar(0xE102)));
            painter->restore();
        }
    }
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
