#include "LightTrackPlugin.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QSizePolicy>
#include <QToolButton>
#include <QWidget>

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

void LightTrackPlugin::Load(ResourceManagerInterface*)
{
}

QWidget* LightTrackPlugin::GetWidget()
{
    QWidget* page = new QWidget();
    QHBoxLayout* page_layout = new QHBoxLayout(page);
    page_layout->setContentsMargins(0, 0, 0, 0);
    page_layout->setSpacing(0);

    QFrame* left = new QFrame(page);
    left->setFrameShape(QFrame::StyledPanel);
    left->setFixedWidth(180);

    QFrame* middle = new QFrame(page);
    middle->setFrameShape(QFrame::StyledPanel);

    QToolButton* right_toggle = new QToolButton(page);
    right_toggle->setAutoRaise(true);
    right_toggle->setArrowType(Qt::RightArrow);
    right_toggle->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    QFrame* right = new QFrame(page);
    right->setFrameShape(QFrame::StyledPanel);
    right->setFixedWidth(180);

    page_layout->addWidget(left);
    page_layout->addWidget(middle, 1);
    page_layout->addWidget(right_toggle);
    page_layout->addWidget(right);

    connect(right_toggle, &QToolButton::clicked, right, [right_toggle, right]()
    {
        const bool show_right = right->isHidden();
        right->setVisible(show_right);
        right_toggle->setArrowType(show_right ? Qt::RightArrow : Qt::LeftArrow);
    });

    return page;
}

QMenu* LightTrackPlugin::GetTrayMenu()
{
    return nullptr;
}

void LightTrackPlugin::Unload()
{
}
