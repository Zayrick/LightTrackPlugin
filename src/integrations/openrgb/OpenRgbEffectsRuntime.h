#pragma once

class OpenRGBPluginAPIInterface;

namespace lighttrack::openrgb
{
void InitializeEffectsRuntime(OpenRGBPluginAPIInterface* plugin_api);
void ShutdownEffectsRuntime();
}
