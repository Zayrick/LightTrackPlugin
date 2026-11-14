#include "integrations/openrgb/OpenRgbEffectsRuntime.h"

#include "OpenRGBEffectSettings.h"
#include "OpenRGBEffectsPlugin.h"

OpenRGBPluginAPIInterface* OpenRGBEffectsPlugin::api = nullptr;
std::atomic<bool> OpenRGBEffectsPlugin::controllers_updating{false};
std::vector<ControllerZone*> OpenRGBEffectsPlugin::controller_zones;
std::shared_mutex OpenRGBEffectsPlugin::controller_zones_mutex;

namespace lighttrack::openrgb
{
void InitializeEffectsRuntime(OpenRGBPluginAPIInterface* plugin_api)
{
    OpenRGBEffectsPlugin::api = plugin_api;
    if(plugin_api != nullptr)
    {
        // The upstream loader appends preferred colors, so reset first when a
        // plugin pause/resume cycle initializes the runtime more than once.
        OpenRGBEffectSettings::globalSettings = {};
        OpenRGBEffectSettings::LoadGlobalSettings();
    }
}

void ShutdownEffectsRuntime()
{
    OpenRGBEffectsPlugin::api = nullptr;
}
}
