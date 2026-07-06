#include "LightTrackPlugin.h"

#include "ResourceManagerInterface.h"
#include "RGBController/RGBController.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStyle>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
const char* LIGHTTRACK_CARD_MIME = "application/x-lighttrack-card";

QPoint MousePos(QMouseEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

QPoint MouseGlobalPos(QMouseEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->globalPosition().toPoint();
#else
    return event->globalPos();
#endif
}

QPoint DropPos(QDropEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
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

void ClearLayout(QLayout* layout)
{
    QLayoutItem* child = nullptr;

    while((child = layout->takeAt(0)) != nullptr)
    {
        delete child->widget();
        delete child;
    }
}

class EffectCard : public QFrame
{
public:
    EffectCard(const QString& name, const QString& color, QWidget* parent = nullptr) :
        QFrame(parent),
        name(name),
        color(color)
    {
        setFixedHeight(40);
        setCursor(Qt::OpenHandCursor);
        setStyleSheet(QString(
            "QFrame { background: %1; border-radius: 4px; }"
            "QLabel { color: white; border: 0; background: transparent; }").arg(color));

        QHBoxLayout* layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 0, 8, 0);

        QLabel* label = new QLabel(name, this);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(label);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if(event->button() == Qt::LeftButton)
        {
            drag_start = MousePos(event);
        }

        QFrame::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if(!(event->buttons() & Qt::LeftButton))
        {
            return;
        }

        if((MousePos(event) - drag_start).manhattanLength() < QApplication::startDragDistance())
        {
            return;
        }

        QDrag* drag = new QDrag(this);
        QMimeData* mime = new QMimeData();
        mime->setData(LIGHTTRACK_CARD_MIME, (name + "\n" + color).toUtf8());
        mime->setText(name);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }

private:
    QString name;
    QString color;
    QPoint drag_start;
};

class TimelineClip : public QFrame
{
public:
    TimelineClip(const QString& name, const QString& color, QWidget* parent = nullptr) :
        QFrame(parent)
    {
        setMinimumWidth(56);
        setFixedHeight(32);
        setCursor(Qt::OpenHandCursor);
        setStyleSheet(QString(
            "QFrame { background: %1; border-radius: 4px; }"
            "QLabel { color: white; border: 0; background: transparent; }"
            "QToolButton { border: 0; background: transparent; }").arg(color));

        QHBoxLayout* layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 0, 2, 0);
        layout->setSpacing(2);

        QLabel* label = new QLabel(name, this);
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(label);

        QToolButton* remove = new QToolButton(this);
        remove->setAutoRaise(true);
        remove->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
        remove->setFixedSize(20, 20);
        remove->setToolTip("Remove");
        layout->addWidget(remove);

        connect(remove, &QToolButton::clicked, this, &TimelineClip::deleteLater);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if(event->button() == Qt::LeftButton)
        {
            drag_start = MouseGlobalPos(event);
            start_x = x();
            start_width = width();
            resizing = MousePos(event).x() >= width() - 8;
            setCursor(resizing ? Qt::SizeHorCursor : Qt::ClosedHandCursor);
        }

        QFrame::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if(!(event->buttons() & Qt::LeftButton))
        {
            setCursor(MousePos(event).x() >= width() - 8 ? Qt::SizeHorCursor : Qt::OpenHandCursor);
            return;
        }

        const int delta_x = MouseGlobalPos(event).x() - drag_start.x();

        if(resizing)
        {
            const int max_width = parentWidget() == nullptr ? 10000 : parentWidget()->width() - x();
            setFixedWidth(qBound(56, start_width + delta_x, max_width));
        }
        else if(parentWidget() != nullptr)
        {
            const int max_x = qMax(0, parentWidget()->width() - width());
            move(qBound(0, start_x + delta_x, max_x), y());
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        resizing = false;
        setCursor(Qt::OpenHandCursor);
        QFrame::mouseReleaseEvent(event);
    }

private:
    QPoint drag_start;
    int start_x = 0;
    int start_width = 0;
    bool resizing = false;
};

class TimelineLane : public QFrame
{
public:
    explicit TimelineLane(QWidget* parent = nullptr) :
        QFrame(parent)
    {
        setAcceptDrops(true);
        setMinimumWidth(900);
        setFixedHeight(52);
        setFrameShape(QFrame::StyledPanel);
        setStyleSheet("QFrame { background: #fafafa; border: 1px solid #d6d6d6; }");
    }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if(event->mimeData()->hasFormat(LIGHTTRACK_CARD_MIME))
        {
            event->acceptProposedAction();
        }
    }

    void dropEvent(QDropEvent* event) override
    {
        const QStringList parts = QString::fromUtf8(event->mimeData()->data(LIGHTTRACK_CARD_MIME)).split('\n');
        const QString name = parts.value(0, "Effect");
        const QString color = parts.value(1, "#777777");

        TimelineClip* clip = new TimelineClip(name, color, this);
        const int clip_width = 130;
        const int x = qBound(0, DropPos(event).x(), qMax(0, width() - clip_width));
        clip->setGeometry(x, 10, clip_width, 32);
        clip->show();

        event->acceptProposedAction();
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QFrame::resizeEvent(event);

        for(QObject* child : children())
        {
            TimelineClip* clip = dynamic_cast<TimelineClip*>(child);
            if(clip != nullptr && clip->x() + clip->width() > width())
            {
                clip->move(qMax(0, width() - clip->width()), clip->y());
            }
        }
    }

    void paintEvent(QPaintEvent* event) override
    {
        QFrame::paintEvent(event);

        QPainter painter(this);
        painter.setPen(QColor(226, 226, 226));

        for(int x = 80; x < width(); x += 80)
        {
            painter.drawLine(x, 1, x, height() - 2);
        }
    }
};

class TimelineRuler : public QWidget
{
public:
    explicit TimelineRuler(QWidget* parent = nullptr) :
        QWidget(parent)
    {
        setMinimumWidth(900);
        setFixedHeight(26);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setPen(QColor(118, 118, 118));

        for(int x = 0; x < width(); x += 80)
        {
            painter.drawLine(x, 16, x, 24);
            painter.drawText(x + 4, 14, QString::number(x / 80) + "s");
        }

        painter.drawLine(0, 24, width(), 24);
    }
};

class LightTrackPage : public QWidget
{
public:
    explicit LightTrackPage(ResourceManagerInterface* resource_manager, QWidget* parent = nullptr) :
        QWidget(parent),
        resource_manager(resource_manager)
    {
        QHBoxLayout* page_layout = new QHBoxLayout(this);
        page_layout->setContentsMargins(8, 8, 8, 8);
        page_layout->setSpacing(8);

        QWidget* timeline_panel = new QWidget(this);
        QVBoxLayout* timeline_panel_layout = new QVBoxLayout(timeline_panel);
        timeline_panel_layout->setContentsMargins(0, 0, 0, 0);
        timeline_panel_layout->setSpacing(6);

        title = new QLabel("Timeline", timeline_panel);
        timeline_panel_layout->addWidget(title);

        QScrollArea* scroll = new QScrollArea(timeline_panel);
        scroll->setWidgetResizable(true);

        timeline_body = new QWidget(scroll);
        timeline_body->setMinimumWidth(1080);
        lanes_layout = new QVBoxLayout(timeline_body);
        lanes_layout->setContentsMargins(0, 0, 0, 0);
        lanes_layout->setSpacing(4);
        scroll->setWidget(timeline_body);
        timeline_panel_layout->addWidget(scroll, 1);

        page_layout->addWidget(timeline_panel, 1);

        QToolButton* cards_toggle = new QToolButton(this);
        cards_toggle->setAutoRaise(true);
        cards_toggle->setArrowType(Qt::RightArrow);
        cards_toggle->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

        QFrame* cards = new QFrame(this);
        cards->setFrameShape(QFrame::StyledPanel);
        cards->setFixedWidth(170);

        QVBoxLayout* cards_layout = new QVBoxLayout(cards);
        cards_layout->setContentsMargins(8, 8, 8, 8);
        cards_layout->setSpacing(8);
        cards_layout->addWidget(new QLabel("Cards", cards));
        cards_layout->addWidget(new EffectCard("Solid", "#2f80ed", cards));
        cards_layout->addWidget(new EffectCard("Fade", "#27ae60", cards));
        cards_layout->addWidget(new EffectCard("Wave", "#f2994a", cards));
        cards_layout->addWidget(new EffectCard("Blink", "#eb5757", cards));
        cards_layout->addStretch(1);
        page_layout->addWidget(cards_toggle);
        page_layout->addWidget(cards);

        connect(cards_toggle, &QToolButton::clicked, cards, [cards_toggle, cards]()
        {
            const bool show_cards = cards->isHidden();
            cards->setVisible(show_cards);
            cards_toggle->setArrowType(show_cards ? Qt::RightArrow : Qt::LeftArrow);
        });

        ReloadDevices();
    }

    void ReloadDevices()
    {
        if(resource_manager == nullptr)
        {
            title->setText("Timeline");
            ClearTimeline("OpenRGB resource manager unavailable");
            return;
        }

        std::vector<RGBController*>& controllers = resource_manager->GetRGBControllers();

        title->setText("Timeline");
        ClearLayout(lanes_layout);

        if(controllers.empty())
        {
            ClearTimeline("No devices");
            return;
        }

        AddRuler();

        for(int controller_idx = 0; controller_idx < static_cast<int>(controllers.size()); controller_idx++)
        {
            RGBController* controller = controllers[controller_idx];
            AddLane(QString::fromStdString(controller->GetName()));

            for(int zone_idx = 0; zone_idx < static_cast<int>(controller->zones.size()); zone_idx++)
            {
                const zone& zone_ref = controller->zones[zone_idx];
                AddLane(QString("%1 (%2, %3 LEDs)")
                    .arg(QString::fromStdString(zone_ref.name))
                    .arg(ZoneTypeName(zone_ref.type))
                    .arg(zone_ref.leds_count));

                for(int segment_idx = 0; segment_idx < static_cast<int>(zone_ref.segments.size()); segment_idx++)
                {
                    const segment& segment_ref = zone_ref.segments[segment_idx];
                    AddLane(QString("%1 (%2 LEDs)")
                        .arg(QString::fromStdString(segment_ref.name))
                        .arg(segment_ref.leds_count));
                }
            }
        }

        lanes_layout->addStretch(1);
    }

private:
    void ClearTimeline(const QString& message)
    {
        ClearLayout(lanes_layout);
        QLabel* label = new QLabel(message, timeline_body);
        label->setAlignment(Qt::AlignCenter);
        lanes_layout->addWidget(label, 1);
    }

    void AddRuler()
    {
        QWidget* row = new QWidget(timeline_body);
        QHBoxLayout* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setSpacing(6);

        row_layout->addSpacing(240);
        row_layout->addWidget(new TimelineRuler(row), 1);

        lanes_layout->addWidget(row);
    }

    void AddLane(const QString& name)
    {
        QWidget* row = new QWidget(timeline_body);
        row->setFixedHeight(52);
        QHBoxLayout* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setSpacing(6);

        QLabel* label = new QLabel(name, row);
        label->setFixedWidth(240);
        label->setFixedHeight(52);
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignVCenter);
        label->setToolTip(name.trimmed());
        row_layout->addWidget(label);

        row_layout->addWidget(new TimelineLane(row), 1);
        lanes_layout->addWidget(row);
    }

    ResourceManagerInterface* resource_manager;
    QLabel* title;
    QWidget* timeline_body;
    QVBoxLayout* lanes_layout;
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
