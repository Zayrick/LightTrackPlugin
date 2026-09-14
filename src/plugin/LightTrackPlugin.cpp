#include "plugin/LightTrackPlugin.h"

#include "integrations/openrgb/OpenRgbEffectsRuntime.h"
#include "ui/pages/LightTrackPage.h"

#include "OpenRGBPluginInterface.h"
#include "ResourceManagerCallback.h"

#include <QMetaObject>
#include <QThread>
#include <QWidget>

LightTrackPlugin::~LightTrackPlugin()
{
    Unload();
}

OpenRGBPluginInfo LightTrackPlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info{};
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

void LightTrackPlugin::Load(OpenRGBPluginAPIInterface* plugin_api_ptr)
{
    if(plugin_api == plugin_api_ptr
        && plugin_api != nullptr)
    {
        return;
    }

    if(plugin_api != plugin_api_ptr
        && (plugin_api != nullptr
            || page != nullptr
            || page_ready.load()))
    {
        Unload();
    }

    plugin_api = plugin_api_ptr;
    lighttrack::openrgb::InitializeEffectsRuntime(plugin_api_ptr);
}

QWidget* LightTrackPlugin::GetWidget()
{
    if(page == nullptr)
    {
        page_ready.store(false);
        if(plugin_api != nullptr)
        {
            plugin_api->WaitForDetection();
        }

        page = lighttrack::ui::CreateLightTrackPage(plugin_api);
        page_ready.store(true);
    }

    return page.data();
}

QMenu* LightTrackPlugin::GetTrayMenu()
{
    return nullptr;
}

void LightTrackPlugin::Unload()
{
    page_ready.store(false);
    detection_in_progress.store(false);
    reload_queued.store(false);

    QWidget* page_widget = page.data();
    delete page_widget;
    page = nullptr;
    plugin_api = nullptr;
    lighttrack::openrgb::ShutdownEffectsRuntime();
}

void LightTrackPlugin::ResourceManagerUpdated(unsigned int update_reason)
{
    // The host can notify us while GetWidget is blocked in WaitForDetection.
    // No work may be dispatched to the GUI until that wait has completed.
    if(!page_ready.load())
    {
        return;
    }
    switch(update_reason)
    {
    case RESOURCEMANAGER_UPDATE_REASON_DETECTION_STARTED:
        DeviceDetectionStartedCallback(this);
        break;
    case RESOURCEMANAGER_UPDATE_REASON_DETECTION_COMPLETE:
        DeviceDetectionFinishedCallback(this);
        break;
    case RESOURCEMANAGER_UPDATE_REASON_DEVICE_LIST_UPDATED:
        DeviceListChangedCallback(this);
        break;
    }
}

void LightTrackPlugin::OnProfileAboutToLoad()
{
    if(!page_ready.load())
    {
        return;
    }
    const auto pause = [this]()
    {
        if(page != nullptr)
        {
            lighttrack::ui::PauseLightTrackForProfileLoad(page.data());
        }
    };
    if(QThread::currentThread() == thread())
    {
        pause();
    }
    else
    {
        QMetaObject::invokeMethod(this, pause, Qt::BlockingQueuedConnection);
    }
}

void LightTrackPlugin::OnProfileLoad(nlohmann::json /*profile_data*/) {}

nlohmann::json LightTrackPlugin::OnProfileSave()
{
    // Layout files remain the persistence format; no host profile payload yet.
    return nlohmann::json::object();
}

unsigned char* LightTrackPlugin::OnSDKCommand(unsigned int /*pkt_id*/,
    unsigned char* /*pkt_data*/, unsigned int* pkt_size)
{
    if(pkt_size != nullptr)
    {
        *pkt_size = 0;
    }
    return nullptr;
}

void LightTrackPlugin::ProfileManagerUpdated(unsigned int /*update_reason*/) {}
void LightTrackPlugin::SettingsManagerUpdated(unsigned int /*update_reason*/) {}

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
