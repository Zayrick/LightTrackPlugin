#pragma once

#include "RGBControllerInterface.h"

#include <map>
#include <memory>
#include <thread>

namespace lighttrack::openrgb
{
// Sends LightTrack frames to one physical controller.
//
// Every color write into an OpenRGB controller takes its AccessMutex, which
// the controller's device thread holds while it talks to the hardware. On a
// slow bus (e.g. NvAPI I2C on a graphics card) that can take a long time or
// hang; writing under the shared frame lock then stalled every effect thread
// and the UI thread with it, freezing OpenRGB. A threaded writer keeps those
// calls on its own thread: frames queued while it is busy are coalesced into
// the latest one, and a stuck device only stops its own updates.
//
// Frames are composed with SetLed/FillZone and handed over by Commit(). Not
// thread-safe for composing: the router calls it under its frame lock.
class OutputWriter final
{
public:
    enum class Mode
    {
        Threaded,
        // Writes on the committing thread (tests).
        Synchronous
    };

    OutputWriter(RGBControllerInterface* controller, Mode mode);
    ~OutputWriter();

    OutputWriter(const OutputWriter&) = delete;
    OutputWriter& operator=(const OutputWriter&) = delete;

    // Absolute controller LED index.
    void SetLed(unsigned int index, RGBColor color);
    // Whole zone (OpenRGB SetAllZoneColors); [start, start + count) are the
    // zone's LED indices, used to drop earlier per-LED writes.
    void FillZone(
        unsigned int zone_index,
        unsigned int start,
        unsigned int count,
        RGBColor color);
    // Submits the composed frame. settle: a one-off frame (clear/override)
    // that is resent shortly afterwards, because OpenRGB can drop an update
    // that arrives while the device is still sending the previous one.
    void Commit(bool settle);

    // Waits until every committed frame is applied (tests).
    bool WaitIdle(int timeout_ms);

private:
    struct Frame
    {
        std::map<unsigned int, RGBColor> zones;
        std::map<unsigned int, unsigned int> zone_starts;
        std::map<unsigned int, unsigned int> zone_counts;
        std::map<unsigned int, RGBColor> leds;
        bool empty() const { return zones.empty() && leds.empty(); }
        void Merge(Frame&& later);
    };
    struct Shared;

    static void Apply(
        RGBControllerInterface* controller,
        Frame& frame,
        const Shared* shared);
    static void ForceDirectMode(RGBControllerInterface* controller);
    static void Run(
        std::shared_ptr<Shared> shared,
        RGBControllerInterface* controller);

    RGBControllerInterface* controller_;
    Mode mode_;
    Frame staged_;
    bool direct_forced_ = false;
    std::shared_ptr<Shared> shared_;
    std::thread thread_;
};
}
