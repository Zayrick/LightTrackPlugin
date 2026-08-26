#include "plugin/LightTrackPlugin.h"

#include "integrations/openrgb/OpenRgbEffectsRuntime.h"
#include "ui/pages/LightTrackPage.h"

#include "ResourceManagerInterface.h"

#include <QMetaObject>
#include <QThread>
#include <QWidget>

LightTrackPlugin::~LightTrackPlugin()
{
    Unload();
}

OpenRGBPluginInfo LightTrackPlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;
    info.Name = "LightTrack Plugin";
    info.Description = "LightTrack integration for OpenRGB";
    info.Version = "0.1.0";
    info.Location = OPENRGB_PLUGIN_LOCATION_TOP;
    info.Label = "LightTrack";
    return info;
}

unsigned int LightTrackPlugin::GetPluginAPIVersion()
{
    return OPENRGB_PLUGIN_API_VERSION;
}

void LightTrackPlugin::Load(ResourceManagerInterface* resource_manager_ptr)
{
    if(resource_manager == resource_manager_ptr
        && resource_manager != nullptr)
    {
        return;
    }

    if(resource_manager != resource_manager_ptr
        && (resource_manager != nullptr
            || page != nullptr
            || callbacks_registered))
    {
        Unload();
    }

    resource_manager = resource_manager_ptr;
    lighttrack::openrgb::InitializeEffectsRuntime(resource_manager_ptr);
}

QWidget* LightTrackPlugin::GetWidget()
{
    if(page == nullptr)
    {
        if(resource_manager != nullptr)
        {
            resource_manager->WaitForDeviceDetection();
        }

        page = lighttrack::ui::CreateLightTrackPage(resource_manager);
    }

    if(resource_manager != nullptr
        && !callbacks_registered)
    {
        resource_manager->RegisterDeviceListChangeCallback(
            DeviceListChangedCallback, this);
        resource_manager->RegisterDetectionStartCallback(
            DeviceDetectionStartedCallback, this);
        resource_manager->RegisterDetectionEndCallback(
            DeviceDetectionFinishedCallback, this);
        callbacks_registered = true;
    }

    return page.data();
}

QMenu* LightTrackPlugin::GetTrayMenu()
{
    return nullptr;
}

void LightTrackPlugin::Unload()
{
    if(resource_manager != nullptr
        && callbacks_registered)
    {
        resource_manager->UnregisterDeviceListChangeCallback(
            DeviceListChangedCallback, this);
        resource_manager->UnregisterDetectionStartCallback(
            DeviceDetectionStartedCallback, this);
        resource_manager->UnregisterDetectionEndCallback(
            DeviceDetectionFinishedCallback, this);
    }
    callbacks_registered = false;
    detection_in_progress.store(false);
    reload_queued.store(false);

    QWidget* page_widget = page.data();
    delete page_widget;
    page = nullptr;
    resource_manager = nullptr;
    lighttrack::openrgb::ShutdownEffectsRuntime();
}

void LightTrackPlugin::DeviceListChangedCallback(void* ptr)
{
    LightTrackPlugin* plugin =
        static_cast<LightTrackPlugin*>(ptr);
    if(plugin == nullptr)
    {
        return;
    }

    if(plugin->detection_in_progress.load())
    {
        // A full detection publishes an empty list and then one update per
        // discovered controller. Rebuild once from DetectionFinished instead
        // of queueing a complete layout restore for every intermediate list.
        return;
    }

    // Hot-unplug callbacks are issued before the caller deletes the removed
    // controller. Stop effect workers synchronously while pointers are valid.
    plugin->PrepareForDeviceReload();
    plugin->QueueDeviceReload();
}

void LightTrackPlugin::DeviceDetectionStartedCallback(void* ptr)
{
    LightTrackPlugin* plugin = static_cast<LightTrackPlugin*>(ptr);
    if(plugin == nullptr)
    {
        return;
    }

    plugin->detection_in_progress.store(true);
    // ResourceManager invokes this callback before Cleanup() deletes any
    // hardware controller, so this call must complete synchronously.
    plugin->PrepareForDeviceReload();
}

void LightTrackPlugin::DeviceDetectionFinishedCallback(void* ptr)
{
    LightTrackPlugin* plugin = static_cast<LightTrackPlugin*>(ptr);
    if(plugin == nullptr)
    {
        return;
    }

    plugin->detection_in_progress.store(false);
    plugin->QueueDeviceReload();
}

void LightTrackPlugin::PrepareForDeviceReload()
{
    const QPointer<QWidget> guarded_page = page;
    if(guarded_page == nullptr)
    {
        return;
    }

    const auto prepare = [guarded_page]()
    {
        if(guarded_page != nullptr)
        {
            lighttrack::ui::PrepareLightTrackForDeviceReload(
                guarded_page.data());
        }
    };

    if(QThread::currentThread() == guarded_page->thread())
    {
        prepare();
        return;
    }

    QMetaObject::invokeMethod(
        guarded_page.data(),
        prepare,
        Qt::BlockingQueuedConnection);
}

void LightTrackPlugin::QueueDeviceReload()
{
    if(reload_queued.exchange(true))
    {
        return;
    }

    const QPointer<QWidget> guarded_page = page;
    if(guarded_page == nullptr)
    {
        reload_queued.store(false);
        return;
    }

    const QPointer<LightTrackPlugin> guarded_plugin(this);

    const bool queued = QMetaObject::invokeMethod(
        guarded_page.data(),
        [guarded_page, guarded_plugin]()
    {
        if(guarded_plugin != nullptr
            && guarded_plugin->detection_in_progress.load())
        {
            // A reload queued immediately before a scan must not consume the
            // scan's intermediate device list. DetectionFinished will queue
            // the final, stable rebuild.
            guarded_plugin->reload_queued.store(false);
            if(!guarded_plugin->detection_in_progress.load())
            {
                guarded_plugin->QueueDeviceReload();
            }
            return;
        }

        if(guarded_page != nullptr)
        {
            lighttrack::ui::ReloadLightTrackDevices(guarded_page.data());
        }
        if(guarded_plugin != nullptr)
        {
            guarded_plugin->reload_queued.store(false);
        }
    },
        Qt::QueuedConnection);

    if(!queued)
    {
        reload_queued.store(false);
    }
}
