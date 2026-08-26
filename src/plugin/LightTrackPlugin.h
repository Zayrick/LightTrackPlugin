#pragma once

// Thin OpenRGB host adapter. UI and runtime implementations live in separate modules.
#include <QObject>
#include <QPointer>
#include "OpenRGBPluginInterface.h"

#include <atomic>

class ResourceManagerInterface;

class LightTrackPlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID)
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    ~LightTrackPlugin() override;

    OpenRGBPluginInfo GetPluginInfo() override;
    unsigned int GetPluginAPIVersion() override;
    void Load(ResourceManagerInterface* resource_manager_ptr) override;
    QWidget* GetWidget() override;
    QMenu* GetTrayMenu() override;
    void Unload() override;

private:
    static void DeviceListChangedCallback(void* ptr);
    static void DeviceDetectionStartedCallback(void* ptr);
    static void DeviceDetectionFinishedCallback(void* ptr);

    void PrepareForDeviceReload();
    void QueueDeviceReload();

    ResourceManagerInterface* resource_manager = nullptr;
    QPointer<QWidget> page;
    bool callbacks_registered = false;
    std::atomic<bool> detection_in_progress{false};
    std::atomic<bool> reload_queued{false};
};
