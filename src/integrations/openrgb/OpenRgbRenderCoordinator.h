#pragma once

#include <mutex>

class RGBEffect;

namespace lighttrack::openrgb
{
// OpenRGB exposes one mutable color buffer per controller and consumes it on
// an asynchronous device thread. All LightTrack writers share this mutex so
// a complete effect or override frame is composed before it is submitted.
std::mutex& ControllerFrameMutex();

void SetCurrentRenderingEffect(RGBEffect* effect) noexcept;
RGBEffect* CurrentRenderingEffect() noexcept;

// True while this thread is submitting a LightTrack frame to a real output
// controller (used by playback diagnostics to tell writers apart).
void SetSubmittingOutput(bool submitting) noexcept;
bool IsSubmittingOutput() noexcept;
}
