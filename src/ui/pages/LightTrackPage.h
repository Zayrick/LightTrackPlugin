#pragma once

class QWidget;
class ResourceManagerInterface;

namespace lighttrack::ui
{
QWidget* CreateLightTrackPage(
    ResourceManagerInterface* resource_manager);
void ReloadLightTrackDevices(QWidget* page);
void PrepareLightTrackForDeviceReload(QWidget* page);
}
