#include "integrations/openrgb/OutputWriter.h"

#include "integrations/openrgb/OpenRgbRenderCoordinator.h"
#include "integrations/openrgb/PlaybackDiagnostics.h"

#include <QString>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace lighttrack::openrgb
{
namespace
{
using Clock = std::chrono::steady_clock;

// Delays of the resends after a one-off frame (see Commit()).
constexpr std::chrono::milliseconds SETTLE_DELAYS[] = {
    std::chrono::milliseconds(40),
    std::chrono::milliseconds(150)};

// How long the destructor waits for the last frame to reach a device before
// giving up on a stuck one (its thread is then left to finish on its own).
constexpr auto STOP_TIMEOUT = std::chrono::milliseconds(400);
}

struct OutputWriter::Shared
{
    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable idle;
    Frame pending;
    bool update = false;
    bool settle = false;
    bool busy = false;
    bool exited = false;
    std::atomic<bool> stop{false};
    // The destructor stopped waiting: the controller may be gone soon.
    std::atomic<bool> abandoned{false};
    std::deque<Clock::time_point> resends;
};

void OutputWriter::Frame::Merge(Frame&& later)
{
    for(const auto& value : later.zones)
    {
        const unsigned int start = later.zone_starts[value.first];
        const unsigned int count = later.zone_counts[value.first];
        leds.erase(leds.lower_bound(start), leds.lower_bound(start + count));
        zones[value.first] = value.second;
        zone_starts[value.first] = start;
        zone_counts[value.first] = count;
    }
    for(const auto& value : later.leds)
    {
        leds[value.first] = value.second;
    }
}

OutputWriter::OutputWriter(RGBControllerInterface* controller, Mode mode) :
    controller_(controller),
    mode_(mode),
    shared_(std::make_shared<Shared>())
{
    if(mode_ == Mode::Threaded && controller_ != nullptr)
    {
        thread_ = std::thread(&OutputWriter::Run, shared_, controller_);
    }
}

OutputWriter::~OutputWriter()
{
    if(!thread_.joinable())
    {
        return;
    }

    bool exited = false;
    {
        std::unique_lock<std::mutex> lock(shared_->mutex);
        shared_->stop = true;
        shared_->wake.notify_all();
        exited = shared_->idle.wait_for(
            lock,
            STOP_TIMEOUT,
            [this]() { return shared_->exited; });
    }
    if(exited)
    {
        thread_.join();
    }
    else
    {
        // The device is stuck inside OpenRGB; never block the caller (the
        // UI thread) on it. The thread owns its shared state and stops as
        // soon as the device call returns.
        PlaybackDiagnostics::Note(
            QString("output writer for %1 did not stop in time")
                .arg(QString::fromStdString(controller_->GetName())));
        shared_->abandoned = true;
        thread_.detach();
    }
}

void OutputWriter::SetLed(unsigned int index, RGBColor color)
{
    staged_.leds[index] = color;
}

void OutputWriter::FillZone(
    unsigned int zone_index,
    unsigned int start,
    unsigned int count,
    RGBColor color)
{
    Frame fill;
    fill.zones[zone_index] = color;
    fill.zone_starts[zone_index] = start;
    fill.zone_counts[zone_index] = count;
    staged_.Merge(std::move(fill));
}

void OutputWriter::Commit(bool settle)
{
    if(controller_ == nullptr)
    {
        staged_ = Frame();
        return;
    }

    if(mode_ == Mode::Synchronous)
    {
        if(!direct_forced_)
        {
            ForceDirectMode(controller_);
            direct_forced_ = true;
        }
        Apply(controller_, staged_, nullptr);
        staged_ = Frame();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->pending.Merge(std::move(staged_));
        shared_->update = true;
        shared_->settle = shared_->settle || settle;
    }
    staged_ = Frame();
    shared_->wake.notify_one();
}

bool OutputWriter::WaitIdle(int timeout_ms)
{
    if(mode_ == Mode::Synchronous)
    {
        return true;
    }
    std::unique_lock<std::mutex> lock(shared_->mutex);
    return shared_->idle.wait_for(
        lock,
        std::chrono::milliseconds(timeout_ms),
        [this]() { return !shared_->update && !shared_->busy; });
}

void OutputWriter::Apply(
    RGBControllerInterface* controller,
    Frame& frame,
    const Shared* shared)
{
    const auto stopping = [shared]()
    {
        return shared != nullptr && shared->abandoned;
    };

    for(const auto& value : frame.zones)
    {
        if(stopping())
        {
            return;
        }
        controller->SetAllZoneColors(static_cast<int>(value.first), value.second);
    }
    for(const auto& value : frame.leds)
    {
        if(stopping())
        {
            return;
        }
        controller->SetColor(value.first, value.second);
    }
    SetSubmittingOutput(true);
    controller->UpdateLEDs();
    SetSubmittingOutput(false);
}

void OutputWriter::ForceDirectMode(RGBControllerInterface* controller)
{
    // Once per writer (not per frame): switching modes is a slow device call.
    for(unsigned int index = 0; index < controller->GetModeCount(); ++index)
    {
        if(controller->GetModeName(index) == "Direct")
        {
            if(controller->GetActiveMode() != static_cast<int>(index))
            {
                PlaybackDiagnostics::Note(
                    QString("mode switch to Direct on %1 (was %2)")
                        .arg(QString::fromStdString(controller->GetName()))
                        .arg(controller->GetActiveMode()));
                controller->SetActiveMode(static_cast<int>(index));
            }
            return;
        }
    }

    PlaybackDiagnostics::Note(
        QString("no Direct mode: SetCustomMode+UpdateMode on %1")
            .arg(QString::fromStdString(controller->GetName())));
    controller->SetCustomMode();
    controller->UpdateMode();
}

void OutputWriter::Run(
    std::shared_ptr<Shared> shared,
    RGBControllerInterface* controller)
{
    bool direct_forced = false;
    std::unique_lock<std::mutex> lock(shared->mutex);
    for(;;)
    {
        const auto ready = [&shared]()
        {
            return shared->update || shared->stop;
        };
        if(shared->resends.empty())
        {
            shared->wake.wait(lock, ready);
        }
        else
        {
            shared->wake.wait_until(lock, shared->resends.front(), ready);
        }

        if(shared->update)
        {
            Frame frame = std::move(shared->pending);
            shared->pending = Frame();
            const bool settle = shared->settle;
            shared->update = false;
            shared->settle = false;
            shared->busy = true;
            lock.unlock();

            if(!direct_forced)
            {
                ForceDirectMode(controller);
                direct_forced = true;
            }
            Apply(controller, frame, shared.get());

            lock.lock();
            shared->busy = false;
            if(settle)
            {
                const auto now = Clock::now();
                shared->resends.clear();
                for(const auto delay : SETTLE_DELAYS)
                {
                    shared->resends.push_back(now + delay);
                }
            }
            shared->idle.notify_all();
            continue;
        }

        if(shared->stop)
        {
            // Last frame is out; one quick resend in case the device dropped
            // it, unless the caller already stopped waiting for us.
            if(!shared->resends.empty() && !shared->abandoned)
            {
                shared->resends.clear();
                lock.unlock();
                std::this_thread::sleep_for(SETTLE_DELAYS[0]);
                if(!shared->abandoned)
                {
                    controller->UpdateLEDs();
                }
                lock.lock();
            }
            break;
        }

        if(!shared->resends.empty()
            && Clock::now() >= shared->resends.front())
        {
            shared->resends.pop_front();
            shared->busy = true;
            lock.unlock();
            controller->UpdateLEDs();
            lock.lock();
            shared->busy = false;
            shared->idle.notify_all();
        }
    }
    shared->exited = true;
    shared->idle.notify_all();
}
}
