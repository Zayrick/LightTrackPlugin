#pragma once

#include <QString>
#include <QtGlobal>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace lighttrack::audio
{
// Vinyl-style scrubbing: while the playhead is dragged, a virtual needle
// chases the drag position at a speed that follows the hand, so the music
// plays forwards/backwards and pitch-shifted like rubbing a DJ record.
//
// A low-rate mono copy of the song is decoded in the background on Load().
class AudioScrubber
{
public:
    AudioScrubber();
    ~AudioScrubber();

    AudioScrubber(const AudioScrubber&) = delete;
    AudioScrubber& operator=(const AudioScrubber&) = delete;

    void Load(const QString& path);
    void Unload();

    // Move the needle target. Starts the audio output if needed. Large
    // jumps (e.g. a single click far away) move silently.
    void ScrubTo(qint64 position_ms);
    // Stop the audio output (call shortly after the drag ends).
    void Stop();

    bool IsReady() const { return ready_.load(std::memory_order_acquire); }

    // Audio-thread only: fills interleaved stereo float frames.
    void Render(float* output, unsigned int frame_count);

private:
    struct Device;

    std::unique_ptr<Device> device_;
    unsigned int device_rate_ = 48000;
    bool running_ = false;
    qint64 last_target_ms_ = -1;

    std::thread decoder_thread_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> ready_{false};
    std::vector<std::int16_t> pcm_;

    std::atomic<double> target_frame_{0.0};
    std::atomic<double> reset_to_{0.0};
    std::atomic<bool> reset_pending_{false};

    // Owned by the audio callback.
    double position_ = 0.0;
    double speed_ = 0.0;
};
}
