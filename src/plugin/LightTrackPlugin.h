#pragma once

#include <QObject>
#include <QPointer>
#include "OpenRGBPluginInterface.h"

#include <atomic>

class OpenRGBPluginAPIInterface;

class LightTrackPlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID FILE "LightTrackPlugin.json")
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    ~LightTrackPlugin() override;

    OpenRGBPluginInfo GetPluginInfo() override;
    unsigned int GetPluginAPIVersion() override;
    void Load(OpenRGBPluginAPIInterface* plugin_api_ptr) override;
    QWidget* GetWidget() override;
    QMenu* GetTrayMenu() override;
    void Unload() override;
    void OnProfileAboutToLoad() override;
    void OnProfileLoad(nlohmann::json profile_data) override;
    nlohmann::json OnProfileSave() override;
    unsigned char* OnSDKCommand(unsigned int pkt_id, unsigned char* pkt_data, unsigned int* pkt_size) override;
    void ProfileManagerUpdated(unsigned int update_reason) override;
    void ResourceManagerUpdated(unsigned int update_reason) override;
    void SettingsManagerUpdated(unsigned int update_reason) override;

private:
    static void DeviceListChangedCallback(void* ptr);
    static void DeviceDetectionStartedCallback(void* ptr);
    static void DeviceDetectionFinishedCallback(void* ptr);

    void PrepareForDeviceReload();
    void QueueDeviceReload();

    OpenRGBPluginAPIInterface* plugin_api = nullptr;
    QPointer<QWidget> page;
    std::atomic<bool> page_ready{false};
    std::atomic<bool> detection_in_progress{false};
    std::atomic<bool> reload_queued{false};
};
