#include "LightTrackOpenRGBEffectsBridge.h"

#include "OpenRGBEffectSettings.h"
#include "OpenRGBEffectsPlugin.h"

ResourceManagerInterface* OpenRGBEffectsPlugin::RMPointer = nullptr;

void InitializeOpenRGBEffectsRuntime(ResourceManagerInterface* resource_manager)
{
    OpenRGBEffectsPlugin::RMPointer = resource_manager;
    if(resource_manager != nullptr)
    {
        OpenRGBEffectSettings::LoadGlobalSettings();
    }
}

void ShutdownOpenRGBEffectsRuntime()
{
    OpenRGBEffectsPlugin::RMPointer = nullptr;
}
