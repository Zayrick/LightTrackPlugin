#pragma once

#include "RGBEffect.h"

#include <atomic>
#include <chrono>
#include <cstdint>

namespace lighttrack::openrgb
{
// Base for LightTrack's own effects. The timeline backend calls
// SetClipTiming() right before activating the effect for a clip, so the
// effect can render relative to the clip (fade across it, start on its first
// beat) and stays aligned after seeking or resuming mid-clip.
//
// Clip time runs at the playback rate (slow play), and stands still while
// playback is paused ("frozen").
//
// Outside the timeline (no SetClipTiming before activation) time starts at
// activation and the clip duration is unknown (0).
class ClipTimedEffect : public RGBEffect
{
public:
    explicit ClipTimedEffect(QWidget* parent = nullptr) :
        RGBEffect(parent)
    {
    }

    void SetClipTiming(
        std::int64_t offset_ms,
        std::int64_t duration_ms,
        bool frozen = false,
        double rate = 1.0)
    {
        Anchor(static_cast<double>(offset_ms), rate);
        is_frozen.store(frozen, std::memory_order_relaxed);
        clip_duration_ms.store(
            duration_ms > 0 ? duration_ms : 0,
            std::memory_order_relaxed);
        timing_pending.store(true, std::memory_order_relaxed);
    }

    // Called on every playback/preview tick while the clip stays active.
    // While running, only corrects the clock when it has drifted noticeably
    // (seek inside the clip, edited clip length) or the rate changed, so
    // coarse playback-position updates do not add jitter.
    void SyncClipTiming(
        std::int64_t offset_ms,
        std::int64_t duration_ms,
        bool frozen = false,
        double rate = 1.0)
    {
        clip_duration_ms.store(
            duration_ms > 0 ? duration_ms : 0,
            std::memory_order_relaxed);

        if(frozen)
        {
            Anchor(static_cast<double>(offset_ms), rate);
            is_frozen.store(true, std::memory_order_relaxed);
            return;
        }

        constexpr double max_drift_ms = 40.0;
        const double drift = ElapsedMs() - static_cast<double>(offset_ms);
        if(is_frozen.load(std::memory_order_relaxed)
            || rate != clock_rate.load(std::memory_order_relaxed)
            || drift > max_drift_ms
            || drift < -max_drift_ms)
        {
            Anchor(static_cast<double>(offset_ms), rate);
        }
        is_frozen.store(false, std::memory_order_relaxed);
    }

    void EffectState(bool enabled) override
    {
        if(enabled && !timing_pending.exchange(false))
        {
            Anchor(0.0, 1.0);
            clip_duration_ms.store(0, std::memory_order_relaxed);
            is_frozen.store(false, std::memory_order_relaxed);
        }
        RGBEffect::EffectState(enabled);
    }

protected:
    // Seconds since the clip started (includes any seek offset). Stays put
    // while playback is paused and runs slower in slow play.
    double ElapsedSeconds() const
    {
        return ElapsedMs() / 1000.0;
    }

    bool IsFrozen() const
    {
        return is_frozen.load(std::memory_order_relaxed);
    }

    // Clip length in seconds, or 0 when unknown.
    double ClipSeconds() const
    {
        return static_cast<double>(
                   clip_duration_ms.load(std::memory_order_relaxed))
            / 1000.0;
    }

    // 0..1 across the clip. With an unknown duration it loops every
    // fallback_seconds so the effect is still visible in previews.
    double ClipProgress(double fallback_seconds = 2.0) const
    {
        const double elapsed = ElapsedSeconds();
        const double length = ClipSeconds();
        if(length <= 0.0)
        {
            const double cycles = elapsed / fallback_seconds;
            return cycles - static_cast<std::int64_t>(cycles);
        }
        const double progress = elapsed / length;
        return progress < 0.0 ? 0.0 : (progress > 1.0 ? 1.0 : progress);
    }

private:
    static std::int64_t NowNs()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    double ElapsedMs() const
    {
        const double offset = anchor_offset_ms.load(std::memory_order_relaxed);
        if(is_frozen.load(std::memory_order_relaxed))
        {
            return offset;
        }
        const double since_anchor_ms = static_cast<double>(
            NowNs() - anchor_ns.load(std::memory_order_relaxed)) / 1e6;
        return offset
            + since_anchor_ms * clock_rate.load(std::memory_order_relaxed);
    }

    void Anchor(double offset_ms, double rate)
    {
        anchor_ns.store(NowNs(), std::memory_order_relaxed);
        anchor_offset_ms.store(offset_ms, std::memory_order_relaxed);
        clock_rate.store(rate > 0.0 ? rate : 1.0, std::memory_order_relaxed);
    }

    // Clip time = anchor_offset_ms + (now - anchor_ns) * clock_rate.
    std::atomic<std::int64_t> anchor_ns{NowNs()};
    std::atomic<double> anchor_offset_ms{0.0};
    std::atomic<double> clock_rate{1.0};
    std::atomic<std::int64_t> clip_duration_ms{0};
    std::atomic<bool> timing_pending{false};
    std::atomic<bool> is_frozen{false};
};
}
