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
#include <QFrame>
#include <QGraphicsItem>
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
#include <QResizeEvent>
#include <QScrollBar>
#include <QSize>
#include <QStyleOptionGraphicsItem>
#include <QTransform>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

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
const qreal CLIP_MIN_WIDTH = 56.0;
const qreal CLIP_DEFAULT_WIDTH = 130.0;
const qreal GRID_WIDTH = 80.0;
const qreal RESIZE_HANDLE_WIDTH = 9.0;

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

        painter->setPen(Qt::white);
        QFont font = painter->font();
        font.setBold(true);
        painter->setFont(font);

        const qreal close_space = removable ? 28.0 : 12.0;
        const QRectF text_rect = QRectF(9.0, 0.0, qMax<qreal>(0.0, width - close_space), CLIP_HEIGHT);
        const QString label = QFontMetrics(font).elidedText(effect.name, Qt::ElideRight, static_cast<int>(text_rect.width()));
        painter->drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft, label);

        if(removable)
        {
            const QRectF close_rect = RemoveRect();
            painter->setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(close_rect.topLeft() + QPointF(4.0, 4.0), close_rect.bottomRight() - QPointF(4.0, 4.0));
            painter->drawLine(close_rect.topRight() + QPointF(-4.0, 4.0), close_rect.bottomLeft() + QPointF(4.0, -4.0));
        }
    }

    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override
    {
        setCursor(IsResizeHandle(event->pos()) ? Qt::SizeHorCursor : Qt::OpenHandCursor);
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

    bool IsRemoveButton(const QPointF& pos) const
    {
        return removable && RemoveRect().contains(pos);
    }

private:
    QRectF RemoveRect() const
    {
        return QRectF(width - 24.0, 6.0, 18.0, 20.0);
    }

    EffectDefinition effect;
    int lane_index = 0;
    qreal width = CLIP_DEFAULT_WIDTH;
    bool removable = true;
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

        QGraphicsScene::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override
    {
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
        ClipResize
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
        const qreal content_height = lane_count * ROW_HEIGHT;
        const qreal scene_width = qMax<qreal>(TIMELINE_MIN_WIDTH, viewport_size.width());
        const qreal scene_height = qMax<qreal>(content_height, viewport_size.height());

        setSceneRect(0.0, 0.0, scene_width, scene_height);
        Track(addRect(sceneRect(), Qt::NoPen, QBrush(palette.color(QPalette::Base))));

        if(lane_names.empty())
        {
            AddText(empty_message, 12.0, 14.0, scene_width - 24.0, TextLineColor(palette, 160, 170), 10, false);
        }
        else
        {
            for(int i = 0; i < lane_names.size(); i++)
            {
                DrawLane(i, lane_names[i]);
            }
        }

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
        const qreal y = index * ROW_HEIGHT;
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

    DragMode drag_mode = NoDrag;
    QPointF drag_offset;
    QPointF drag_scene_start;
    qreal clip_start_x = 0.0;
    qreal clip_start_width = CLIP_DEFAULT_WIDTH;
    int last_preview_lane = -1;
    qreal last_preview_x = 0.0;
    TimelineClipItem* active_clip = nullptr;
    ClipPreviewItem* preview = nullptr;
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
        setSelectionMode(QAbstractItemView::NoSelection);
        setUniformItemSizes(true);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    }

    void SetLanes(const QVector<QString>& lanes)
    {
        clear();

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
        view->SetRuler(ruler);

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
        QLabel* timeline_header = HeaderLabel("Timeline", center_column);
        timeline_header->setFixedHeight(static_cast<int>(HEADER_HEIGHT));
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

        ReloadDevices();
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
