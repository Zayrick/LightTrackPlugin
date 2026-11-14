#include "TimelineEditorInternal.h"

#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QPainter>

#include <cmath>

namespace lighttrack::timeline_internal
{
QColor WithAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

QColor TextLineColor(const QPalette& palette, int light_alpha, int dark_alpha)
{
    return WithAlpha(
        palette.color(QPalette::Text),
        palette.color(QPalette::Window).lightness() < 128 ? dark_alpha : light_alpha);
}

qint64 TimelineTickIntervalMs(qreal pixels_per_second)
{
    if(pixels_per_second <= 0.0)
    {
        return 1000;
    }

    const qreal raw_interval_ms =
        TIMELINE_TICK_TARGET_WIDTH * 1000.0 / pixels_per_second;
    const qreal magnitude =
        std::pow(10.0, std::floor(std::log10(raw_interval_ms)));
    const qreal normalized_interval = raw_interval_ms / magnitude;

    qreal multiplier = 1.0;
    if(normalized_interval >= std::sqrt(50.0))
    {
        multiplier = 10.0;
    }
    else if(normalized_interval >= std::sqrt(10.0))
    {
        multiplier = 5.0;
    }
    else if(normalized_interval >= std::sqrt(2.0))
    {
        multiplier = 2.0;
    }

    return qMax<qint64>(1, qRound64(multiplier * magnitude));
}

QString FormatTimelineTime(qint64 time_ms, qint64 tick_interval_ms)
{
    int decimal_places = 0;
    if(tick_interval_ms < 1000)
    {
        decimal_places = tick_interval_ms % 100 == 0
            ? 1
            : (tick_interval_ms % 10 == 0 ? 2 : 3);
    }

    QString value =
        QString::number(static_cast<qreal>(time_ms) / 1000.0, 'f', decimal_places);
    while(value.contains('.') && value.endsWith('0'))
    {
        value.chop(1);
    }
    if(value.endsWith('.'))
    {
        value.chop(1);
    }

    return value + "s";
}

void PaintClipCard(
    QPainter* painter,
    const QRectF& rect,
    const EffectDescriptor& effect,
    bool preview,
    bool selected)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const qreal border_width = selected ? 2.0 : 1.0;
    const QRectF card_rect = rect.adjusted(
        border_width / 2.0,
        border_width / 2.0,
        -border_width / 2.0,
        -border_width / 2.0);
    painter->setBrush(
        WithAlpha(effect.color, preview ? 48 : (selected ? 245 : 230)));
    painter->setPen(QPen(
        selected
            ? WithAlpha(Qt::white, 245)
            : WithAlpha(effect.color.darker(110), preview ? 145 : 220),
        border_width,
        preview ? Qt::DashLine : Qt::SolidLine));
    painter->drawRoundedRect(card_rect, 5.0, 5.0);

    QFont font = painter->font();
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(WithAlpha(Qt::white, preview ? 150 : 255));

    const QRectF text_rect = card_rect.adjusted(9.0, 0.0, -9.0, 0.0);
    const int text_width =
        qMax(0, static_cast<int>(std::floor(text_rect.width())));
    if(text_width > 0)
    {
        painter->drawText(
            text_rect,
            Qt::AlignVCenter | Qt::AlignLeft,
            QFontMetrics(font).elidedText(effect.name, Qt::ElideRight, text_width));
    }
    painter->restore();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
QPoint EventPosition(const QDropEvent* event)
{
    return event->position().toPoint();
}

QPoint EventPosition(const QDragMoveEvent* event)
{
    return event->position().toPoint();
}
#else
QPoint EventPosition(const QDropEvent* event)
{
    return event->pos();
}

QPoint EventPosition(const QDragMoveEvent* event)
{
    return event->pos();
}
#endif

}
