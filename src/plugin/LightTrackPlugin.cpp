#include "plugin/LightTrackPlugin.h"

#include "infrastructure/persistence/LayoutFileRepository.h"
#include "integrations/audio/MiniaudioAudioService.h"
#include "integrations/openrgb/OpenRgbEffectsRuntime.h"
#include "integrations/openrgb/OpenRgbTimelineBackend.h"
#include "ui/pages/LightTrackPage.h"

#include "ResourceManagerInterface.h"

#include <QMetaObject>
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

        page = lighttrack::ui::CreateLightTrackPage(
            lighttrack::openrgb::CreateOpenRgbTimelineBackend(
                resource_manager),
            lighttrack::audio::CreateMiniaudioAudioService(),
            lighttrack::persistence::CreateLayoutFileRepository());
    }

    if(resource_manager != nullptr
        && !callbacks_registered)
    {
        resource_manager->RegisterDeviceListChangeCallback(
            DeviceListChangedCallback, this);
        resource_manager->RegisterDetectionProgressCallback(
            DeviceListChangedCallback, this);
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
        resource_manager->UnregisterDetectionProgressCallback(
            DeviceListChangedCallback, this);
    }
    callbacks_registered = false;

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

    const QPointer<QWidget> guarded_page = plugin->page;
    if(guarded_page == nullptr)
    {
        return;
    }

    QMetaObject::invokeMethod(
        guarded_page.data(),
        [guarded_page]()
    {
        if(guarded_page != nullptr)
        {
            lighttrack::ui::ReloadLightTrackDevices(guarded_page.data());
        }
    },
        Qt::QueuedConnection);
}
