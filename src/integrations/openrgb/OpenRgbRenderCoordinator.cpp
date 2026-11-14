#include "integrations/openrgb/OpenRgbRenderCoordinator.h"

namespace lighttrack::openrgb
{
namespace
{
thread_local RGBEffect* current_rendering_effect = nullptr;
thread_local bool submitting_output = false;
}

void SetSubmittingOutput(bool submitting) noexcept
{
    submitting_output = submitting;
}

bool IsSubmittingOutput() noexcept
{
    return submitting_output;
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
