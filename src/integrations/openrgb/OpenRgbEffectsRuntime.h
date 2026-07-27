#pragma once

class ResourceManagerInterface;

namespace lighttrack::openrgb
{
void InitializeEffectsRuntime(ResourceManagerInterface* resource_manager);
void ShutdownEffectsRuntime();
}
