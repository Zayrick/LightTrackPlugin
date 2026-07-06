#pragma once

#include <QObject>
#include "OpenRGBPluginInterface.h"

class ResourceManagerInterface;

class LightTrackPlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID)
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    OpenRGBPluginInfo GetPluginInfo() override;
    unsigned int GetPluginAPIVersion() override;
    void Load(ResourceManagerInterface* resource_manager_ptr) override;
    QWidget* GetWidget() override;
    QMenu* GetTrayMenu() override;
    void Unload() override;

private:
    static void DeviceListChangedCallback(void* ptr);

    ResourceManagerInterface* resource_manager = nullptr;
    QWidget* page = nullptr;
};
