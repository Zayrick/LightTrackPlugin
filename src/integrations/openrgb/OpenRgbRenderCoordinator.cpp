#include "integrations/openrgb/OpenRgbRenderCoordinator.h"

namespace lighttrack::openrgb
{
namespace
{
thread_local RGBEffect* current_rendering_effect = nullptr;
}

std::mutex& ControllerFrameMutex()
{
    static std::mutex mutex;
    return mutex;
}

void SetCurrentRenderingEffect(RGBEffect* effect) noexcept
{
    current_rendering_effect = effect;
}

RGBEffect* CurrentRenderingEffect() noexcept
{
    return current_rendering_effect;
}
}
