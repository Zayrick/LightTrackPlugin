#include "integrations/openrgb/OpenRgbEffectsRuntime.h"

#include "OpenRGBEffectSettings.h"
#include "OpenRGBEffectsPlugin.h"

ResourceManagerInterface* OpenRGBEffectsPlugin::RMPointer = nullptr;

namespace lighttrack::openrgb
{
void InitializeEffectsRuntime(ResourceManagerInterface* resource_manager)
{
    OpenRGBEffectsPlugin::RMPointer = resource_manager;
    if(resource_manager != nullptr)
    {
        // The upstream loader appends preferred colors, so reset first when a
        // plugin pause/resume cycle initializes the runtime more than once.
        OpenRGBEffectSettings::globalSettings = {};
        OpenRGBEffectSettings::LoadGlobalSettings();
    }
}

void ShutdownEffectsRuntime()
{
    OpenRGBEffectsPlugin::RMPointer = nullptr;
}
}
