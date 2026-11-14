#pragma once

#include <QString>

#include <chrono>
#include <string>

namespace lighttrack::openrgb
{
// Lightweight playback timing (a few clock reads per frame). Reset() when
// playback starts; WriteReport() on pause/stop writes
// %TEMP%/lighttrack_perf.txt with where the time went.
namespace performance
{
using Clock = std::chrono::steady_clock;

void Reset();

// One effect frame: time waiting for its turn, computing, waiting for the
// frame lock and publishing to the devices.
void RecordEffectFrame(
    const std::string& effect_name,
    unsigned int target_fps,
    double wait_step_ms,
    double step_ms,
    double wait_publish_ms,
    double publish_ms);

// UI-thread work by name: "tick" (position update, clip switching, idle
// refresh), "timeline paint", ...
void RecordUi(const char* name, double ms);

void WriteReport();

inline double MsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start)
        .count();
}
}
}
