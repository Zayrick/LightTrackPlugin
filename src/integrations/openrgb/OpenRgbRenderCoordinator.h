#pragma once

#include <mutex>

namespace lighttrack::openrgb
{
// OpenRGB exposes one mutable color buffer per controller and consumes it on
// an asynchronous device thread. All LightTrack writers share this mutex so
// a complete effect or override frame is composed before it is submitted.
std::mutex& ControllerFrameMutex();
}
