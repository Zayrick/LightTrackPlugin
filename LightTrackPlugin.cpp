#include "LightTrackPlugin.h"

#include "ResourceManagerInterface.h"
#include "RGBController/RGBController.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QByteArray>
#include <QColor>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMetaObject>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSize>
#include <QSizePolicy>
#include <QStyleOptionGraphicsItem>
#include <QTimer>
#include <QTransform>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>
#include <QDir>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "miniaudio.h"

#ifdef _WIN32
#include <qt_windows.h>
#include <mmsystem.h>
#endif

namespace
{
const char* EFFECT_MIME = "application/x-lighttrack-effect";

const qreal PAGE_MARGIN = 10.0;
const qreal GAP = 8.0;
const qreal HEADER_HEIGHT = 42.0;
const qreal RULER_HEIGHT = 30.0;
const qreal LABEL_WIDTH = 238.0;
const qreal ROW_HEIGHT = 52.0;
const qreal TIMELINE_MIN_WIDTH = 900.0;
const qreal SIDE_PANEL_WIDTH = 178.0;
const qreal CARD_HEIGHT = 42.0;
const qreal CLIP_HEIGHT = 32.0;
const qreal CLIP_MIN_WIDTH = 72.0;
const qreal CLIP_DEFAULT_WIDTH = 130.0;
const qreal GRID_WIDTH = 80.0;
const qreal RESIZE_HANDLE_WIDTH = 9.0;
const qreal CLIP_DRAG_HANDLE_WIDTH = 22.0;
const qreal REMOVE_BUTTON_SIZE = 18.0;
const qreal REMOVE_BUTTON_RIGHT_MARGIN = RESIZE_HANDLE_WIDTH + 8.0;
const qreal PLAYHEAD_HANDLE_RADIUS = 6.0;
const int MUSIC_SPECTRUM_BARS = 1200;

struct EffectDefinition
{
    QString name;
    QColor color;
};

struct LaneInfo
{
    QString name;
    QRectF rect;
};

QVector<EffectDefinition> EffectDefinitions()
{
    QVector<EffectDefinition> effects;
    effects.push_back({"Solid", QColor("#2f80ed")});
    effects.push_back({"Fade", QColor("#27ae60")});
    effects.push_back({"Wave", QColor("#f2994a")});
    effects.push_back({"Blink", QColor("#eb5757")});
    return effects;
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

bool FindEffect(const QString& name, EffectDefinition* effect)
{
    for(const EffectDefinition& candidate : EffectDefinitions())
    {
        if(candidate.name == name)
        {
            if(effect != nullptr)
            {
                *effect = candidate;
            }
            return true;
        }
    }

    return false;
}

QString ZoneTypeName(zone_type type)
{
    if(type == ZONE_TYPE_MATRIX)
    {
        return "matrix";
    }

    if(type == ZONE_TYPE_LINEAR)
    {
        return "linear";
    }

    return "single";
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

    void SetPreview(qreal clip_width, QColor preview_color)
    {
        prepareGeometryChange();
        width = clip_width;
        color = preview_color;
        update();
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setBrush(WithAlpha(color, 48));
        painter->setPen(QPen(WithAlpha(color.darker(110), 145), 1.0, Qt::DashLine));
        painter->drawRoundedRect(QRectF(0.0, 0.0, width, CLIP_HEIGHT), 5.0, 5.0);
    }

private:
    qreal width = CLIP_DEFAULT_WIDTH;
    QColor color = QColor("#888888");
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
        painter->setPen(QPen(QColor(64, 188, 255, 92), 1.0));

        const qreal center_y = height / 2.0;
        const qreal step = width / levels.size();

        for(int i = 0; i < levels.size(); i++)
        {
            const qreal x = i * step;
            const qreal bar_height = qMax<qreal>(2.0, levels[i] * height * 0.42);
            painter->drawLine(QPointF(x, center_y - bar_height), QPointF(x, center_y + bar_height));
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

    void SetLanes(const QVector<QString>& names, const QString& empty_text)
    {
        CancelDrag();
        ClearClips();
        lane_names = names;
        empty_message = empty_text;
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

    qreal ContentWidth() const
    {
        return sceneRect().width();
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

        const qreal x = ClampClipX(lane, pos.x() - CLIP_DEFAULT_WIDTH / 2.0, CLIP_DEFAULT_WIDTH);
        ShowPreview(lane, x, CLIP_DEFAULT_WIDTH, effect.color);
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

        AddClip(effect, lane, pos.x() - CLIP_DEFAULT_WIDTH / 2.0, CLIP_DEFAULT_WIDTH);
        return true;
    }

    void ClearPreview()
    {
        HidePreview();
    }

protected:
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

        const int lane_count = qMax(1, lane_names.size());
        const qreal content_height = ROW_HEIGHT + lane_count * ROW_HEIGHT;
        const qreal scene_width = qMax(qMax<qreal>(TIMELINE_MIN_WIDTH, viewport_size.width()), MusicPixelWidth());
        const qreal scene_height = qMax<qreal>(content_height, viewport_size.height());

        setSceneRect(0.0, 0.0, scene_width, scene_height);
        Track(addRect(sceneRect(), Qt::NoPen, QBrush(palette.color(QPalette::Base))));

        if(lane_names.empty())
        {
            AddText(empty_message, 12.0, ROW_HEIGHT + 14.0, scene_width - 24.0, TextLineColor(palette, 160, 170), 10, false);
        }
        else
        {
            for(int i = 0; i < lane_names.size(); i++)
            {
                DrawLane(i, lane_names[i]);
            }
        }

        AddMusicItems();
        RelayoutClips();
    }

    QGraphicsTextItem* AddText(const QString& text, qreal x, qreal y, qreal width, const QColor& color, int point_size, bool bold)
    {
        QFont font;
        font.setPointSize(point_size);
        font.setBold(bold);

        QGraphicsTextItem* item = addText(text, font);
        item->setDefaultTextColor(color);
        item->setTextWidth(width);
        item->setPos(x, y);
        item->setZValue(5.0);
        return Track(item);
    }

    void DrawLane(int index, const QString& name)
    {
        const qreal y = ROW_HEIGHT + index * ROW_HEIGHT;
        QColor row_color = (index % 2 == 0) ? palette.color(QPalette::Base) : palette.color(QPalette::AlternateBase);

        if(!row_color.isValid() || row_color == palette.color(QPalette::Base))
        {
            row_color = (index % 2 == 0) ? palette.color(QPalette::Base) : palette.color(QPalette::Window);
        }

        const QRectF lane_rect(0.0, y, sceneRect().width(), ROW_HEIGHT);
        Track(addRect(lane_rect, QPen(TextLineColor(palette, 38, 72)), QBrush(row_color)));

        for(qreal x = GRID_WIDTH; x < lane_rect.width(); x += GRID_WIDTH)
        {
            Track(addLine(x, y + 1.0, x, y + ROW_HEIGHT - 1.0, QPen(TextLineColor(palette, 20, 38))));
        }

        lanes.push_back({name, lane_rect});
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
            const qreal x = ClampClipX(lane, clip->pos().x(), clip->ClipWidth());
            clip->setPos(x, ClipY(lane));
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

        return music_duration_ms * GRID_WIDTH / 1000.0;
    }

    void AddMusicItems()
    {
        const qreal music_width = MusicPixelWidth();
        const QRectF row_rect(0.0, 0.0, sceneRect().width(), ROW_HEIGHT);
        QColor row_color = palette.color(QPalette::Window);

        if(!row_color.isValid())
        {
            row_color = palette.color(QPalette::Base);
        }

        Track(addRect(row_rect, QPen(TextLineColor(palette, 52, 82)), QBrush(row_color)));

        for(qreal x = GRID_WIDTH; x < row_rect.width(); x += GRID_WIDTH)
        {
            Track(addLine(x, 1.0, x, ROW_HEIGHT - 1.0, QPen(TextLineColor(palette, 24, 42))));
        }

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

        const qreal x = qBound<qreal>(0.0, music_position_ms * GRID_WIDTH / 1000.0, sceneRect().right());
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

        const qint64 position_ms = qBound<qint64>(0, static_cast<qint64>(x * 1000.0 / GRID_WIDTH), music_duration_ms);
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
        ShowPreview(target_lane, x, width, active_clip->Effect().color);
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
        ShowPreview(lane, clip_start_x, width, active_clip->Effect().color);
    }

    void ShowPreview(int lane, qreal x, qreal width, const QColor& color)
    {
        if(preview == nullptr)
        {
            preview = new ClipPreviewItem();
            addItem(preview);
        }

        preview->SetPreview(width, color);
        preview->setPos(x, ClipY(lane));
        preview->setVisible(true);
        last_preview_lane = lane;
        last_preview_x = x;
    }

    void HidePreview()
    {
        if(preview != nullptr)
        {
            preview->setVisible(false);
        }

        last_preview_lane = -1;
        last_preview_x = 0.0;
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
    QVector<QString> lane_names;
    QVector<LaneInfo> lanes;
    QVector<QGraphicsItem*> layout_items;
    QVector<TimelineClipItem*> clips;
    QString empty_message;
    QVector<qreal> music_spectrum;
    std::function<void(qint64)> music_seek_callback;
    qint64 music_duration_ms = 0;
    qint64 music_position_ms = 0;

    DragMode drag_mode = NoDrag;
    QPointF drag_offset;
    QPointF drag_scene_start;
    qreal clip_start_x = 0.0;
    qreal clip_start_width = CLIP_DEFAULT_WIDTH;
    int last_preview_lane = -1;
    qreal last_preview_x = 0.0;
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

        for(qreal x = 0.0; x <= content_width; x += GRID_WIDTH)
        {
            const qreal view_x = x - horizontal_offset;

            if(view_x < -GRID_WIDTH || view_x > width() + GRID_WIDTH)
            {
                continue;
            }

            painter.setPen(QPen(line_color));
            painter.drawLine(QPointF(view_x, 12.0), QPointF(view_x, base_y));
            painter.setPen(text_color);
            painter.drawText(QRectF(view_x + 4.0, 0.0, 48.0, height() - 8.0),
                Qt::AlignVCenter | Qt::AlignLeft, QString::number(static_cast<int>(x / GRID_WIDTH)) + "s");
        }
    }

private:
    qreal content_width = TIMELINE_MIN_WIDTH;
    int horizontal_offset = 0;
};

class LaneListWidget : public QListWidget
{
public:
    explicit LaneListWidget(QWidget* parent = nullptr) :
        QListWidget(parent)
    {
        setAlternatingRowColors(true);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSelectionMode(QAbstractItemView::NoSelection);
        setUniformItemSizes(true);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    }

    void SetLanes(const QVector<QString>& lanes)
    {
        clear();

        QListWidgetItem* music_item = new QListWidgetItem("Music");
        music_item->setFlags(Qt::ItemIsEnabled);
        music_item->setSizeHint(QSize(0, static_cast<int>(ROW_HEIGHT)));
        music_item->setToolTip("Music");
        addItem(music_item);

        for(const QString& lane : lanes)
        {
            QListWidgetItem* item = new QListWidgetItem(lane);
            item->setFlags(Qt::ItemIsEnabled);
            item->setSizeHint(QSize(0, static_cast<int>(ROW_HEIGHT)));
            item->setToolTip(lane);
            addItem(item);
        }
    }
};

class EffectsListWidget : public QListWidget
{
public:
    explicit EffectsListWidget(QWidget* parent = nullptr) :
        QListWidget(parent)
    {
        setDragEnabled(true);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setSpacing(8);
        setUniformItemSizes(true);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

        for(const EffectDefinition& effect : EffectDefinitions())
        {
            QListWidgetItem* item = new QListWidgetItem(effect.name);
            item->setBackground(effect.color);
            item->setData(Qt::UserRole, effect.name.toUtf8());
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
            item->setForeground(Qt::white);
            item->setSizeHint(QSize(0, static_cast<int>(CARD_HEIGHT)));
            addItem(item);
        }
    }

protected:
    void startDrag(Qt::DropActions) override
    {
        QListWidgetItem* item = currentItem();
        if(item == nullptr)
        {
            return;
        }

        QMimeData* mime_data = new QMimeData();
        mime_data->setData(EFFECT_MIME, item->data(Qt::UserRole).toByteArray());

        QDrag* drag = new QDrag(this);
        drag->setMimeData(mime_data);
        drag->exec(Qt::CopyAction);
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
        setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);

        light_scene->SetPalette(palette());
        light_scene->SetViewportSize(viewport()->size());

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

    void SetLanes(const QVector<QString>& lanes, const QString& empty_text)
    {
        light_scene->SetLanes(lanes, empty_text);
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

    void SetMusicSeekCallback(std::function<void(qint64)> callback)
    {
        light_scene->SetMusicSeekCallback(std::move(callback));
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
        QHBoxLayout* page_layout = new QHBoxLayout(this);
        page_layout->setContentsMargins(static_cast<int>(PAGE_MARGIN), static_cast<int>(PAGE_MARGIN),
            static_cast<int>(PAGE_MARGIN), static_cast<int>(PAGE_MARGIN));
        page_layout->setSpacing(static_cast<int>(GAP));

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

        QWidget* left_column = new QWidget(this);
        QVBoxLayout* left_layout = new QVBoxLayout(left_column);
        left_layout->setContentsMargins(0, 0, 0, 0);
        left_layout->setSpacing(0);
        QLabel* left_header = HeaderLabel("Devices / Zones", left_column);
        left_header->setFixedHeight(static_cast<int>(HEADER_HEIGHT + RULER_HEIGHT));
        left_layout->addWidget(left_header);
        left_layout->addWidget(lane_list);
        left_column->setFixedWidth(static_cast<int>(LABEL_WIDTH));

        QWidget* center_column = new QWidget(this);
        QVBoxLayout* center_layout = new QVBoxLayout(center_column);
        center_layout->setContentsMargins(0, 0, 0, 0);
        center_layout->setSpacing(0);
        QWidget* timeline_header = new QWidget(center_column);
        timeline_header->setFixedHeight(static_cast<int>(HEADER_HEIGHT));
        QHBoxLayout* timeline_header_layout = new QHBoxLayout(timeline_header);
        timeline_header_layout->setContentsMargins(0, 0, 0, 0);
        timeline_header_layout->setSpacing(6);
        QLabel* timeline_title = HeaderLabel("Timeline", timeline_header);
        music_label = new QLabel("No music", timeline_header);
        music_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        music_label->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
        choose_music_button = new QPushButton("Music...", timeline_header);
        play_button = new QPushButton("Play", timeline_header);
        play_button->setEnabled(false);
        timeline_header_layout->addWidget(timeline_title);
        timeline_header_layout->addWidget(music_label, 1);
        timeline_header_layout->addWidget(choose_music_button);
        timeline_header_layout->addWidget(play_button);
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

        page_layout->addWidget(left_column);
        page_layout->addWidget(center_column, 1);
        page_layout->addWidget(right_column);

        connect(view->verticalScrollBar(), &QScrollBar::valueChanged, lane_list->verticalScrollBar(), &QScrollBar::setValue);
        connect(lane_list->verticalScrollBar(), &QScrollBar::valueChanged, view->verticalScrollBar(), &QScrollBar::setValue);
        connect(choose_music_button, &QPushButton::clicked, this, [this]()
        {
            ChooseMusic();
        });
        connect(play_button, &QPushButton::clicked, this, [this]()
        {
            ToggleMusicPlayback();
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
    }

    void ReloadDevices()
    {
        QVector<QString> lanes;
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
            lanes.push_back(QString::fromStdString(controller->GetName()));

            for(int zone_idx = 0; zone_idx < static_cast<int>(controller->zones.size()); zone_idx++)
            {
                const zone& zone_ref = controller->zones[zone_idx];
                lanes.push_back(QString("%1 (%2, %3 LEDs)")
                    .arg(QString::fromStdString(zone_ref.name))
                    .arg(ZoneTypeName(zone_ref.type))
                    .arg(zone_ref.leds_count));

                for(int segment_idx = 0; segment_idx < static_cast<int>(zone_ref.segments.size()); segment_idx++)
                {
                    const segment& segment_ref = zone_ref.segments[segment_idx];
                    lanes.push_back(QString("%1 (%2 LEDs)")
                        .arg(QString::fromStdString(segment_ref.name))
                        .arg(segment_ref.leds_count));
                }
            }
        }

        SetLanes(lanes, empty_message);
    }

private:
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
        play_button->setText("Play");
        play_button->setEnabled(false);
        view->SetMusicPosition(0);

        const QFileInfo file_info(path);
        const QVector<qreal> spectrum = BuildMusicSpectrumPreview(path);
        music_label->setText(file_info.fileName());
        music_label->setToolTip(path);

        if(!OpenMusic(path))
        {
            music_label->setText(file_info.fileName() + " (cannot play)");
            view->SetMusicSpectrum(spectrum, 0);
            return;
        }

        view->SetMusicSpectrum(spectrum, music_duration_ms);
        play_button->setEnabled(true);
    }

    bool OpenMusic(const QString& path)
    {
#ifdef _WIN32
        music_alias = QString("lighttrack_music_%1").arg(reinterpret_cast<quintptr>(this), 0, 16);
        QString native_path = QDir::toNativeSeparators(path);
        native_path.remove('"');

        if(!Mci(QString("open \"%1\" alias %2").arg(native_path, music_alias)))
        {
            music_alias.clear();
            return false;
        }

        Mci(QString("set %1 time format milliseconds").arg(music_alias));
        music_duration_ms = MciNumber(QString("status %1 length").arg(music_alias));

        if(music_duration_ms <= 0)
        {
            CloseMusic();
            return false;
        }

        music_loaded = true;
        return true;
#else
        Q_UNUSED(path);
        music_duration_ms = 0;
        music_loaded = false;
        return false;
#endif
    }

    void ToggleMusicPlayback()
    {
        if(!music_loaded)
        {
            return;
        }

#ifdef _WIN32
        if(music_playing)
        {
            Mci(QString("pause %1").arg(music_alias));
            music_timer->stop();
            music_playing = false;
            play_button->setText("Play");
            UpdateMusicPosition();
            return;
        }

        if(music_duration_ms > 0 && MusicPosition() >= music_duration_ms - 20)
        {
            Mci(QString("seek %1 to start").arg(music_alias));
            view->SetMusicPosition(0);
        }

        if(Mci(QString("play %1").arg(music_alias)))
        {
            music_timer->start();
            music_playing = true;
            play_button->setText("Pause");
        }
#endif
    }

    void SeekMusic(qint64 position_ms)
    {
        if(!music_loaded)
        {
            return;
        }

#ifdef _WIN32
        const qint64 clamped_position = qBound<qint64>(0, position_ms, music_duration_ms);
        Mci(QString("seek %1 to %2").arg(music_alias).arg(clamped_position));

        if(music_playing)
        {
            Mci(QString("play %1").arg(music_alias));
        }

        view->SetMusicPosition(clamped_position);
#else
        Q_UNUSED(position_ms);
#endif
    }

    void UpdateMusicPosition()
    {
        if(!music_loaded)
        {
            return;
        }

#ifdef _WIN32
        qint64 position = MusicPosition();
        const bool finished = music_playing && MciText(QString("status %1 mode").arg(music_alias)) == "stopped";

        if(finished || (music_duration_ms > 0 && position >= music_duration_ms))
        {
            position = music_duration_ms;
            music_timer->stop();
            music_playing = false;
            play_button->setText("Play");
        }

        view->SetMusicPosition(position);
#endif
    }

    void CloseMusic()
    {
        if(music_timer != nullptr)
        {
            music_timer->stop();
        }

#ifdef _WIN32
        if(!music_alias.isEmpty())
        {
            Mci(QString("stop %1").arg(music_alias));
            Mci(QString("close %1").arg(music_alias));
        }
#endif

        music_alias.clear();
        music_loaded = false;
        music_playing = false;
        music_duration_ms = 0;

        if(play_button != nullptr)
        {
            play_button->setText("Play");
            play_button->setEnabled(false);
        }
    }

#ifdef _WIN32
    bool Mci(const QString& command, QString* result = nullptr) const
    {
        wchar_t buffer[256] = {};
        const MCIERROR error = mciSendStringW(reinterpret_cast<LPCWSTR>(command.utf16()),
            result == nullptr ? nullptr : buffer, result == nullptr ? 0 : 256, nullptr);

        if(error != 0)
        {
            return false;
        }

        if(result != nullptr)
        {
            *result = QString::fromWCharArray(buffer).trimmed();
        }

        return true;
    }

    QString MciText(const QString& command) const
    {
        QString result;
        Mci(command, &result);
        return result;
    }

    qint64 MciNumber(const QString& command) const
    {
        bool ok = false;
        const qint64 value = MciText(command).toLongLong(&ok);
        return ok ? value : 0;
    }

    qint64 MusicPosition() const
    {
        return MciNumber(QString("status %1 position").arg(music_alias));
    }
#endif

    void SetLanes(const QVector<QString>& lanes, const QString& empty_message)
    {
        lane_list->SetLanes(lanes);
        view->SetLanes(lanes, empty_message);
    }

    ResourceManagerInterface* resource_manager;
    LaneListWidget* lane_list;
    TimelineRulerWidget* ruler;
    LightTrackView* view;
    EffectsListWidget* effects_list;
    QPushButton* choose_music_button = nullptr;
    QPushButton* play_button = nullptr;
    QLabel* music_label = nullptr;
    QTimer* music_timer = nullptr;
    QString music_path;
    QString music_alias;
    qint64 music_duration_ms = 0;
    bool music_loaded = false;
    bool music_playing = false;
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
}

void LightTrackPlugin::DeviceListChangedCallback(void* ptr)
{
    LightTrackPage* light_track_page = static_cast<LightTrackPage*>(ptr);

    QMetaObject::invokeMethod(light_track_page, [light_track_page]()
    {
        light_track_page->ReloadDevices();
    }, Qt::QueuedConnection);
}
