#include "LightTrackPlugin.h"

#include "LightTrackEffectCatalog.h"
#include "LightTrackOpenRGBEffectsBridge.h"
#include "ResourceManagerInterface.h"
#include "RGBController/RGBController.h"
#include "ControllerZone.h"
#include "ColorUtils.h"
#include "EffectListManager.h"
#include "EffectManager.h"
#include "OpenRGBEffectSettings.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QCursor>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QDir>
#include <QGraphicsItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMetaObject>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSize>
#include <QSizePolicy>
#include <QSlider>
#include <QStyleOptionGraphicsItem>
#include <QTimer>
#include <QTransform>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "miniaudio.h"

namespace
{
const char* EFFECT_MIME = "application/x-lighttrack-effect";

const qreal PAGE_MARGIN = 10.0;
const qreal GAP = 8.0;
const qreal TOOLBAR_HEIGHT = 36.0;
const qreal TOOLBAR_ICON_SIZE = 20.0;
const qreal HEADER_HEIGHT = 42.0;
const qreal RULER_HEIGHT = 30.0;
const qreal LABEL_WIDTH = 238.0;
const qreal ROW_HEIGHT = 36.0;
const qreal TIMELINE_MIN_WIDTH = 900.0;
const qreal SIDE_PANEL_WIDTH = 178.0;
const qreal EFFECT_ROW_HEIGHT = 28.0;
const qreal CLIP_HEIGHT = 32.0;
const qreal CLIP_MIN_WIDTH = 72.0;
const qreal CLIP_DEFAULT_WIDTH = 130.0;
const qreal GRID_WIDTH = 80.0;
const qreal TIMELINE_ZOOM_MIN = 40.0;
const qreal TIMELINE_ZOOM_MAX = 240.0;
const qreal RESIZE_HANDLE_WIDTH = 9.0;
const qreal CLIP_DRAG_HANDLE_WIDTH = 22.0;
const qreal REMOVE_BUTTON_SIZE = 18.0;
const qreal REMOVE_BUTTON_RIGHT_MARGIN = RESIZE_HANDLE_WIDTH + 8.0;
const qreal PLAYHEAD_HANDLE_RADIUS = 6.0;
const int MUSIC_SPECTRUM_BARS = 1200;

struct EffectDefinition
{
    QString id;
    QString name;
    QString category;
    QColor color;
};

struct LaneInfo
{
    QString name;
    QRectF rect;
    int level = 0;
};

enum EffectListRole
{
    EffectIdRole = Qt::UserRole + 1,
    LaneIndexRole
};

class TimelineClipItem;

struct LaneEntry
{
    QString name;
    int level = 0;
    RGBController* controller = nullptr;
    int zone_index = -1;
    int segment_index = -1;
};

struct TimelineClipState
{
    const TimelineClipItem* clip = nullptr;
    EffectDefinition effect;
    int lane = -1;
    qint64 start_ms = 0;
    qint64 end_ms = 0;
};

struct RuntimeTarget
{
    int lane = -1;
    ControllerZone* zone = nullptr;
};

QVector<EffectDefinition> EffectDefinitions()
{
    QVector<EffectDefinition> effects;
    const QVector<LightTrackEffectGroup> groups = LoadOpenRGBEffectsCatalog();

    for(const LightTrackEffectGroup& group : groups)
    {
        for(const LightTrackEffectInfo& effect : group.effects)
        {
            effects.push_back({effect.id, effect.name, group.name, effect.color});
        }
    }

    return effects;
}

QVector<LightTrackEffectGroup> EffectGroups()
{
    return LoadOpenRGBEffectsCatalog();
}

QColor WithAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

QColor TextLineColor(const QPalette& palette, int light_alpha, int dark_alpha)
{
    return WithAlpha(palette.color(QPalette::Text), palette.color(QPalette::Window).lightness() < 128 ? dark_alpha : light_alpha);
}

QVector<qreal> BuildMusicSpectrumPreview(const QString& path)
{
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 0);
    ma_decoder decoder;

#ifdef _WIN32
    const std::wstring decoder_path = QDir::toNativeSeparators(path).toStdWString();
    const ma_result init_result = ma_decoder_init_file_w(decoder_path.c_str(), &config, &decoder);
#else
    const QByteArray decoder_path = path.toLocal8Bit();
    const ma_result init_result = ma_decoder_init_file(decoder_path.constData(), &config, &decoder);
#endif

    if(init_result != MA_SUCCESS)
    {
        return {};
    }

    QVector<float> samples;
    QVector<float> chunk(4096);

    // ponytail: one-shot decode is enough for normal songs; stream bins if very long tracks matter.
    for(;;)
    {
        ma_uint64 frames_read = 0;
        const ma_result read_result = ma_decoder_read_pcm_frames(&decoder, chunk.data(), static_cast<ma_uint64>(chunk.size()), &frames_read);

        if(frames_read > 0)
        {
            const int read_count = static_cast<int>(frames_read);
            const int old_size = samples.size();
            samples.resize(old_size + read_count);
            std::copy(chunk.constData(), chunk.constData() + read_count, samples.data() + old_size);
        }

        if(frames_read == 0 || read_result != MA_SUCCESS)
        {
            break;
        }
    }

    ma_decoder_uninit(&decoder);

    if(samples.isEmpty())
    {
        return {};
    }

    QVector<qreal> levels;
    levels.reserve(MUSIC_SPECTRUM_BARS);
    qreal peak = 0.0;
    const qint64 sample_count = samples.size();

    for(int i = 0; i < MUSIC_SPECTRUM_BARS; i++)
    {
        const qint64 start = sample_count * i / MUSIC_SPECTRUM_BARS;
        const qint64 end = sample_count * (i + 1) / MUSIC_SPECTRUM_BARS;

        if(end <= start)
        {
            levels.push_back(0.0);
            continue;
        }

        qreal sum = 0.0;
        for(qint64 sample_idx = start; sample_idx < end; sample_idx++)
        {
            const qreal sample = samples[static_cast<int>(sample_idx)];
            sum += sample * sample;
        }

        const qreal level = std::sqrt(sum / (end - start));
        levels.push_back(level);
        peak = qMax(peak, level);
    }

    if(peak > 0.0)
    {
        for(qreal& level : levels)
        {
            level = qBound<qreal>(0.02, level / peak, 1.0);
        }
    }

    return levels;
}

bool FindEffect(const QString& id, EffectDefinition* effect)
{
    LightTrackEffectInfo exported_effect;
    if(!FindOpenRGBEffect(id, &exported_effect))
    {
        return false;
    }

    if(effect != nullptr)
    {
        *effect = {exported_effect.id, exported_effect.name, exported_effect.category, exported_effect.color};
    }

    return true;
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

QLabel* HeaderLabel(const QString& text, QWidget* parent)
{
    QLabel* label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    label->setContentsMargins(8, 0, 8, 0);

    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);

    return label;
}

class OverlayScrollBar : public QWidget
{
public:
    explicit OverlayScrollBar(QAbstractScrollArea* scroll_area, Qt::Orientation orientation) :
        // Keep the overlay outside the viewport so scrollContentsBy() cannot move it with the content.
        QWidget(scroll_area),
        scroll_area(scroll_area),
        scroll_bar(orientation == Qt::Horizontal ? scroll_area->horizontalScrollBar() : scroll_area->verticalScrollBar()),
        orientation(orientation)
    {
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_StyledBackground, false);
        setAutoFillBackground(false);
        setFocusPolicy(Qt::NoFocus);
        setMouseTracking(true);
        hide();

        QWidget* scroll_viewport = scroll_area->viewport();
        scroll_viewport->setMouseTracking(true);
        scroll_viewport->installEventFilter(this);

        if(orientation == Qt::Horizontal)
        {
            scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
        else
        {
            scroll_area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }

        fade_timer.setInterval(16);
        connect(&fade_timer, &QTimer::timeout, this, [this]()
        {
            const qreal step = target_opacity > opacity ? 0.18 : -0.12;
            opacity = qBound<qreal>(0.0, opacity + step, 1.0);

            if((step > 0.0 && opacity >= target_opacity) || (step < 0.0 && opacity <= target_opacity))
            {
                opacity = target_opacity;
                fade_timer.stop();
            }

            update();

            if(opacity <= 0.0 && target_opacity <= 0.0)
            {
                hide();
            }
        });

        hide_timer.setSingleShot(true);
        hide_timer.setInterval(HIDE_DELAY_MS);
        connect(&hide_timer, &QTimer::timeout, this, [this]()
        {
            if(!pointer_near && !dragging)
            {
                FadeTo(0.0);
            }
        });

        connect(scroll_bar, &QScrollBar::rangeChanged, this, [this](int, int)
        {
            if(!IsScrollable())
            {
                pointer_near = false;
                hide_timer.stop();
                opacity = 0.0;
                target_opacity = 0.0;
                fade_timer.stop();
                hide();
            }

            update();
        });
        connect(scroll_bar, &QScrollBar::valueChanged, this, [this](int)
        {
            update();
            RevealTemporarily();
        });

        UpdateGeometry();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if(watched == scroll_area->viewport())
        {
            switch(event->type())
            {
                case QEvent::Resize:
                case QEvent::Move:
                case QEvent::Show:
                    UpdateGeometry();
                    break;

                case QEvent::MouseMove:
                {
                    const QMouseEvent* mouse_event = static_cast<QMouseEvent*>(event);
                    UpdatePointerProximity(mouse_event->pos());
                    break;
                }

                case QEvent::Enter:
                case QEvent::Leave:
                    UpdatePointerProximityFromCursor();
                    break;

                case QEvent::Wheel:
                    RevealTemporarily();
                    break;

                default:
                    break;
            }
        }

        return QWidget::eventFilter(watched, event);
    }

    bool event(QEvent* event) override
    {
        if(event->type() == QEvent::Enter || event->type() == QEvent::Leave)
        {
            UpdatePointerProximityFromCursor();
        }

        return QWidget::event(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        if(!IsScrollable() || opacity <= 0.0)
        {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);

        QColor thumb_color = palette().color(QPalette::Text);
        thumb_color.setAlpha(qRound(153 * opacity));
        painter.setBrush(thumb_color);

        const QRectF thumb = ThumbRect();
        painter.drawRoundedRect(thumb, THUMB_WIDTH / 2.0, THUMB_WIDTH / 2.0);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if(event->button() != Qt::LeftButton || !IsScrollable())
        {
            QWidget::mousePressEvent(event);
            return;
        }

        const qreal pointer_position = AxisPosition(event->pos());
        const QRectF thumb = ThumbRect();
        const qreal thumb_start = orientation == Qt::Horizontal ? thumb.left() : thumb.top();
        const qreal thumb_length = orientation == Qt::Horizontal ? thumb.width() : thumb.height();

        dragging = true;
        hide_timer.stop();
        Reveal();

        if(thumb.contains(event->pos()))
        {
            drag_offset = pointer_position - thumb_start;
        }
        else
        {
            drag_offset = thumb_length / 2.0;
            SetValueFromThumbPosition(pointer_position - drag_offset);
        }

        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if(dragging)
        {
            SetValueFromThumbPosition(AxisPosition(event->pos()) - drag_offset);
            event->accept();
            return;
        }

        UpdatePointerProximityFromCursor();
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if(event->button() == Qt::LeftButton && dragging)
        {
            dragging = false;
            UpdatePointerProximityFromCursor();

            if(!pointer_near)
            {
                hide_timer.start();
            }

            event->accept();
            return;
        }

        QWidget::mouseReleaseEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override
    {
        RevealTemporarily();

        const QPoint pixel_delta = event->pixelDelta();
        const QPoint angle_delta = event->angleDelta();
        int delta = orientation == Qt::Horizontal ? pixel_delta.x() : pixel_delta.y();

        if(delta == 0)
        {
            delta = orientation == Qt::Horizontal ? pixel_delta.y() : pixel_delta.x();
        }

        if(delta != 0)
        {
            wheel_remainder += delta;
        }
        else
        {
            delta = orientation == Qt::Horizontal ? angle_delta.x() : angle_delta.y();
            if(delta == 0)
            {
                delta = orientation == Qt::Horizontal ? angle_delta.y() : angle_delta.x();
            }

            const int scroll_lines = qMax(1, QApplication::wheelScrollLines());
            const int single_step = qMax(1, scroll_bar->singleStep());
            wheel_remainder += static_cast<qreal>(delta) * scroll_lines * single_step / 120.0;
        }

        const int whole_delta = static_cast<int>(wheel_remainder);
        if(whole_delta != 0)
        {
            scroll_bar->setValue(scroll_bar->value() - whole_delta);
            wheel_remainder -= whole_delta;
        }

        event->accept();
    }

private:
    static const int HIT_WIDTH = 14;
    static const int THUMB_WIDTH = 5;
    static const int EDGE_MARGIN = 3;
    static const int TRACK_PADDING = 2;
    static const int MIN_THUMB_LENGTH = 28;
    static const int REVEAL_DISTANCE = 26;
    static const int HIDE_DELAY_MS = 650;

    bool IsScrollable() const
    {
        return scroll_bar->maximum() > scroll_bar->minimum();
    }

    qreal AxisPosition(const QPoint& point) const
    {
        return orientation == Qt::Horizontal ? point.x() : point.y();
    }

    qreal TrackLength() const
    {
        return qMax<qreal>(0.0, (orientation == Qt::Horizontal ? width() : height()) - 2.0 * TRACK_PADDING);
    }

    qreal ThumbLength() const
    {
        const qreal track_length = TrackLength();
        const qreal range = scroll_bar->maximum() - scroll_bar->minimum();
        const qreal page_step = qMax(1, scroll_bar->pageStep());
        const qreal content_length = range + page_step;

        if(track_length <= 0.0 || content_length <= 0.0)
        {
            return 0.0;
        }

        return qMin(track_length, qMax<qreal>(MIN_THUMB_LENGTH, track_length * page_step / content_length));
    }

    QRectF ThumbRect() const
    {
        const qreal track_length = TrackLength();
        const qreal thumb_length = ThumbLength();
        const qreal movable_length = qMax<qreal>(0.0, track_length - thumb_length);
        const qreal range = scroll_bar->maximum() - scroll_bar->minimum();
        const qreal ratio = range > 0.0 ? (scroll_bar->value() - scroll_bar->minimum()) / range : 0.0;
        const qreal thumb_position = TRACK_PADDING + movable_length * ratio;

        if(orientation == Qt::Horizontal)
        {
            return QRectF(thumb_position, (height() - THUMB_WIDTH) / 2.0, thumb_length, THUMB_WIDTH);
        }

        return QRectF((width() - THUMB_WIDTH) / 2.0, thumb_position, THUMB_WIDTH, thumb_length);
    }

    void SetValueFromThumbPosition(qreal position)
    {
        const qreal movable_length = qMax<qreal>(0.0, TrackLength() - ThumbLength());
        if(movable_length <= 0.0)
        {
            return;
        }

        const qreal clamped_position = qBound<qreal>(0.0, position - TRACK_PADDING, movable_length);
        const qreal ratio = clamped_position / movable_length;
        const int range = scroll_bar->maximum() - scroll_bar->minimum();
        scroll_bar->setValue(scroll_bar->minimum() + qRound(range * ratio));
    }

    void UpdateGeometry()
    {
        const QRect viewport_geometry = scroll_area->viewport()->geometry();

        if(orientation == Qt::Horizontal)
        {
            const int length = qMax(0, viewport_geometry.width() - 2 * EDGE_MARGIN);
            setGeometry(viewport_geometry.left() + EDGE_MARGIN,
                viewport_geometry.bottom() - HIT_WIDTH + 1, length, HIT_WIDTH);
        }
        else
        {
            const int length = qMax(0, viewport_geometry.height() - 2 * EDGE_MARGIN);
            setGeometry(viewport_geometry.right() - HIT_WIDTH + 1,
                viewport_geometry.top() + EDGE_MARGIN, HIT_WIDTH, length);
        }

        raise();
        update();
    }

    void UpdatePointerProximity(const QPoint& viewport_position)
    {
        const QRect viewport_rect = scroll_area->viewport()->rect();
        const bool inside = viewport_rect.contains(viewport_position);
        const bool near_edge = orientation == Qt::Horizontal ?
            viewport_position.y() >= viewport_rect.bottom() - REVEAL_DISTANCE :
            viewport_position.x() >= viewport_rect.right() - REVEAL_DISTANCE;
        const bool is_near = IsScrollable() && inside && near_edge;

        if(pointer_near == is_near)
        {
            return;
        }

        pointer_near = is_near;
        if(pointer_near)
        {
            hide_timer.stop();
            Reveal();
        }
        else if(!dragging)
        {
            hide_timer.start();
        }
    }

    void UpdatePointerProximityFromCursor()
    {
        QWidget* scroll_viewport = scroll_area->viewport();
        UpdatePointerProximity(scroll_viewport->mapFromGlobal(QCursor::pos()));
    }

    void Reveal()
    {
        if(!IsScrollable())
        {
            return;
        }

        show();
        raise();
        FadeTo(1.0);
    }

    void RevealTemporarily()
    {
        if(!IsScrollable())
        {
            return;
        }

        Reveal();
        if(!pointer_near && !dragging)
        {
            hide_timer.start();
        }
    }

    void FadeTo(qreal target)
    {
        target_opacity = target;
        if(qFuzzyCompare(opacity, target_opacity))
        {
            if(target_opacity <= 0.0)
            {
                hide();
            }
            return;
        }

        if(!fade_timer.isActive())
        {
            fade_timer.start();
        }
    }

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

QString LucideFontFamily()
{
    static const QString family = []()
    {
        const int font_id = QFontDatabase::addApplicationFont(":/lighttrack/fonts/lucide.ttf");
        if(font_id < 0)
        {
            return QString();
        }

        const QStringList families = QFontDatabase::applicationFontFamilies(font_id);
        return families.isEmpty() ? QString() : families.first();
    }();

    return family;
}

QPushButton* ToolbarButton(ushort codepoint, QWidget* parent)
{
    QPushButton* button = new QPushButton(QString(QChar(codepoint)), parent);
    button->setFixedSize(static_cast<int>(TOOLBAR_HEIGHT), static_cast<int>(TOOLBAR_HEIGHT));
    button->setFocusPolicy(Qt::NoFocus);

    QFont font(LucideFontFamily());
    font.setPixelSize(static_cast<int>(TOOLBAR_ICON_SIZE));
    button->setFont(font);

    return button;
}

class ClipPreviewItem : public QGraphicsItem
{
public:
    ClipPreviewItem()
    {
        setAcceptedMouseButtons(Qt::NoButton);
        setZValue(20.0);
        setVisible(false);
    }

    QRectF boundingRect() const override
    {
        return QRectF(0.0, 0.0, width, CLIP_HEIGHT).adjusted(-1.0, -1.0, 1.0, 1.0);
    }

    void SetPreview(qreal clip_width, const EffectDefinition& preview_effect)
    {
        prepareGeometryChange();
        width = clip_width;
        effect = preview_effect;
        update();
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setBrush(WithAlpha(effect.color, 48));
        painter->setPen(QPen(WithAlpha(effect.color.darker(110), 145), 1.0, Qt::DashLine));
        painter->drawRoundedRect(QRectF(0.0, 0.0, width, CLIP_HEIGHT), 5.0, 5.0);

        painter->setPen(WithAlpha(Qt::white, 150));
        QFont font = painter->font();
        font.setBold(true);
        painter->setFont(font);
        const QRectF text_rect(9.0, 0.0, qMax<qreal>(0.0, width - 18.0), CLIP_HEIGHT);
        painter->drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft,
            QFontMetrics(font).elidedText(effect.name, Qt::ElideRight, static_cast<int>(text_rect.width())));
    }

private:
    qreal width = CLIP_DEFAULT_WIDTH;
    EffectDefinition effect;
};

class MusicSpectrumItem : public QGraphicsItem
{
public:
    MusicSpectrumItem()
    {
        setAcceptedMouseButtons(Qt::NoButton);
        setZValue(12.0);
    }

    QRectF boundingRect() const override
    {
        return QRectF(0.0, 0.0, width, height);
    }

    void SetSpectrum(const QVector<qreal>& new_levels, qreal new_width, qreal new_height)
    {
        prepareGeometryChange();
        levels = new_levels;
        width = new_width;
        height = new_height;
        update();
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
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
            const qreal bar_height = qMax<qreal>(2.0, levels[i] * height * 0.42);
            painter->drawRect(QRectF(x, center_y - bar_height, bar_width, bar_height * 2.0));
        }
    }

private:
    QVector<qreal> levels;
    qreal width = 0.0;
    qreal height = 0.0;
};

class TimelineClipItem : public QGraphicsItem
{
public:
    explicit TimelineClipItem(const EffectDefinition& effect, bool removable = true) :
        effect(effect),
        removable(removable)
    {
        setAcceptHoverEvents(removable);
        setCursor(Qt::OpenHandCursor);
        setZValue(35.0);
    }

    QRectF boundingRect() const override
    {
        return QRectF(0.0, 0.0, width, CLIP_HEIGHT);
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(effect.color);
        painter->drawRoundedRect(boundingRect(), 5.0, 5.0);

        if(removable)
        {
            const QRectF handle_rect = DragHandleRect();
            painter->setPen(QPen(WithAlpha(Qt::white, 70), 1.0));
            painter->drawLine(QPointF(handle_rect.right(), 7.0), QPointF(handle_rect.right(), CLIP_HEIGHT - 7.0));
            painter->setPen(Qt::NoPen);
            painter->setBrush(WithAlpha(Qt::white, 150));

            for(int row = 0; row < 3; row++)
            {
                for(int column = 0; column < 2; column++)
                {
                    painter->drawEllipse(QPointF(7.5 + column * 6.0, 10.0 + row * 6.0), 1.35, 1.35);
                }
            }

            painter->setPen(QPen(WithAlpha(Qt::white, 75), 1.0));
            painter->drawLine(QPointF(width - RESIZE_HANDLE_WIDTH, 7.0), QPointF(width - RESIZE_HANDLE_WIDTH, CLIP_HEIGHT - 7.0));
        }

        painter->setPen(Qt::white);
        QFont font = painter->font();
        font.setBold(true);
        painter->setFont(font);

        const qreal text_left = removable ? CLIP_DRAG_HANDLE_WIDTH + 7.0 : 9.0;
        const qreal text_right = removable ? RemoveRect().left() - 7.0 : width - 12.0;
        const QRectF text_rect = QRectF(text_left, 0.0, qMax<qreal>(0.0, text_right - text_left), CLIP_HEIGHT);
        const QString label = QFontMetrics(font).elidedText(effect.name, Qt::ElideRight, static_cast<int>(text_rect.width()));
        painter->drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft, label);

        if(removable)
        {
            const QRectF close_rect = RemoveRect();
            if(hover_remove)
            {
                painter->setBrush(WithAlpha(Qt::white, 22));
                painter->setPen(QPen(WithAlpha(Qt::white, 180), 1.0));
                painter->drawRoundedRect(close_rect.adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);
            }

            painter->setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(close_rect.topLeft() + QPointF(4.0, 4.0), close_rect.bottomRight() - QPointF(4.0, 4.0));
            painter->drawLine(close_rect.topRight() + QPointF(-4.0, 4.0), close_rect.bottomLeft() + QPointF(4.0, -4.0));
        }
    }

    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override
    {
        SetRemoveHover(IsRemoveButton(event->pos()));

        if(IsResizeHandle(event->pos()))
        {
            setCursor(Qt::SizeHorCursor);
        }
        else if(IsRemoveButton(event->pos()))
        {
            setCursor(Qt::PointingHandCursor);
        }
        else if(IsDragHandle(event->pos()))
        {
            setCursor(Qt::OpenHandCursor);
        }
        else
        {
            unsetCursor();
        }
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent*) override
    {
        SetRemoveHover(false);
        unsetCursor();
    }

    EffectDefinition Effect() const
    {
        return effect;
    }

    int LaneIndex() const
    {
        return lane_index;
    }

    void SetLaneIndex(int index)
    {
        lane_index = index;
    }

    qreal ClipWidth() const
    {
        return width;
    }

    void SetClipWidth(qreal clip_width)
    {
        prepareGeometryChange();
        width = qMax(CLIP_MIN_WIDTH, clip_width);
        update();
    }

    bool IsResizeHandle(const QPointF& pos) const
    {
        return removable && pos.x() >= width - RESIZE_HANDLE_WIDTH;
    }

    bool IsDragHandle(const QPointF& pos) const
    {
        return removable && DragHandleRect().contains(pos);
    }

    bool IsRemoveButton(const QPointF& pos) const
    {
        return removable && RemoveRect().contains(pos);
    }

private:
    QRectF DragHandleRect() const
    {
        return QRectF(0.0, 0.0, CLIP_DRAG_HANDLE_WIDTH, CLIP_HEIGHT);
    }

    QRectF RemoveRect() const
    {
        return QRectF(width - REMOVE_BUTTON_RIGHT_MARGIN - REMOVE_BUTTON_SIZE, 7.0, REMOVE_BUTTON_SIZE, REMOVE_BUTTON_SIZE);
    }

    void SetRemoveHover(bool hover)
    {
        if(hover_remove == hover)
        {
            return;
        }

        hover_remove = hover;
        update(RemoveRect().adjusted(-2.0, -2.0, 2.0, 2.0));
    }

    EffectDefinition effect;
    int lane_index = 0;
    qreal width = CLIP_DEFAULT_WIDTH;
    bool removable = true;
    bool hover_remove = false;
};

class LightTrackScene : public QGraphicsScene
{
public:
    explicit LightTrackScene(QObject* parent = nullptr) :
        QGraphicsScene(parent)
    {
    }

    void SetPalette(const QPalette& new_palette)
    {
        palette = new_palette;
        RebuildLayout();
    }

    void SetViewportSize(const QSize& size)
    {
        if(viewport_size == size)
        {
            return;
        }

        viewport_size = size;
        RebuildLayout();
    }

    void SetLanes(const QVector<LaneEntry>& entries, const QString& empty_text)
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

    void SetVisibleLanes(const QVector<int>& indices)
    {
        QVector<int> filtered_indices;
        filtered_indices.reserve(indices.size());

        for(int index : indices)
        {
            if(index >= 0 && index < lane_entries.size() && !filtered_indices.contains(index))
            {
                filtered_indices.push_back(index);
            }
        }

        if(filtered_indices.size() == visible_lane_indices.size()
            && std::equal(filtered_indices.cbegin(), filtered_indices.cend(),
                visible_lane_indices.cbegin()))
        {
            return;
        }

        CancelDrag();
        visible_lane_indices = filtered_indices;
        RebuildLayout();
    }

    void SetMusicSpectrum(const QVector<qreal>& spectrum, qint64 duration_ms)
    {
        music_spectrum = spectrum;
        music_duration_ms = qMax<qint64>(0, duration_ms);
        music_position_ms = 0;
        RebuildLayout();
    }

    void SetMusicPosition(qint64 position_ms)
    {
        music_position_ms = qMax<qint64>(0, position_ms);

        if(music_duration_ms > 0)
        {
            music_position_ms = qMin(music_position_ms, music_duration_ms);
        }

        UpdatePlayhead();
    }

    void SetMusicSeekCallback(std::function<void(qint64)> callback)
    {
        music_seek_callback = std::move(callback);
    }

    bool SetPixelsPerSecond(qreal value)
    {
        value = qBound(TIMELINE_ZOOM_MIN, value, TIMELINE_ZOOM_MAX);

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

    qreal PixelsPerSecond() const
    {
        return pixels_per_second;
    }

    qreal ContentWidth() const
    {
        return sceneRect().width();
    }

    QVector<TimelineClipState> TimelineClips() const
    {
        QVector<TimelineClipState> states;

        if(pixels_per_second <= 0.0)
        {
            return states;
        }

        for(TimelineClipItem* clip : clips)
        {
            const qreal start_x = clip->pos().x();
            states.push_back({clip, clip->Effect(), clip->LaneIndex(),
                qMax<qint64>(0, static_cast<qint64>(std::floor(start_x * 1000.0 / pixels_per_second))),
                qMax<qint64>(0, static_cast<qint64>(std::ceil((start_x + clip->ClipWidth()) * 1000.0 / pixels_per_second)))});
        }

        return states;
    }

    bool PreviewEffectAt(const QString& effect_name, const QPointF& pos)
    {
        EffectDefinition effect;
        if(!FindEffect(effect_name, &effect))
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
        const qreal x = ClampClipX(lane, pos.x() - clip_width / 2.0, clip_width);
        ShowPreview(lane, x, clip_width, effect);
        return true;
    }

    bool AddEffectAt(const QString& effect_name, const QPointF& pos)
    {
        EffectDefinition effect;
        if(!FindEffect(effect_name, &effect))
        {
            return false;
        }

        const int lane = LaneAt(pos);
        if(lane < 0)
        {
            return false;
        }

        const qreal clip_width = DefaultClipWidth();
        AddClip(effect, lane, pos.x() - clip_width / 2.0, clip_width);
        return true;
    }

    void ClearPreview()
    {
        HidePreview();
    }

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override
    {
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->fillRect(rect, palette.color(QPalette::Base));

        QColor music_row_color = palette.color(QPalette::Window);
        if(!music_row_color.isValid())
        {
            music_row_color = palette.color(QPalette::Base);
        }

        PaintRow(painter, QRectF(0.0, 0.0, sceneRect().width(), ROW_HEIGHT),
            music_row_color, TextLineColor(palette, 52, 82), TextLineColor(palette, 24, 42), rect);

        if(lane_entries.empty())
        {
            if(!empty_message.isEmpty())
            {
                QFont font;
                font.setPointSize(10);
                painter->setRenderHint(QPainter::TextAntialiasing, true);
                painter->setFont(font);
                painter->setPen(TextLineColor(palette, 160, 170));
                painter->drawText(QRectF(12.0, ROW_HEIGHT + 14.0, sceneRect().width() - 24.0, ROW_HEIGHT),
                    Qt::AlignLeft | Qt::AlignTop, empty_message);
            }
            return;
        }

        for(int lane_index : visible_lane_indices)
        {
            const QColor row_color = palette.color(QPalette::Base);

            PaintRow(painter, lanes[lane_index].rect,
                row_color, TextLineColor(palette, 38, 72), TextLineColor(palette, 20, 38), rect);
        }

        PaintInheritedClips(painter, rect);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override
    {
        if(event->button() != Qt::LeftButton)
        {
            QGraphicsScene::mousePressEvent(event);
            return;
        }

        if(IsPlayheadHandle(event->scenePos()))
        {
            drag_mode = PlayheadSeek;
            SeekMusicAt(event->scenePos().x());
            event->accept();
            return;
        }

        TimelineClipItem* clip = ClipAt(event->scenePos());
        if(clip == nullptr)
        {
            QGraphicsScene::mousePressEvent(event);
            return;
        }

        const QPointF local_pos = clip->mapFromScene(event->scenePos());

        if(clip->IsRemoveButton(local_pos))
        {
            RemoveClip(clip);
            event->accept();
            return;
        }

        if(!clip->IsResizeHandle(local_pos) && !clip->IsDragHandle(local_pos))
        {
            event->accept();
            return;
        }

        active_clip = clip;
        drag_mode = clip->IsResizeHandle(local_pos) ? ClipResize : ClipMove;
        drag_offset = local_pos;
        drag_scene_start = event->scenePos();
        clip_start_x = clip->pos().x();
        clip_start_width = clip->ClipWidth();
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

    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override
    {
        if(drag_mode == ClipMove)
        {
            UpdateClipMove(event->scenePos());
            event->accept();
            return;
        }

        if(drag_mode == ClipResize)
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

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override
    {
        if(drag_mode == PlayheadSeek)
        {
            drag_mode = NoDrag;
            event->accept();
            return;
        }

        if(drag_mode == ClipMove || drag_mode == ClipResize)
        {
            if(active_clip != nullptr)
            {
                active_clip->setOpacity(1.0);
                active_clip->setZValue(35.0);
            }

            HidePreview();
            active_clip = nullptr;
            drag_mode = NoDrag;
            event->accept();
            return;
        }

        QGraphicsScene::mouseReleaseEvent(event);
    }

private:
    enum DragMode
    {
        NoDrag,
        ClipMove,
        ClipResize,
        PlayheadSeek
    };

    template<typename ItemType>
    ItemType* Track(ItemType* item)
    {
        item->setAcceptedMouseButtons(Qt::NoButton);
        layout_items.push_back(item);
        return item;
    }

    void ClearLayoutItems()
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

    void ClearClips()
    {
        for(TimelineClipItem* clip : clips)
        {
            removeItem(clip);
            delete clip;
        }
        clips.clear();
        active_clip = nullptr;
    }

    void RebuildLayout()
    {
        ClearLayoutItems();
        lanes.clear();

        const int lane_count = qMax(1, visible_lane_indices.size());
        const qreal content_height = ROW_HEIGHT + lane_count * ROW_HEIGHT;
        const qreal scene_width = qMax(qMax<qreal>(TIMELINE_MIN_WIDTH, viewport_size.width()), MusicPixelWidth());
        const qreal scene_height = qMax<qreal>(content_height, viewport_size.height());

        setSceneRect(0.0, 0.0, scene_width, scene_height);

        for(int i = 0; i < lane_entries.size(); i++)
        {
            lanes.push_back({lane_entries[i].name, QRectF(), lane_entries[i].level});
        }

        for(int visible_row = 0; visible_row < visible_lane_indices.size(); visible_row++)
        {
            const int lane_index = visible_lane_indices[visible_row];
            lanes[lane_index].rect = QRectF(0.0, ROW_HEIGHT + visible_row * ROW_HEIGHT,
                scene_width, ROW_HEIGHT);
        }

        invalidate(sceneRect(), QGraphicsScene::BackgroundLayer);
        AddMusicItems();
        RelayoutClips();
    }

    void RefreshInheritedClips()
    {
        invalidate(sceneRect(), QGraphicsScene::BackgroundLayer);
    }

    void PaintRow(QPainter* painter, const QRectF& row_rect, const QColor& fill, const QColor& border,
        const QColor& grid, const QRectF& exposed) const
    {
        if(!row_rect.intersects(exposed))
        {
            return;
        }

        painter->fillRect(row_rect.intersected(exposed), fill);
        painter->setPen(QPen(border));
        painter->drawRect(row_rect.adjusted(0.0, 0.0, -0.5, -0.5));

        if(pixels_per_second <= 0.0)
        {
            return;
        }

        painter->setPen(QPen(grid));
        const qreal first_x = std::ceil(qMax(pixels_per_second, exposed.left()) / pixels_per_second) * pixels_per_second;

        for(qreal x = first_x; x < row_rect.right() && x <= exposed.right(); x += pixels_per_second)
        {
            painter->drawLine(QPointF(x, row_rect.top() + 1.0), QPointF(x, row_rect.bottom() - 1.0));
        }
    }

    void RelayoutClips()
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

            const int lane = qBound(0, clip->LaneIndex(), lanes.size() - 1);
            clip->SetLaneIndex(lane);

            if(!lanes[lane].rect.isValid() || lanes[lane].rect.isEmpty())
            {
                clip->setVisible(false);
                continue;
            }

            clip->setVisible(true);
            const qreal x = ClampClipX(lane, clip->pos().x(), clip->ClipWidth());
            clip->setPos(x, ClipY(lane));
        }

        RefreshInheritedClips();
    }

    bool IsDescendantLane(int lane, int ancestor_lane) const
    {
        if(lane <= ancestor_lane || lane >= lanes.size() || ancestor_lane >= lanes.size())
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

    void PaintGhostClip(QPainter* painter, const QRectF& exposed, int lane, qreal x, qreal width,
        const EffectDefinition& effect, bool preview_ghost) const
    {
        const QRectF ghost_rect(x, ClipY(lane), width, CLIP_HEIGHT);
        if(!ghost_rect.intersects(exposed))
        {
            return;
        }

        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setBrush(WithAlpha(effect.color, preview_ghost ? 36 : 44));
        painter->setPen(QPen(WithAlpha(effect.color.darker(115), preview_ghost ? 100 : 125),
            1.0, preview_ghost ? Qt::DashLine : Qt::SolidLine));
        painter->drawRoundedRect(ghost_rect, 5.0, 5.0);

        QFont font = painter->font();
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(WithAlpha(Qt::white, preview_ghost ? 88 : 110));
        painter->drawText(ghost_rect.adjusted(9.0, 0.0, -9.0, 0.0), Qt::AlignVCenter | Qt::AlignLeft,
            QFontMetrics(font).elidedText(effect.name, Qt::ElideRight, static_cast<int>(ghost_rect.width() - 18.0)));
    }

    void PaintInheritedClips(QPainter* painter, const QRectF& exposed) const
    {
        for(int lane = 0; lane < lanes.size(); lane++)
        {
            if(!lanes[lane].rect.intersects(exposed))
            {
                continue;
            }

            for(int ancestor_lane = 0; ancestor_lane < lane; ancestor_lane++)
            {
                if(!IsDescendantLane(lane, ancestor_lane))
                {
                    continue;
                }

                for(TimelineClipItem* clip : clips)
                {
                    if(clip->LaneIndex() == ancestor_lane)
                    {
                        PaintGhostClip(painter, exposed, lane, clip->pos().x(), clip->ClipWidth(), clip->Effect(), false);
                    }
                }

                if(active_clip == nullptr && last_preview_lane == ancestor_lane)
                {
                    PaintGhostClip(painter, exposed, lane, last_preview_x, last_preview_width, last_preview_effect, true);
                }
            }
        }
    }

    TimelineClipItem* ClipAt(const QPointF& pos) const
    {
        return dynamic_cast<TimelineClipItem*>(itemAt(pos, QTransform()));
    }

    int LaneAt(const QPointF& pos) const
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

    qreal ClipY(int lane) const
    {
        return lanes[lane].rect.top() + (lanes[lane].rect.height() - CLIP_HEIGHT) / 2.0;
    }

    qreal ClampClipX(int lane, qreal x, qreal width) const
    {
        const QRectF rect = lanes[lane].rect;
        return qBound(rect.left(), x, rect.right() - width);
    }

    qreal MusicPixelWidth() const
    {
        if(music_duration_ms <= 0)
        {
            return 0.0;
        }

        return music_duration_ms * pixels_per_second / 1000.0;
    }

    void AddMusicItems()
    {
        const qreal music_width = MusicPixelWidth();

        if(!music_spectrum.empty() && music_width > 0.0)
        {
            MusicSpectrumItem* item = new MusicSpectrumItem();
            addItem(item);
            Track(item);
            item->SetSpectrum(music_spectrum, music_width, ROW_HEIGHT);
            music_item = item;
        }

        if(music_duration_ms > 0)
        {
            playhead_item = Track(addLine(0.0, 0.0, 0.0, sceneRect().height(), QPen(QColor(255, 255, 255, 220), 2.0)));
            playhead_item->setZValue(95.0);
            playhead_handle = Track(addEllipse(-PLAYHEAD_HANDLE_RADIUS, ROW_HEIGHT / 2.0 - PLAYHEAD_HANDLE_RADIUS,
                PLAYHEAD_HANDLE_RADIUS * 2.0, PLAYHEAD_HANDLE_RADIUS * 2.0,
                QPen(QColor(255, 255, 255, 235), 2.0), QBrush(QColor(64, 188, 255))));
            playhead_handle->setZValue(100.0);
            UpdatePlayhead();
        }
    }

    void UpdatePlayhead()
    {
        if(playhead_item == nullptr)
        {
            return;
        }

        const qreal x = qBound<qreal>(0.0, music_position_ms * pixels_per_second / 1000.0, sceneRect().right());
        playhead_item->setLine(x, 0.0, x, sceneRect().height());

        if(playhead_handle != nullptr)
        {
            playhead_handle->setRect(x - PLAYHEAD_HANDLE_RADIUS, ROW_HEIGHT / 2.0 - PLAYHEAD_HANDLE_RADIUS,
                PLAYHEAD_HANDLE_RADIUS * 2.0, PLAYHEAD_HANDLE_RADIUS * 2.0);
        }
    }

    bool IsPlayheadHandle(const QPointF& pos) const
    {
        return music_duration_ms > 0 && playhead_handle != nullptr
            && playhead_handle->rect().adjusted(-4.0, -4.0, 4.0, 4.0).contains(pos);
    }

    void SeekMusicAt(qreal x)
    {
        if(music_duration_ms <= 0)
        {
            return;
        }

        const qint64 position_ms = qBound<qint64>(0, static_cast<qint64>(x * 1000.0 / pixels_per_second), music_duration_ms);
        SetMusicPosition(position_ms);

        if(music_seek_callback)
        {
            music_seek_callback(position_ms);
        }
    }

    void UpdateClipMove(const QPointF& pos)
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
        const qreal x = ClampClipX(target_lane, pos.x() - drag_offset.x(), width);
        active_clip->SetLaneIndex(target_lane);
        active_clip->setPos(x, ClipY(target_lane));
        ShowPreview(target_lane, x, width, active_clip->Effect());
    }

    void UpdateClipResize(const QPointF& pos)
    {
        if(active_clip == nullptr)
        {
            return;
        }

        const int lane = active_clip->LaneIndex();
        const qreal max_width = lanes[lane].rect.right() - clip_start_x;
        const qreal width = qBound(CLIP_MIN_WIDTH, clip_start_width + pos.x() - drag_scene_start.x(), max_width);
        active_clip->SetClipWidth(width);
        ShowPreview(lane, clip_start_x, width, active_clip->Effect());
    }

    void ShowPreview(int lane, qreal x, qreal width, const EffectDefinition& effect)
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

    void HidePreview()
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

    void AddClip(const EffectDefinition& effect, int lane, qreal x, qreal width)
    {
        if(lane < 0 || lane >= lanes.size())
        {
            return;
        }

        TimelineClipItem* clip = new TimelineClipItem(effect);
        clip->SetLaneIndex(lane);
        clip->SetClipWidth(width);
        clip->setPos(ClampClipX(lane, x, width), ClipY(lane));
        addItem(clip);
        clips.push_back(clip);
        RefreshInheritedClips();
    }

    qreal DefaultClipWidth() const
    {
        return qMax(CLIP_MIN_WIDTH, CLIP_DEFAULT_WIDTH * pixels_per_second / GRID_WIDTH);
    }

    void RemoveClip(TimelineClipItem* clip)
    {
        clips.removeOne(clip);
        removeItem(clip);
        delete clip;

        if(active_clip == clip)
        {
            active_clip = nullptr;
        }

        RefreshInheritedClips();
    }

    void CancelDrag()
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

    QPalette palette;
    QSize viewport_size;
    QVector<LaneEntry> lane_entries;
    QVector<int> visible_lane_indices;
    QVector<LaneInfo> lanes;
    QVector<QGraphicsItem*> layout_items;
    QVector<TimelineClipItem*> clips;
    QString empty_message;
    QVector<qreal> music_spectrum;
    std::function<void(qint64)> music_seek_callback;
    qint64 music_duration_ms = 0;
    qint64 music_position_ms = 0;
    qreal pixels_per_second = GRID_WIDTH;

    DragMode drag_mode = NoDrag;
    QPointF drag_offset;
    QPointF drag_scene_start;
    qreal clip_start_x = 0.0;
    qreal clip_start_width = CLIP_DEFAULT_WIDTH;
    int last_preview_lane = -1;
    qreal last_preview_x = 0.0;
    qreal last_preview_width = CLIP_DEFAULT_WIDTH;
    EffectDefinition last_preview_effect;
    TimelineClipItem* active_clip = nullptr;
    ClipPreviewItem* preview = nullptr;
    MusicSpectrumItem* music_item = nullptr;
    QGraphicsLineItem* playhead_item = nullptr;
    QGraphicsEllipseItem* playhead_handle = nullptr;
};

class TimelineRulerWidget : public QWidget
{
public:
    explicit TimelineRulerWidget(QWidget* parent = nullptr) :
        QWidget(parent)
    {
        setFixedHeight(static_cast<int>(RULER_HEIGHT));
    }

    void SetContentWidth(qreal width)
    {
        if(qFuzzyCompare(content_width, width))
        {
            return;
        }

        content_width = width;
        update();
    }

    void SetPixelsPerSecond(qreal value)
    {
        if(qFuzzyCompare(pixels_per_second, value))
        {
            return;
        }

        pixels_per_second = qBound(TIMELINE_ZOOM_MIN, value, TIMELINE_ZOOM_MAX);
        update();
    }

    void SetScrollOffset(int offset)
    {
        if(horizontal_offset == offset)
        {
            return;
        }

        horizontal_offset = offset;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Base));
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        const QColor line_color = TextLineColor(palette(), 90, 120);
        const QColor text_color = TextLineColor(palette(), 160, 180);
        const qreal base_y = height() - 6.0;

        painter.setPen(QPen(line_color));
        painter.drawLine(QPointF(0.0, base_y), QPointF(width(), base_y));

        for(qreal x = 0.0; x <= content_width; x += pixels_per_second)
        {
            const qreal view_x = x - horizontal_offset;

            if(view_x < -pixels_per_second || view_x > width() + pixels_per_second)
            {
                continue;
            }

            painter.setPen(QPen(line_color));
            painter.drawLine(QPointF(view_x, 12.0), QPointF(view_x, base_y));
            painter.setPen(text_color);
            painter.drawText(QRectF(view_x + 4.0, 0.0, 48.0, height() - 8.0),
                Qt::AlignVCenter | Qt::AlignLeft, QString::number(static_cast<int>(x / pixels_per_second)) + "s");
        }
    }

private:
    qreal content_width = TIMELINE_MIN_WIDTH;
    qreal pixels_per_second = GRID_WIDTH;
    int horizontal_offset = 0;
};

class LaneListWidget : public QTreeWidget
{
public:
    explicit LaneListWidget(QWidget* parent = nullptr) :
        QTreeWidget(parent)
    {
        setAlternatingRowColors(false);
        setColumnCount(1);
        setFrameShape(QFrame::NoFrame);
        setHeaderHidden(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setIndentation(18);
        setItemsExpandable(true);
        setRootIsDecorated(true);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSelectionMode(QAbstractItemView::NoSelection);
        setUniformRowHeights(true);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

        connect(this, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem*)
        {
            NotifyVisibleLanesChanged();
        });
        connect(this, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem*)
        {
            NotifyVisibleLanesChanged();
        });
    }

    void SetLanes(const QVector<LaneEntry>& lanes)
    {
        rebuilding = true;
        clear();

        QTreeWidgetItem* music_item = new QTreeWidgetItem(this, QStringList("Music"));
        music_item->setData(0, LaneIndexRole, -1);
        music_item->setFlags(Qt::ItemIsEnabled);
        music_item->setSizeHint(0, QSize(0, static_cast<int>(ROW_HEIGHT)));
        music_item->setToolTip(0, "Music");

        QTreeWidgetItem* controller_item = nullptr;
        QTreeWidgetItem* zone_item = nullptr;

        for(int lane_index = 0; lane_index < lanes.size(); lane_index++)
        {
            const LaneEntry& lane = lanes[lane_index];
            QTreeWidgetItem* parent_item = nullptr;
            if(lane.level == 1)
            {
                parent_item = controller_item;
            }
            else if(lane.level >= 2)
            {
                parent_item = zone_item != nullptr ? zone_item : controller_item;
            }

            QTreeWidgetItem* item = parent_item != nullptr ?
                new QTreeWidgetItem(parent_item, QStringList(lane.name)) :
                new QTreeWidgetItem(this, QStringList(lane.name));

            item->setData(0, LaneIndexRole, lane_index);
            item->setFlags(Qt::ItemIsEnabled);
            item->setSizeHint(0, QSize(0, static_cast<int>(ROW_HEIGHT)));
            item->setToolTip(0, lane.name);

            if(lane.level == 0)
            {
                controller_item = item;
                zone_item = nullptr;
            }
            else if(lane.level == 1)
            {
                zone_item = item;
            }
        }

        expandAll();
        rebuilding = false;
    }

    void SetVisibilityChangedCallback(std::function<void(const QVector<int>&)> callback)
    {
        visibility_changed_callback = std::move(callback);
    }

    QVector<int> VisibleLaneIndices() const
    {
        QVector<int> indices;

        for(int i = 0; i < topLevelItemCount(); i++)
        {
            AppendVisibleLaneIndices(topLevelItem(i), indices);
        }

        return indices;
    }

protected:
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        QTreeWidget::drawRow(painter, option, index);

        const bool is_music_row = !index.parent().isValid() && index.row() == 0;
        painter->save();
        painter->setPen(QPen(TextLineColor(palette(), is_music_row ? 52 : 38,
            is_music_row ? 82 : 72)));
        const qreal separator_y = option.rect.y() + option.rect.height() - 0.5;
        painter->drawLine(QPointF(option.rect.left(), separator_y),
            QPointF(option.rect.right() + 1.0, separator_y));
        painter->restore();
    }

private:
    void AppendVisibleLaneIndices(const QTreeWidgetItem* item, QVector<int>& indices) const
    {
        const int lane_index = item->data(0, LaneIndexRole).toInt();
        if(lane_index >= 0)
        {
            indices.push_back(lane_index);
        }

        if(!item->isExpanded())
        {
            return;
        }

        for(int i = 0; i < item->childCount(); i++)
        {
            AppendVisibleLaneIndices(item->child(i), indices);
        }
    }

    void NotifyVisibleLanesChanged()
    {
        if(!rebuilding && visibility_changed_callback)
        {
            visibility_changed_callback(VisibleLaneIndices());
        }
    }

    bool rebuilding = false;
    std::function<void(const QVector<int>&)> visibility_changed_callback;
};

class EffectsListWidget : public QTreeWidget
{
public:
    explicit EffectsListWidget(QWidget* parent = nullptr) :
        QTreeWidget(parent)
    {
        setColumnCount(2);
        setDragEnabled(true);
        setFrameShape(QFrame::NoFrame);
        setHeaderHidden(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setIndentation(16);
        setTreePosition(0);
        header()->setMinimumSectionSize(indentation());
        header()->setStretchLastSection(false);
        header()->setSectionResizeMode(0, QHeaderView::Fixed);
        header()->setSectionResizeMode(1, QHeaderView::Stretch);
        header()->resizeSection(0, indentation());
        setRootIsDecorated(true);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setUniformRowHeights(true);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

        new OverlayScrollBar(this, Qt::Vertical);

        for(const LightTrackEffectGroup& group : EffectGroups())
        {
            QTreeWidgetItem* group_item = new QTreeWidgetItem(this);
            group_item->setText(1, group.name);
            group_item->setFlags(Qt::ItemIsEnabled);
            group_item->setSizeHint(1, QSize(0, static_cast<int>(EFFECT_ROW_HEIGHT)));
            group_item->setToolTip(1, group.name);

            for(const LightTrackEffectInfo& effect : group.effects)
            {
                QTreeWidgetItem* item = new QTreeWidgetItem(group_item);
                item->setText(1, effect.name);
                item->setData(1, EffectIdRole, effect.id.toUtf8());
                item->setIcon(1, ColorIcon(effect.color));
                item->setToolTip(1, group.name + QStringLiteral(" / ") + effect.name);
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
                item->setSizeHint(1, QSize(0, static_cast<int>(EFFECT_ROW_HEIGHT)));
            }
        }

        expandAll();
    }

protected:
    void startDrag(Qt::DropActions) override
    {
        QTreeWidgetItem* item = currentItem();
        if(item == nullptr || item->childCount() > 0)
        {
            return;
        }

        QMimeData* mime_data = new QMimeData();
        mime_data->setData(EFFECT_MIME, item->data(1, EffectIdRole).toByteArray());

        QDrag* drag = new QDrag(this);
        drag->setMimeData(mime_data);
        drag->exec(Qt::CopyAction);
    }

private:
    QIcon ColorIcon(const QColor& color) const
    {
        QPixmap pixmap(10, 10);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color.isValid() ? color : palette().color(QPalette::Highlight));
        painter.drawEllipse(QRectF(1.0, 1.0, 8.0, 8.0));
        return QIcon(pixmap);
    }
};

class LightTrackView : public QGraphicsView
{
public:
    explicit LightTrackView(QWidget* parent = nullptr) :
        QGraphicsView(parent),
        light_scene(new LightTrackScene(this))
    {
        setScene(light_scene);
        setAcceptDrops(true);
        setAlignment(Qt::AlignLeft | Qt::AlignTop);
        setDragMode(QGraphicsView::NoDrag);
        setFrameShape(QFrame::NoFrame);
        setRenderHints(QPainter::TextAntialiasing);
        setCacheMode(QGraphicsView::CacheBackground);
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        light_scene->SetPalette(palette());
        light_scene->SetViewportSize(viewport()->size());

        new OverlayScrollBar(this, Qt::Horizontal);
        new OverlayScrollBar(this, Qt::Vertical);

        connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int value)
        {
            if(ruler != nullptr)
            {
                ruler->SetScrollOffset(value);
            }
        });
    }

    void SetRuler(TimelineRulerWidget* ruler_widget)
    {
        ruler = ruler_widget;
        SyncRuler();
    }

    void SetLanes(const QVector<LaneEntry>& lanes, const QString& empty_text)
    {
        light_scene->SetLanes(lanes, empty_text);
        SyncRuler();
    }

    void SetVisibleLanes(const QVector<int>& lane_indices)
    {
        light_scene->SetVisibleLanes(lane_indices);
        SyncRuler();
    }

    void SetMusicSpectrum(const QVector<qreal>& spectrum, qint64 duration_ms)
    {
        light_scene->SetMusicSpectrum(spectrum, duration_ms);
        SyncRuler();
    }

    void SetMusicPosition(qint64 position_ms)
    {
        light_scene->SetMusicPosition(position_ms);
    }

    QVector<TimelineClipState> TimelineClips() const
    {
        return light_scene->TimelineClips();
    }

    void SetMusicSeekCallback(std::function<void(qint64)> callback)
    {
        light_scene->SetMusicSeekCallback(std::move(callback));
    }

    void SetHorizontalZoom(int pixels_per_second)
    {
        const QPoint anchor = viewport()->rect().center();
        const qreal old_scene_x = mapToScene(anchor).x();
        const qreal old_pixels_per_second = light_scene->PixelsPerSecond();

        if(!light_scene->SetPixelsPerSecond(pixels_per_second))
        {
            return;
        }

        SyncRuler();
        horizontalScrollBar()->setValue(static_cast<int>(old_scene_x * light_scene->PixelsPerSecond() / old_pixels_per_second - anchor.x()));
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QGraphicsView::resizeEvent(event);
        light_scene->SetViewportSize(viewport()->size());
        SyncRuler();
    }

    void changeEvent(QEvent* event) override
    {
        QGraphicsView::changeEvent(event);

        if(event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        {
            light_scene->SetPalette(palette());
            SyncRuler();
        }
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if(event->mimeData()->hasFormat(EFFECT_MIME))
        {
            event->acceptProposedAction();
            return;
        }

        QGraphicsView::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if(!event->mimeData()->hasFormat(EFFECT_MIME))
        {
            QGraphicsView::dragMoveEvent(event);
            return;
        }

        light_scene->PreviewEffectAt(QString::fromUtf8(event->mimeData()->data(EFFECT_MIME)), mapToScene(EventPosition(event)));
        event->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent* event) override
    {
        light_scene->ClearPreview();
        QGraphicsView::dragLeaveEvent(event);
    }

    void dropEvent(QDropEvent* event) override
    {
        if(!event->mimeData()->hasFormat(EFFECT_MIME))
        {
            QGraphicsView::dropEvent(event);
            return;
        }

        const bool added = light_scene->AddEffectAt(QString::fromUtf8(event->mimeData()->data(EFFECT_MIME)), mapToScene(EventPosition(event)));
        light_scene->ClearPreview();

        if(added)
        {
            event->acceptProposedAction();
        }
    }

private:
    void SyncRuler()
    {
        if(ruler == nullptr)
        {
            return;
        }

        ruler->SetContentWidth(light_scene->ContentWidth());
        ruler->SetPixelsPerSecond(light_scene->PixelsPerSecond());
        ruler->SetScrollOffset(horizontalScrollBar()->value());
    }

    LightTrackScene* light_scene;
    TimelineRulerWidget* ruler = nullptr;
};

class LightTrackPage : public QWidget
{
public:
    explicit LightTrackPage(ResourceManagerInterface* resource_manager, QWidget* parent = nullptr) :
        QWidget(parent),
        resource_manager(resource_manager)
    {
        QVBoxLayout* page_layout = new QVBoxLayout(this);
        page_layout->setContentsMargins(static_cast<int>(PAGE_MARGIN), static_cast<int>(PAGE_MARGIN),
            static_cast<int>(PAGE_MARGIN), static_cast<int>(PAGE_MARGIN));
        page_layout->setSpacing(0);

        QWidget* toolbar = new QWidget(this);
        toolbar->setObjectName("lightTrackToolbar");
        toolbar->setFixedHeight(static_cast<int>(TOOLBAR_HEIGHT));
        toolbar->setStyleSheet(R"(
            QWidget#lightTrackToolbar QPushButton {
                background-color: transparent;
                border: 1px solid transparent;
                border-radius: 8px;
                padding: 0;
            }

            QWidget#lightTrackToolbar QPushButton:hover {
                background-color: rgba(127, 127, 127, 45);
            }

            QWidget#lightTrackToolbar QPushButton:pressed {
                background-color: rgba(127, 127, 127, 80);
            }

            QWidget#lightTrackToolbar QPushButton#toolbarMusicButton {
                padding: 0 10px;
            }
        )");
        QHBoxLayout* toolbar_layout = new QHBoxLayout(toolbar);
        toolbar_layout->setContentsMargins(0, 0, 0, 0);
        toolbar_layout->setSpacing(0);

        toolbar_left_group = new QWidget(toolbar);
        toolbar_left_group->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        QHBoxLayout* toolbar_left_layout = new QHBoxLayout(toolbar_left_group);
        toolbar_left_layout->setContentsMargins(0, 0, 0, 0);
        toolbar_left_layout->setSpacing(0);
        toolbar_left_layout->addWidget(ToolbarButton(0xE2A1, toolbar_left_group));
        toolbar_left_layout->addWidget(ToolbarButton(0xE2A0, toolbar_left_group));
        toolbar_left_layout->addWidget(ToolbarButton(0xE14D, toolbar_left_group));

        toolbar_right_group = new QWidget(toolbar);
        toolbar_right_group->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        QHBoxLayout* toolbar_right_layout = new QHBoxLayout(toolbar_right_group);
        toolbar_right_layout->setContentsMargins(0, 0, 0, 0);
        toolbar_right_layout->setSpacing(0);
        toolbar_right_layout->addStretch();
        toolbar_music_button = new QPushButton("Select Music File", toolbar_right_group);
        toolbar_music_button->setObjectName("toolbarMusicButton");
        toolbar_music_button->setFixedHeight(static_cast<int>(TOOLBAR_HEIGHT));
        toolbar_music_button->setFocusPolicy(Qt::NoFocus);
        toolbar_right_layout->addWidget(toolbar_music_button);
        UpdateToolbarSideWidths();

        toolbar_layout->addWidget(toolbar_left_group);
        toolbar_layout->addStretch();
        toolbar_play_button = ToolbarButton(0xE13C, toolbar);
        toolbar_play_button->setEnabled(false);
        toolbar_layout->addWidget(toolbar_play_button);
        toolbar_layout->addStretch();
        toolbar_layout->addWidget(toolbar_right_group);

        QFrame* toolbar_separator = new QFrame(this);
        toolbar_separator->setFrameShape(QFrame::HLine);
        toolbar_separator->setFrameShadow(QFrame::Sunken);

        QWidget* content = new QWidget(this);
        QHBoxLayout* content_layout = new QHBoxLayout(content);
        content_layout->setContentsMargins(0, static_cast<int>(GAP), 0, 0);
        content_layout->setSpacing(0);

        lane_list = new LaneListWidget(this);
        ruler = new TimelineRulerWidget(this);
        view = new LightTrackView(this);
        effects_list = new EffectsListWidget(this);
        music_timer = new QTimer(this);
        music_timer->setInterval(33);
        view->SetRuler(ruler);
        view->SetMusicSeekCallback([this](qint64 position_ms)
        {
            SeekMusic(position_ms);
        });
        lane_list->SetVisibilityChangedCallback([this](const QVector<int>& lane_indices)
        {
            view->SetVisibleLanes(lane_indices);
        });

        QWidget* track_area = new QWidget(content);
        QHBoxLayout* track_layout = new QHBoxLayout(track_area);
        track_layout->setContentsMargins(0, 0, 0, 0);
        track_layout->setSpacing(0);

        QWidget* left_column = new QWidget(track_area);
        QVBoxLayout* left_layout = new QVBoxLayout(left_column);
        left_layout->setContentsMargins(0, 0, 0, 0);
        left_layout->setSpacing(0);
        QLabel* left_header = HeaderLabel("Devices / Zones", left_column);
        left_header->setFixedHeight(static_cast<int>(HEADER_HEIGHT + RULER_HEIGHT));
        left_layout->addWidget(left_header);
        left_layout->addWidget(lane_list);
        left_column->setFixedWidth(static_cast<int>(LABEL_WIDTH));

        QWidget* center_column = new QWidget(track_area);
        QVBoxLayout* center_layout = new QVBoxLayout(center_column);
        center_layout->setContentsMargins(0, 0, 0, 0);
        center_layout->setSpacing(0);
        QWidget* timeline_header = new QWidget(center_column);
        timeline_header->setFixedHeight(static_cast<int>(HEADER_HEIGHT));
        QHBoxLayout* timeline_header_layout = new QHBoxLayout(timeline_header);
        timeline_header_layout->setContentsMargins(0, 0, 0, 0);
        timeline_header_layout->setSpacing(6);
        QLabel* timeline_title = HeaderLabel("Timeline", timeline_header);
        QSlider* zoom_slider = new QSlider(Qt::Horizontal, timeline_header);
        zoom_slider->setRange(static_cast<int>(TIMELINE_ZOOM_MIN), static_cast<int>(TIMELINE_ZOOM_MAX));
        zoom_slider->setValue(static_cast<int>(GRID_WIDTH));
        zoom_slider->setFixedWidth(120);
        zoom_slider->setToolTip("Timeline zoom");
        timeline_header_layout->addWidget(timeline_title);
        timeline_header_layout->addStretch();
        timeline_header_layout->addWidget(zoom_slider);
        center_layout->addWidget(timeline_header);
        center_layout->addWidget(ruler);
        center_layout->addWidget(view);

        QWidget* right_column = new QWidget(this);
        QVBoxLayout* right_layout = new QVBoxLayout(right_column);
        right_layout->setContentsMargins(0, 0, 0, 0);
        right_layout->setSpacing(0);
        QLabel* effects_header = HeaderLabel("Effects", right_column);
        effects_header->setFixedHeight(static_cast<int>(HEADER_HEIGHT));
        right_layout->addWidget(effects_header);
        right_layout->addWidget(effects_list);
        right_column->setFixedWidth(static_cast<int>(SIDE_PANEL_WIDTH));

        track_layout->addWidget(left_column);
        track_layout->addWidget(center_column, 1);

        content_layout->addWidget(track_area, 1);
        content_layout->addSpacing(static_cast<int>(GAP));
        content_layout->addWidget(right_column);

        page_layout->addWidget(toolbar);
        page_layout->addWidget(toolbar_separator);
        page_layout->addWidget(content, 1);

        connect(view->verticalScrollBar(), &QScrollBar::valueChanged, lane_list->verticalScrollBar(), &QScrollBar::setValue);
        connect(lane_list->verticalScrollBar(), &QScrollBar::valueChanged, view->verticalScrollBar(), &QScrollBar::setValue);
        connect(toolbar_music_button, &QPushButton::clicked, this, [this]()
        {
            ChooseMusic();
        });
        connect(toolbar_play_button, &QPushButton::clicked, this, [this]()
        {
            ToggleMusicPlayback();
        });
        connect(zoom_slider, &QSlider::valueChanged, this, [this](int value)
        {
            view->SetHorizontalZoom(value);
        });
        connect(music_timer, &QTimer::timeout, this, [this]()
        {
            UpdateMusicPosition();
        });
        ReloadDevices();
    }

    ~LightTrackPage() override
    {
        CloseMusic();
        if(music_engine_ready)
        {
            ma_engine_uninit(&music_engine);
        }
    }

    void ReloadDevices()
    {
        StopRuntime();
        runtime_targets.clear();
        runtime_zones.clear();

        QVector<LaneEntry> lanes;
        QString empty_message;

        if(resource_manager == nullptr)
        {
            empty_message = "OpenRGB resource manager unavailable";
            SetLanes(lanes, empty_message);
            return;
        }

        std::vector<RGBController*>& controllers = resource_manager->GetRGBControllers();

        if(controllers.empty())
        {
            empty_message = "No devices";
            SetLanes(lanes, empty_message);
            return;
        }

        for(int controller_idx = 0; controller_idx < static_cast<int>(controllers.size()); controller_idx++)
        {
            RGBController* controller = controllers[controller_idx];
            lanes.push_back({QString::fromStdString(controller->GetName()), 0, controller, -1, -1});

            for(int zone_idx = 0; zone_idx < static_cast<int>(controller->zones.size()); zone_idx++)
            {
                const zone& zone_ref = controller->zones[zone_idx];
                const int zone_lane = lanes.size();
                lanes.push_back({QString::fromStdString(zone_ref.name), 1, controller, zone_idx, -1});

                if(zone_ref.segments.empty())
                {
                    AddRuntimeTarget(zone_lane, controller, zone_idx, -1);
                    continue;
                }

                for(int segment_idx = 0; segment_idx < static_cast<int>(zone_ref.segments.size()); segment_idx++)
                {
                    const segment& segment_ref = zone_ref.segments[segment_idx];
                    const int segment_lane = lanes.size();
                    lanes.push_back({QString::fromStdString(segment_ref.name), 2, controller, zone_idx, segment_idx});
                    AddRuntimeTarget(segment_lane, controller, zone_idx, segment_idx);
                }
            }
        }

        SetLanes(lanes, empty_message);
    }

private:
    void AddRuntimeTarget(int lane, RGBController* controller, int zone_idx, int segment_idx)
    {
        runtime_zones.push_back(std::make_unique<ControllerZone>(controller, static_cast<unsigned int>(zone_idx), false, 100, segment_idx >= 0, segment_idx));
        runtime_targets.push_back({lane, runtime_zones.back().get()});
    }

    std::vector<ControllerZone*> AllRuntimeZones() const
    {
        std::vector<ControllerZone*> zones;
        zones.reserve(runtime_targets.size());

        for(const RuntimeTarget& target : runtime_targets)
        {
            if(target.zone != nullptr)
            {
                zones.push_back(target.zone);
            }
        }

        return zones;
    }

    void ForceDirectMode(RGBController* controller) const
    {
        if(controller == nullptr)
        {
            return;
        }

        for(unsigned int i = 0; i < controller->modes.size(); i++)
        {
            if(controller->modes[i].name == "Direct")
            {
                if(controller->GetMode() != static_cast<int>(i))
                {
                    controller->SetMode(i);
                }
                return;
            }
        }

        controller->SetCustomMode();
        controller->UpdateMode();
    }

    void SendBlackToTargets(const std::vector<ControllerZone*>& zones, bool force_mode) const
    {
        std::set<RGBController*> controllers;

        for(ControllerZone* controller_zone : zones)
        {
            if(controller_zone == nullptr || controller_zone->controller == nullptr)
            {
                continue;
            }

            controller_zone->SetAllZoneLEDs(ColorUtils::OFF(), 100, 0, 0);
            controllers.insert(controller_zone->controller);
        }

        for(RGBController* controller : controllers)
        {
            if(force_mode)
            {
                ForceDirectMode(controller);
            }

            controller->UpdateLEDs();
        }
    }

    bool LaneCoversTarget(int lane, int target_lane) const
    {
        if(lane < 0 || target_lane < 0 || lane >= current_lanes.size() || target_lane >= current_lanes.size() || lane > target_lane)
        {
            return false;
        }

        if(lane == target_lane)
        {
            return true;
        }

        const int ancestor_level = current_lanes[lane].level;
        if(current_lanes[target_lane].level <= ancestor_level)
        {
            return false;
        }

        for(int i = lane + 1; i <= target_lane; i++)
        {
            if(current_lanes[i].level <= ancestor_level)
            {
                return false;
            }
        }

        return true;
    }

    RGBEffect* CreateRuntimeEffect(const EffectDefinition& definition) const
    {
        std::function<RGBEffect*()> constructor = EffectListManager::get()->GetEffectConstructor(definition.id.toStdString());
        if(!constructor)
        {
            return nullptr;
        }

        RGBEffect* effect = constructor();
        if(effect == nullptr)
        {
            return nullptr;
        }

        effect->hide();
        effect->SetFPS(OpenRGBEffectSettings::globalSettings.fps);
        effect->SetBrightness(OpenRGBEffectSettings::globalSettings.brightness);
        effect->SetTemperature(OpenRGBEffectSettings::globalSettings.temperature);
        effect->SetTint(OpenRGBEffectSettings::globalSettings.tint);

        std::vector<RGBColor> initial_colors;
        for(unsigned int i = 0; i < effect->EffectDetails.UserColors; i++)
        {
            if(OpenRGBEffectSettings::globalSettings.use_prefered_colors && i < OpenRGBEffectSettings::globalSettings.prefered_colors.size())
            {
                initial_colors.push_back(OpenRGBEffectSettings::globalSettings.prefered_colors[i]);
            }
            else
            {
                initial_colors.push_back(ColorUtils::RandomRGBColor());
            }
        }

        effect->SetUserColors(initial_colors);
        effect->SetRandomColorsEnabled(OpenRGBEffectSettings::globalSettings.prefer_random);
        return effect;
    }

    RGBEffect* EnsureRuntimeEffect(const TimelineClipState& clip)
    {
        auto found = runtime_effects.find(clip.clip);
        if(found != runtime_effects.end())
        {
            return found->second;
        }

        RGBEffect* effect = CreateRuntimeEffect(clip.effect);
        if(effect != nullptr)
        {
            runtime_effects[clip.clip] = effect;
        }

        return effect;
    }

    void DestroyRuntimeEffect(RGBEffect* effect) const
    {
        if(effect == nullptr)
        {
            return;
        }

        EffectManager* manager = EffectManager::Get();
        if(manager->IsActive(effect))
        {
            manager->SetEffectUnActive(effect);
        }

        manager->RemoveMapping(effect);
        delete effect;
    }

    void StartRuntime(qint64 position_ms)
    {
        if(runtime_running)
        {
            SyncRuntime(position_ms);
            return;
        }

        runtime_running = true;
        SendBlackToTargets(AllRuntimeZones(), true);
        SyncRuntime(position_ms);
    }

    void StopRuntime()
    {
        const bool was_running = runtime_running || !runtime_effects.empty();

        for(auto& runtime_effect : runtime_effects)
        {
            DestroyRuntimeEffect(runtime_effect.second);
        }

        runtime_effects.clear();
        runtime_assignments.clear();
        runtime_running = false;

        if(was_running)
        {
            SendBlackToTargets(AllRuntimeZones(), true);
        }
    }

    void SyncRuntime(qint64 position_ms)
    {
        if(!runtime_running)
        {
            return;
        }

        const QVector<TimelineClipState> clips = view->TimelineClips();
        std::map<const TimelineClipItem*, TimelineClipState> assigned_clips;
        std::map<const TimelineClipItem*, std::vector<ControllerZone*>> assignments;

        for(const RuntimeTarget& target : runtime_targets)
        {
            if(target.zone == nullptr)
            {
                continue;
            }

            const TimelineClipState* best_clip = nullptr;
            int best_level = -1;

            for(const TimelineClipState& clip : clips)
            {
                if(clip.start_ms > position_ms || position_ms >= clip.end_ms || !LaneCoversTarget(clip.lane, target.lane))
                {
                    continue;
                }

                const int level = current_lanes[clip.lane].level;
                if(level >= best_level)
                {
                    best_clip = &clip;
                    best_level = level;
                }
            }

            if(best_clip != nullptr)
            {
                assignments[best_clip->clip].push_back(target.zone);
                assigned_clips[best_clip->clip] = *best_clip;
            }
        }

        for(auto effect_it = runtime_effects.begin(); effect_it != runtime_effects.end();)
        {
            if(assignments.find(effect_it->first) == assignments.end())
            {
                DestroyRuntimeEffect(effect_it->second);
                runtime_assignments.erase(effect_it->first);
                effect_it = runtime_effects.erase(effect_it);
            }
            else
            {
                ++effect_it;
            }
        }

        std::set<ControllerZone*> active_targets;
        EffectManager* manager = EffectManager::Get();

        for(auto& assignment : assignments)
        {
            RGBEffect* effect = EnsureRuntimeEffect(assigned_clips[assignment.first]);
            if(effect == nullptr)
            {
                continue;
            }

            auto previous = runtime_assignments.find(assignment.first);
            if(previous == runtime_assignments.end() || previous->second != assignment.second)
            {
                manager->Assign(assignment.second, effect);
                runtime_assignments[assignment.first] = assignment.second;
            }

            if(!manager->IsActive(effect))
            {
                manager->SetEffectActive(effect);
            }

            active_targets.insert(assignment.second.begin(), assignment.second.end());
        }

        std::vector<ControllerZone*> inactive_targets;
        for(const RuntimeTarget& target : runtime_targets)
        {
            if(target.zone != nullptr && active_targets.find(target.zone) == active_targets.end())
            {
                inactive_targets.push_back(target.zone);
            }
        }

        SendBlackToTargets(inactive_targets, false);
    }

    void ChooseMusic()
    {
        const QString path = QFileDialog::getOpenFileName(this, "Select music", QString(),
            "Audio Files (*.wav *.mp3 *.flac *.ogg);;All Files (*.*)");

        if(path.isEmpty())
        {
            return;
        }

        CloseMusic();
        music_path = path;
        music_spectrum_preview.clear();
        view->SetMusicPosition(0);

        const QFileInfo file_info(path);
        toolbar_music_button->setText(file_info.fileName());
        toolbar_music_button->setToolTip(path);
        UpdateToolbarSideWidths();

        if(!OpenMusic(path))
        {
            toolbar_music_button->setToolTip(path + "\nCannot play this file");
            view->SetMusicSpectrum({}, 0);
            return;
        }

        music_spectrum_preview = BuildMusicSpectrumPreview(path);
        view->SetMusicSpectrum(music_spectrum_preview, music_duration_ms);
        SetPlaybackControls(true, false);
    }

    bool OpenMusic(const QString& path)
    {
        if(path.isEmpty())
        {
            return false;
        }

        if(!music_engine_ready)
        {
            music_engine_ready = ma_engine_init(nullptr, &music_engine) == MA_SUCCESS;
            if(!music_engine_ready)
            {
                return false;
            }
        }

        const ma_uint32 sound_flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
#ifdef _WIN32
        const std::wstring sound_path = QDir::toNativeSeparators(path).toStdWString();
        const ma_result init_result = ma_sound_init_from_file_w(&music_engine, sound_path.c_str(),
            sound_flags, nullptr, nullptr, &music_sound);
#else
        const QByteArray sound_path = path.toLocal8Bit();
        const ma_result init_result = ma_sound_init_from_file(&music_engine, sound_path.constData(),
            sound_flags, nullptr, nullptr, &music_sound);
#endif

        if(init_result != MA_SUCCESS)
        {
            return false;
        }

        music_sound_ready = true;

        float duration_seconds = 0.0f;
        if(ma_sound_get_length_in_seconds(&music_sound, &duration_seconds) == MA_SUCCESS)
        {
            music_duration_ms = qMax<qint64>(0, static_cast<qint64>(duration_seconds * 1000.0f + 0.5f));
        }
        else
        {
            music_duration_ms = 0;
        }

        music_loaded = true;
        return true;
    }

    void ToggleMusicPlayback()
    {
        if(!music_loaded)
        {
            return;
        }

        if(music_playing)
        {
            ma_sound_stop(&music_sound);
            music_timer->stop();
            music_playing = false;
            SetPlaybackControls(true, false);
            UpdateMusicPosition();
            return;
        }

        if(music_duration_ms > 0 && MusicPositionMs() >= music_duration_ms - 20)
        {
            ma_sound_seek_to_pcm_frame(&music_sound, 0);
            view->SetMusicPosition(0);
        }

        if(ma_sound_start(&music_sound) != MA_SUCCESS)
        {
            MarkMusicError();
            return;
        }

        music_timer->start();
        music_playing = true;
        SetPlaybackControls(true, true);
        StartRuntime(MusicPositionMs());
    }

    void SeekMusic(qint64 position_ms)
    {
        if(!music_loaded)
        {
            return;
        }

        const qint64 clamped_position = music_duration_ms > 0 ?
            qBound<qint64>(0, position_ms, music_duration_ms) :
            qMax<qint64>(0, position_ms);

        if(ma_sound_seek_to_second(&music_sound, static_cast<float>(clamped_position) / 1000.0f) != MA_SUCCESS)
        {
            MarkMusicError();
            return;
        }

        view->SetMusicPosition(clamped_position);

        if(music_playing)
        {
            StartRuntime(clamped_position);
        }
    }

    void UpdateMusicPosition()
    {
        if(!music_loaded)
        {
            return;
        }

        if(music_playing && ma_sound_at_end(&music_sound))
        {
            FinishMusicPlayback();
            return;
        }

        const qint64 position = MusicPositionMs();

        if(music_duration_ms > 0 && position >= music_duration_ms)
        {
            FinishMusicPlayback();
            return;
        }

        view->SetMusicPosition(position);

        if(music_playing)
        {
            SyncRuntime(position);
        }
        else
        {
            StopRuntime();
        }
    }

    qint64 MusicPositionMs() const
    {
        if(!music_sound_ready)
        {
            return 0;
        }

        float position_seconds = 0.0f;
        if(ma_sound_get_cursor_in_seconds(&music_sound, &position_seconds) != MA_SUCCESS)
        {
            return 0;
        }

        return qMax<qint64>(0, static_cast<qint64>(position_seconds * 1000.0f + 0.5f));
    }

    void CloseMusic()
    {
        StopRuntime();
        if(music_timer != nullptr)
        {
            music_timer->stop();
        }
        music_spectrum_preview.clear();

        if(music_sound_ready)
        {
            ma_sound_stop(&music_sound);
            ma_sound_uninit(&music_sound);
            music_sound_ready = false;
        }

        music_loaded = false;
        music_playing = false;
        music_duration_ms = 0;

        SetPlaybackControls(false, false);
    }

    void FinishMusicPlayback()
    {
        if(music_timer != nullptr)
        {
            music_timer->stop();
        }
        if(music_sound_ready)
        {
            ma_sound_stop(&music_sound);
        }
        music_playing = false;
        SetPlaybackControls(true, false);
        view->SetMusicPosition(music_duration_ms);
        StopRuntime();
    }

    void MarkMusicError()
    {
        if(!music_loaded)
        {
            return;
        }

        StopRuntime();
        if(music_timer != nullptr)
        {
            music_timer->stop();
        }

        music_loaded = false;
        music_playing = false;
        music_duration_ms = 0;
        music_spectrum_preview.clear();
        if(music_sound_ready)
        {
            ma_sound_uninit(&music_sound);
            music_sound_ready = false;
        }
        view->SetMusicSpectrum({}, 0);

        SetPlaybackControls(false, false);

        if(toolbar_music_button != nullptr)
        {
            toolbar_music_button->setToolTip(music_path + "\nCannot play this file");
        }
    }

    void SetLanes(const QVector<LaneEntry>& lanes, const QString& empty_message)
    {
        current_lanes = lanes;
        lane_list->SetLanes(lanes);
        view->SetLanes(lanes, empty_message);
        view->SetVisibleLanes(lane_list->VisibleLaneIndices());
    }

    void UpdateToolbarSideWidths()
    {
        if(toolbar_left_group == nullptr || toolbar_right_group == nullptr || toolbar_music_button == nullptr)
        {
            return;
        }

        const int icon_group_width = static_cast<int>(TOOLBAR_HEIGHT * 3.0);
        const int side_width = qMax(icon_group_width, toolbar_music_button->sizeHint().width());
        toolbar_left_group->setFixedWidth(side_width);
        toolbar_right_group->setFixedWidth(side_width);
    }

    void SetPlaybackControls(bool enabled, bool playing)
    {
        if(toolbar_play_button != nullptr)
        {
            const ushort codepoint = playing ? 0xE12E : 0xE13C;
            toolbar_play_button->setEnabled(enabled);
            toolbar_play_button->setText(QString(QChar(codepoint)));
        }
    }

    ResourceManagerInterface* resource_manager;
    LaneListWidget* lane_list;
    TimelineRulerWidget* ruler;
    LightTrackView* view;
    EffectsListWidget* effects_list;
    QWidget* toolbar_left_group = nullptr;
    QWidget* toolbar_right_group = nullptr;
    QPushButton* toolbar_music_button = nullptr;
    QPushButton* toolbar_play_button = nullptr;
    QTimer* music_timer = nullptr;
    ma_engine music_engine;
    ma_sound music_sound;
    QString music_path;
    qint64 music_duration_ms = 0;
    bool music_engine_ready = false;
    bool music_sound_ready = false;
    bool music_loaded = false;
    bool music_playing = false;
    QVector<qreal> music_spectrum_preview;
    QVector<LaneEntry> current_lanes;
    std::vector<std::unique_ptr<ControllerZone>> runtime_zones;
    QVector<RuntimeTarget> runtime_targets;
    std::map<const TimelineClipItem*, RGBEffect*> runtime_effects;
    std::map<const TimelineClipItem*, std::vector<ControllerZone*>> runtime_assignments;
    bool runtime_running = false;
};
}

OpenRGBPluginInfo LightTrackPlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;
    info.Name        = "LightTrack Plugin";
    info.Description = "LightTrack integration for OpenRGB";
    info.Version     = "0.1.0";
    info.Location    = OPENRGB_PLUGIN_LOCATION_TOP;
    info.Label       = "LightTrack";
    return info;
}

unsigned int LightTrackPlugin::GetPluginAPIVersion()
{
    return OPENRGB_PLUGIN_API_VERSION;
}

void LightTrackPlugin::Load(ResourceManagerInterface* resource_manager_ptr)
{
    resource_manager = resource_manager_ptr;
    InitializeOpenRGBEffectsRuntime(resource_manager_ptr);
}

QWidget* LightTrackPlugin::GetWidget()
{
    if(page != nullptr)
    {
        return page;
    }

    if(resource_manager != nullptr)
    {
        resource_manager->WaitForDeviceDetection();
    }

    page = new LightTrackPage(resource_manager);

    if(resource_manager != nullptr)
    {
        resource_manager->RegisterDeviceListChangeCallback(DeviceListChangedCallback, page);
        resource_manager->RegisterDetectionProgressCallback(DeviceListChangedCallback, page);
    }

    return page;
}

QMenu* LightTrackPlugin::GetTrayMenu()
{
    return nullptr;
}

void LightTrackPlugin::Unload()
{
    if(resource_manager != nullptr && page != nullptr)
    {
        resource_manager->UnregisterDeviceListChangeCallback(DeviceListChangedCallback, page);
        resource_manager->UnregisterDetectionProgressCallback(DeviceListChangedCallback, page);
    }

    page = nullptr;
    resource_manager = nullptr;
    ShutdownOpenRGBEffectsRuntime();
}

void LightTrackPlugin::DeviceListChangedCallback(void* ptr)
{
    LightTrackPage* light_track_page = static_cast<LightTrackPage*>(ptr);

    QMetaObject::invokeMethod(light_track_page, [light_track_page]()
    {
        light_track_page->ReloadDevices();
    }, Qt::QueuedConnection);
}
