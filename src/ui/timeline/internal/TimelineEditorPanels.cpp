#include "TimelineEditorInternal.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCursor>
#include <QDrag>
#include <QEvent>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTreeWidgetItem>
#include <QWheelEvent>

#include <array>
#include <utility>

namespace lighttrack::timeline_internal
{
namespace
{
inline constexpr int LANE_ACTION_BUTTON_SIZE = 24;
inline constexpr int LANE_ACTION_BUTTON_SPACING = 3;
inline constexpr int LANE_ACTION_RIGHT_MARGIN = 5;
inline constexpr int LANE_ACTION_ICON_SIZE = 15;

inline constexpr std::array<ushort, 3> LANE_ACTION_ICONS = {
    0xE172,
    0xE1C2,
    0xE051
};
inline constexpr int LANE_ACTION_BUTTON_COUNT =
    static_cast<int>(LANE_ACTION_ICONS.size());
inline constexpr int LANE_ACTION_BUTTONS_WIDTH =
    LANE_ACTION_BUTTON_COUNT * LANE_ACTION_BUTTON_SIZE
    + (LANE_ACTION_BUTTON_COUNT - 1)
        * LANE_ACTION_BUTTON_SPACING;
inline constexpr int LANE_ACTIONS_WIDTH =
    LANE_ACTION_BUTTONS_WIDTH + LANE_ACTION_RIGHT_MARGIN;

QRect LaneActionButtonRect(
    const QStyleOptionViewItem& option,
    int action_index)
{
    return {
        option.rect.right()
            - LANE_ACTION_RIGHT_MARGIN
            - LANE_ACTION_BUTTONS_WIDTH
            + 1
            + action_index
                * (LANE_ACTION_BUTTON_SIZE
                    + LANE_ACTION_BUTTON_SPACING),
        option.rect.top()
            + (option.rect.height()
                - LANE_ACTION_BUTTON_SIZE)
                / 2,
        LANE_ACTION_BUTTON_SIZE,
        LANE_ACTION_BUTTON_SIZE
    };
}

class LaneItemDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(
        QPainter* painter,
        const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        const bool has_actions =
            index.data(LaneIndexRole).toInt() >= 0;
        if(!has_actions)
        {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem content_option(option);
        content_option.rect.adjust(
            0,
            0,
            -LANE_ACTIONS_WIDTH,
            0);
        QStyledItemDelegate::paint(
            painter,
            content_option,
            index);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        QFont icon_font(lighttrack::ui::LucideFontFamily());
        icon_font.setPixelSize(LANE_ACTION_ICON_SIZE);
        painter->setFont(icon_font);

        const int action_state =
            index.data(LaneActionStateRole).toInt();
        for(int action_index = 0;
            action_index < LANE_ACTION_BUTTON_COUNT;
            ++action_index)
        {
            const QRect button_rect =
                LaneActionButtonRect(option, action_index);
            const bool checked =
                action_index > 0
                && (action_state
                    & (1 << (action_index - 1))) != 0;

            if(checked)
            {
                const QColor accent =
                    action_index == 2
                    ? QColor(235, 86, 86)
                    : QColor(64, 188, 255);
                painter->setPen(QPen(
                    WithAlpha(accent, 180),
                    1.0));
                painter->setBrush(WithAlpha(accent, 70));
                painter->drawRoundedRect(
                    QRectF(button_rect).adjusted(
                        0.5,
                        0.5,
                        -0.5,
                        -0.5),
                    5.0,
                    5.0);
            }

            painter->setPen(
                option.palette.color(QPalette::ButtonText));
            painter->drawText(
                button_rect,
                Qt::AlignCenter,
                QString(QChar(
                    LANE_ACTION_ICONS[
                        static_cast<std::size_t>(
                            action_index)])));
        }

        painter->restore();
    }

    bool editorEvent(
        QEvent* event,
        QAbstractItemModel* model,
        const QStyleOptionViewItem& option,
        const QModelIndex& index) override
    {
        if(event->type() != QEvent::MouseButtonRelease
            || index.data(LaneIndexRole).toInt() < 0)
        {
            return QStyledItemDelegate::editorEvent(
                event,
                model,
                option,
                index);
        }

        const QMouseEvent* mouse_event =
            static_cast<QMouseEvent*>(event);
        if(mouse_event->button() != Qt::LeftButton)
        {
            return false;
        }

        for(int action_index = 0;
            action_index < LANE_ACTION_BUTTON_COUNT;
            ++action_index)
        {
            const QRect button_rect =
                LaneActionButtonRect(option, action_index);
            if(!button_rect.contains(mouse_event->pos()))
            {
                continue;
            }

            if(action_index == 0)
            {
                bool accepted = false;
                const QString current_name =
                    index.data(Qt::DisplayRole).toString();
                const QString new_name = QInputDialog::getText(
                    qobject_cast<QWidget*>(parent()),
                    QStringLiteral("Rename zone"),
                    QStringLiteral("Name:"),
                    QLineEdit::Normal,
                    current_name,
                    &accepted).trimmed();
                if(accepted
                    && !new_name.isEmpty()
                    && new_name != current_name)
                {
                    model->setData(index, new_name, Qt::EditRole);
                }
                return true;
            }

            const int action_bit = 1 << (action_index - 1);
            const int current_state =
                index.data(LaneActionStateRole).toInt();
            model->setData(
                index,
                current_state ^ action_bit,
                LaneActionStateRole);
            return true;
        }

        return false;
    }
};
}

OverlayScrollBar::OverlayScrollBar(
    QAbstractScrollArea* scroll_area,
    Qt::Orientation orientation) :
    QWidget(scroll_area),
    scroll_area(scroll_area),
    scroll_bar(
        orientation == Qt::Horizontal
            ? scroll_area->horizontalScrollBar()
            : scroll_area->verticalScrollBar()),
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

        if((step > 0.0 && opacity >= target_opacity)
            || (step < 0.0 && opacity <= target_opacity))
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

    connect(
        scroll_bar,
        &QScrollBar::rangeChanged,
        this,
        [this](int, int)
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
    connect(
        scroll_bar,
        &QScrollBar::valueChanged,
        this,
        [this](int)
        {
            update();
            RevealTemporarily();
        });

    UpdateGeometry();
}

bool OverlayScrollBar::eventFilter(QObject* watched, QEvent* event)
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
                const QMouseEvent* mouse_event =
                    static_cast<QMouseEvent*>(event);
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

bool OverlayScrollBar::event(QEvent* event)
{
    if(event->type() == QEvent::Enter || event->type() == QEvent::Leave)
    {
        UpdatePointerProximityFromCursor();
    }

    return QWidget::event(event);
}

void OverlayScrollBar::paintEvent(QPaintEvent*)
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
    painter.drawRoundedRect(
        ThumbRect(),
        THUMB_WIDTH / 2.0,
        THUMB_WIDTH / 2.0);
}

void OverlayScrollBar::mousePressEvent(QMouseEvent* event)
{
    if(event->button() != Qt::LeftButton || !IsScrollable())
    {
        QWidget::mousePressEvent(event);
        return;
    }

    const qreal pointer_position = AxisPosition(event->pos());
    const QRectF thumb = ThumbRect();
    const qreal thumb_start =
        orientation == Qt::Horizontal ? thumb.left() : thumb.top();
    const qreal thumb_length =
        orientation == Qt::Horizontal ? thumb.width() : thumb.height();

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

void OverlayScrollBar::mouseMoveEvent(QMouseEvent* event)
{
    if(dragging)
    {
        SetValueFromThumbPosition(
            AxisPosition(event->pos()) - drag_offset);
        event->accept();
        return;
    }

    UpdatePointerProximityFromCursor();
    QWidget::mouseMoveEvent(event);
}

void OverlayScrollBar::mouseReleaseEvent(QMouseEvent* event)
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

void OverlayScrollBar::wheelEvent(QWheelEvent* event)
{
    RevealTemporarily();

    const QPoint pixel_delta = event->pixelDelta();
    const QPoint angle_delta = event->angleDelta();
    int delta =
        orientation == Qt::Horizontal ? pixel_delta.x() : pixel_delta.y();

    if(delta == 0)
    {
        delta = orientation == Qt::Horizontal
            ? pixel_delta.y()
            : pixel_delta.x();
    }

    if(delta != 0)
    {
        wheel_remainder += delta;
    }
    else
    {
        delta = orientation == Qt::Horizontal
            ? angle_delta.x()
            : angle_delta.y();
        if(delta == 0)
        {
            delta = orientation == Qt::Horizontal
                ? angle_delta.y()
                : angle_delta.x();
        }

        const int scroll_lines = qMax(1, QApplication::wheelScrollLines());
        const int single_step = qMax(1, scroll_bar->singleStep());
        wheel_remainder +=
            static_cast<qreal>(delta) * scroll_lines * single_step / 120.0;
    }

    const int whole_delta = static_cast<int>(wheel_remainder);
    if(whole_delta != 0)
    {
        scroll_bar->setValue(scroll_bar->value() - whole_delta);
        wheel_remainder -= whole_delta;
    }

    event->accept();
}

bool OverlayScrollBar::IsScrollable() const
{
    return scroll_bar->maximum() > scroll_bar->minimum();
}

qreal OverlayScrollBar::AxisPosition(const QPoint& point) const
{
    return orientation == Qt::Horizontal ? point.x() : point.y();
}

qreal OverlayScrollBar::TrackLength() const
{
    return qMax<qreal>(
        0.0,
        (orientation == Qt::Horizontal ? width() : height())
            - 2.0 * TRACK_PADDING);
}

qreal OverlayScrollBar::ThumbLength() const
{
    const qreal track_length = TrackLength();
    const qreal range = scroll_bar->maximum() - scroll_bar->minimum();
    const qreal page_step = qMax(1, scroll_bar->pageStep());
    const qreal content_length = range + page_step;

    if(track_length <= 0.0 || content_length <= 0.0)
    {
        return 0.0;
    }

    return qMin(
        track_length,
        qMax<qreal>(
            MIN_THUMB_LENGTH,
            track_length * page_step / content_length));
}

QRectF OverlayScrollBar::ThumbRect() const
{
    const qreal track_length = TrackLength();
    const qreal thumb_length = ThumbLength();
    const qreal movable_length =
        qMax<qreal>(0.0, track_length - thumb_length);
    const qreal range = scroll_bar->maximum() - scroll_bar->minimum();
    const qreal ratio = range > 0.0
        ? (scroll_bar->value() - scroll_bar->minimum()) / range
        : 0.0;
    const qreal thumb_position =
        TRACK_PADDING + movable_length * ratio;

    if(orientation == Qt::Horizontal)
    {
        return QRectF(
            thumb_position,
            (height() - THUMB_WIDTH) / 2.0,
            thumb_length,
            THUMB_WIDTH);
    }

    return QRectF(
        (width() - THUMB_WIDTH) / 2.0,
        thumb_position,
        THUMB_WIDTH,
        thumb_length);
}

void OverlayScrollBar::SetValueFromThumbPosition(qreal position)
{
    const qreal movable_length =
        qMax<qreal>(0.0, TrackLength() - ThumbLength());
    if(movable_length <= 0.0)
    {
        return;
    }

    const qreal clamped_position = qBound<qreal>(
        0.0,
        position - TRACK_PADDING,
        movable_length);
    const qreal ratio = clamped_position / movable_length;
    const int range = scroll_bar->maximum() - scroll_bar->minimum();
    scroll_bar->setValue(
        scroll_bar->minimum() + qRound(range * ratio));
}

void OverlayScrollBar::UpdateGeometry()
{
    const QRect viewport_geometry = scroll_area->viewport()->geometry();

    if(orientation == Qt::Horizontal)
    {
        const int length =
            qMax(0, viewport_geometry.width() - 2 * EDGE_MARGIN);
        setGeometry(
            viewport_geometry.left() + EDGE_MARGIN,
            viewport_geometry.bottom() - HIT_WIDTH + 1,
            length,
            HIT_WIDTH);
    }
    else
    {
        const int length =
            qMax(0, viewport_geometry.height() - 2 * EDGE_MARGIN);
        setGeometry(
            viewport_geometry.right() - HIT_WIDTH + 1,
            viewport_geometry.top() + EDGE_MARGIN,
            HIT_WIDTH,
            length);
    }

    raise();
    update();
}

void OverlayScrollBar::UpdatePointerProximity(const QPoint& viewport_position)
{
    const QRect viewport_rect = scroll_area->viewport()->rect();
    const bool inside = viewport_rect.contains(viewport_position);
    const bool near_edge = orientation == Qt::Horizontal
        ? viewport_position.y()
            >= viewport_rect.bottom() - REVEAL_DISTANCE
        : viewport_position.x()
            >= viewport_rect.right() - REVEAL_DISTANCE;
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

void OverlayScrollBar::UpdatePointerProximityFromCursor()
{
    QWidget* scroll_viewport = scroll_area->viewport();
    UpdatePointerProximity(
        scroll_viewport->mapFromGlobal(QCursor::pos()));
}

void OverlayScrollBar::Reveal()
{
    if(!IsScrollable())
    {
        return;
    }

    show();
    raise();
    FadeTo(1.0);
}

void OverlayScrollBar::RevealTemporarily()
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

void OverlayScrollBar::FadeTo(qreal target)
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

LaneListWidget::LaneListWidget(QWidget* parent) :
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
    setItemDelegate(new LaneItemDelegate(this));

    connect(
        this,
        &QTreeWidget::itemExpanded,
        this,
        [this](QTreeWidgetItem*)
        {
            NotifyVisibleLanesChanged();
        });
    connect(
        this,
        &QTreeWidget::itemCollapsed,
        this,
        [this](QTreeWidgetItem*)
        {
            NotifyVisibleLanesChanged();
        });
    connect(
        this,
        &QTreeWidget::itemChanged,
        this,
        [this](QTreeWidgetItem* item, int column)
        {
            HandleItemChanged(item, column);
        });
}

void LaneListWidget::SetLanes(const QVector<TimelineLane>& lanes)
{
    rebuilding = true;
    clear();

    QTreeWidgetItem* music_item =
        new QTreeWidgetItem(this, QStringList("Music"));
    music_item->setData(0, LaneIndexRole, -1);
    music_item->setFlags(Qt::ItemIsEnabled);
    music_item->setSizeHint(
        0,
        QSize(0, static_cast<int>(ROW_HEIGHT)));
    music_item->setToolTip(0, "Music");

    QTreeWidgetItem* controller_item = nullptr;
    QTreeWidgetItem* zone_item = nullptr;

    for(int lane_index = 0; lane_index < lanes.size(); lane_index++)
    {
        const TimelineLane& lane = lanes[lane_index];
        QTreeWidgetItem* parent_item = nullptr;
        if(lane.level == 1)
        {
            parent_item = controller_item;
        }
        else if(lane.level >= 2)
        {
            parent_item =
                zone_item != nullptr ? zone_item : controller_item;
        }

        QTreeWidgetItem* item = parent_item != nullptr
            ? new QTreeWidgetItem(
                parent_item,
                QStringList(lane.name))
            : new QTreeWidgetItem(
                this,
                QStringList(lane.name));

        item->setData(0, LaneIndexRole, lane_index);
        const int action_state =
            (lane.highlighted ? 1 : 0)
            | (lane.disabled ? 2 : 0);
        item->setData(0, LaneActionStateRole, action_state);
        item->setData(0, LaneCommittedNameRole, lane.name);
        item->setData(
            0,
            LaneCommittedActionStateRole,
            action_state);
        item->setFlags(Qt::ItemIsEnabled);
        item->setSizeHint(
            0,
            QSize(0, static_cast<int>(ROW_HEIGHT)));
        item->setToolTip(
            0,
            QStringLiteral(
                "%1\nRename / Light zone / Disable zone")
                .arg(lane.name));

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

void LaneListWidget::SetVisibilityChangedCallback(
    std::function<void(const QVector<int>&)> callback)
{
    visibility_changed_callback = std::move(callback);
}

void LaneListWidget::SetLaneRenamedCallback(
    TimelineEditor::LaneRenamedCallback callback)
{
    lane_renamed_callback = std::move(callback);
}

void LaneListWidget::SetLaneHighlightedCallback(
    TimelineEditor::LaneStateChangedCallback callback)
{
    lane_highlighted_callback = std::move(callback);
}

void LaneListWidget::SetLaneDisabledCallback(
    TimelineEditor::LaneStateChangedCallback callback)
{
    lane_disabled_callback = std::move(callback);
}

QVector<int> LaneListWidget::VisibleLaneIndices() const
{
    QVector<int> indices;
    for(int i = 0; i < topLevelItemCount(); i++)
    {
        AppendVisibleLaneIndices(topLevelItem(i), indices);
    }
    return indices;
}

void LaneListWidget::drawRow(
    QPainter* painter,
    const QStyleOptionViewItem& option,
    const QModelIndex& index) const
{
    QTreeWidget::drawRow(painter, option, index);

    const bool is_music_row =
        !index.parent().isValid() && index.row() == 0;
    painter->save();
    painter->setPen(QPen(TextLineColor(
        palette(),
        is_music_row ? 52 : 38,
        is_music_row ? 82 : 72)));
    const qreal separator_y =
        option.rect.y() + option.rect.height() - 0.5;
    painter->drawLine(
        QPointF(option.rect.left(), separator_y),
        QPointF(option.rect.right() + 1.0, separator_y));
    painter->restore();
}

void LaneListWidget::AppendVisibleLaneIndices(
    const QTreeWidgetItem* item,
    QVector<int>& indices) const
{
    const int lane_index =
        item->data(0, LaneIndexRole).toInt();
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

void LaneListWidget::HandleItemChanged(
    QTreeWidgetItem* item,
    int column)
{
    if(rebuilding || item == nullptr || column != 0)
    {
        return;
    }

    const int lane_index =
        item->data(0, LaneIndexRole).toInt();
    if(lane_index < 0)
    {
        return;
    }

    const QSignalBlocker blocker(this);
    const QString committed_name =
        item->data(0, LaneCommittedNameRole).toString();
    const QString requested_name = item->text(0).trimmed();
    if(requested_name != committed_name)
    {
        const bool accepted =
            !requested_name.isEmpty()
            && (!lane_renamed_callback
                || lane_renamed_callback(
                    lane_index,
                    requested_name));
        if(accepted)
        {
            item->setText(0, requested_name);
            item->setData(
                0,
                LaneCommittedNameRole,
                requested_name);
            item->setToolTip(
                0,
                QStringLiteral(
                    "%1\nRename / Light zone / Disable zone")
                    .arg(requested_name));
        }
        else
        {
            item->setText(0, committed_name);
        }
    }

    int committed_state =
        item->data(
            0,
            LaneCommittedActionStateRole).toInt();
    const int requested_state =
        item->data(0, LaneActionStateRole).toInt();

    if((requested_state & 1) != (committed_state & 1))
    {
        const bool highlighted = (requested_state & 1) != 0;
        if(!lane_highlighted_callback
            || lane_highlighted_callback(
                lane_index,
                highlighted))
        {
            committed_state =
                highlighted
                ? committed_state | 1
                : committed_state & ~1;
        }
    }

    if((requested_state & 2) != (committed_state & 2))
    {
        const bool disabled = (requested_state & 2) != 0;
        if(!lane_disabled_callback
            || lane_disabled_callback(lane_index, disabled))
        {
            committed_state =
                disabled
                ? committed_state | 2
                : committed_state & ~2;
        }
    }

    item->setData(
        0,
        LaneCommittedActionStateRole,
        committed_state);
    item->setData(0, LaneActionStateRole, committed_state);
}

void LaneListWidget::NotifyVisibleLanesChanged()
{
    if(!rebuilding && visibility_changed_callback)
    {
        visibility_changed_callback(VisibleLaneIndices());
    }
}

EffectsListWidget::EffectsListWidget(QWidget* parent) :
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
}

void EffectsListWidget::SetEffects(const QVector<EffectGroup>& groups)
{
    clear();
    for(const EffectGroup& group : groups)
    {
        QTreeWidgetItem* group_item =
            new QTreeWidgetItem(this);
        group_item->setText(1, group.name);
        group_item->setFlags(Qt::ItemIsEnabled);
        group_item->setSizeHint(
            1,
            QSize(0, static_cast<int>(EFFECT_ROW_HEIGHT)));
        group_item->setToolTip(1, group.name);

        for(const EffectDescriptor& effect : group.effects)
        {
            QTreeWidgetItem* item =
                new QTreeWidgetItem(group_item);
            item->setText(1, effect.name);
            item->setData(
                1,
                EffectIdRole,
                effect.id.toUtf8());
            item->setIcon(1, ColorIcon(effect.color));
            item->setToolTip(
                1,
                group.name
                    + QStringLiteral(" / ")
                    + effect.name);
            item->setFlags(
                Qt::ItemIsEnabled
                | Qt::ItemIsSelectable
                | Qt::ItemIsDragEnabled);
            item->setSizeHint(
                1,
                QSize(0, static_cast<int>(EFFECT_ROW_HEIGHT)));
        }
    }
    expandAll();
}

void EffectsListWidget::startDrag(Qt::DropActions)
{
    QTreeWidgetItem* item = currentItem();
    if(item == nullptr || item->childCount() > 0)
    {
        return;
    }

    QMimeData* mime_data = new QMimeData();
    mime_data->setData(
        EFFECT_MIME,
        item->data(1, EffectIdRole).toByteArray());

    QDrag* drag = new QDrag(this);
    drag->setMimeData(mime_data);
    drag->exec(Qt::CopyAction);
}

QIcon EffectsListWidget::ColorIcon(const QColor& color) const
{
    QPixmap pixmap(10, 10);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(
        color.isValid()
            ? color
            : palette().color(QPalette::Highlight));
    painter.drawEllipse(QRectF(1.0, 1.0, 8.0, 8.0));
    return QIcon(pixmap);
}
}
