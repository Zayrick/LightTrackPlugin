#pragma once

class QWidget;
class OpenRGBPluginAPIInterface;

namespace lighttrack::ui
{
QWidget* CreateLightTrackPage(
    OpenRGBPluginAPIInterface* plugin_api);
void ReloadLightTrackDevices(QWidget* page);
void PrepareLightTrackForDeviceReload(QWidget* page);
void PauseLightTrackForProfileLoad(QWidget* page);
}
