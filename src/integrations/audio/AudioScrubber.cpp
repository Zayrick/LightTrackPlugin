#include "integrations/audio/AudioScrubber.h"

#include <QByteArray>
#include <QDir>

#include <algorithm>
#include <cmath>
#include <string>

#include "miniaudio.h"

namespace lighttrack::audio
{
namespace
{
// Scrub audio is decoded mono at a low rate: plenty for the scratch sound
// and small in memory (~10 MB for a 4-minute song).
constexpr ma_uint32 SOURCE_RATE = 22050;
// How far behind the drag the needle settles; sets the feel of the scratch.
constexpr double FOLLOW_SECONDS = 0.06;
// Max needle speed as a multiple of normal playback.
constexpr double MAX_SPEED = 8.0;
// Jumps larger than this move the needle silently (e.g. a click to seek).
constexpr qint64 JUMP_MS = 1500;
constexpr double OUTPUT_GAIN = 0.9;

void ScrubberCallback(
    ma_device* device,
    void* output,
    const void*,
    ma_uint32 frame_count)
{
    static_cast<AudioScrubber*>(device->pUserData)
        ->Render(static_cast<float*>(output), frame_count);
}
}

struct AudioScrubber::Device
{
    ma_device device{};
    bool initialized = false;
};

AudioScrubber::AudioScrubber() :
    device_(std::make_unique<Device>())
{
}

AudioScrubber::~AudioScrubber()
{
    Unload();
}

void AudioScrubber::Load(const QString& path)
{
    Unload();
    if(path.isEmpty())
    {
        return;
    }

    ma_device_config config =
        ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = 0; // device default
    config.dataCallback = ScrubberCallback;
    config.pUserData = this;
    if(ma_device_init(nullptr, &config, &device_->device) != MA_SUCCESS)
    {
        return;
    }
    device_->initialized = true;
    device_rate_ = std::max<ma_uint32>(1, device_->device.sampleRate);

    cancel_.store(false);
    decoder_thread_ = std::thread([this, path]()
    {
        ma_decoder_config decoder_config =
            ma_decoder_config_init(ma_format_s16, 1, SOURCE_RATE);
        ma_decoder decoder{};
#ifdef _WIN32
        const std::wstring native_path =
            QDir::toNativeSeparators(path).toStdWString();
        if(ma_decoder_init_file_w(
               native_path.c_str(), &decoder_config, &decoder)
            != MA_SUCCESS)
        {
            return;
        }
#else
        const QByteArray native_path = path.toLocal8Bit();
        if(ma_decoder_init_file(
               native_path.constData(), &decoder_config, &decoder)
            != MA_SUCCESS)
        {
            return;
        }
#endif
        std::vector<std::int16_t> samples;
        ma_uint64 length = 0;
        if(ma_decoder_get_length_in_pcm_frames(&decoder, &length)
            == MA_SUCCESS)
        {
            samples.reserve(static_cast<std::size_t>(length));
        }

        std::vector<std::int16_t> chunk(65536);
        while(!cancel_.load())
        {
            ma_uint64 read = 0;
            const ma_result result = ma_decoder_read_pcm_frames(
                &decoder, chunk.data(), chunk.size(), &read);
            samples.insert(samples.end(), chunk.begin(), chunk.begin() + read);
            if(result != MA_SUCCESS || read < chunk.size())
            {
                break;
            }
        }
        ma_decoder_uninit(&decoder);

        if(!cancel_.load() && samples.size() > 2)
        {
            pcm_ = std::move(samples);
            ready_.store(true, std::memory_order_release);
        }
    });
}

void AudioScrubber::Unload()
{
    Stop();
    cancel_.store(true);
    if(decoder_thread_.joinable())
    {
        decoder_thread_.join();
    }
    if(device_->initialized)
    {
        ma_device_uninit(&device_->device);
        device_->initialized = false;
    }
    ready_.store(false, std::memory_order_release);
    pcm_.clear();
    pcm_.shrink_to_fit();
    last_target_ms_ = -1;
}

void AudioScrubber::ScrubTo(qint64 position_ms)
{
    if(!IsReady() || !device_->initialized)
    {
        return;
    }

    const double target =
        static_cast<double>(position_ms) * SOURCE_RATE / 1000.0;
    const bool jump = last_target_ms_ < 0
        || std::llabs(position_ms - last_target_ms_) > JUMP_MS;
    last_target_ms_ = position_ms;

    if(!running_ || jump)
    {
        // Start (or jump) silently at the target; following drags scratch.
        reset_to_.store(target);
        reset_pending_.store(true, std::memory_order_release);
    }
    target_frame_.store(target);

    if(!running_)
    {
        running_ = ma_device_start(&device_->device) == MA_SUCCESS;
    }
}

void AudioScrubber::Stop()
{
    if(running_ && device_->initialized)
    {
        ma_device_stop(&device_->device);
    }
    running_ = false;
    last_target_ms_ = -1;
}

void AudioScrubber::Render(float* output, unsigned int frame_count)
{
    if(!IsReady())
    {
        std::fill(output, output + frame_count * 2, 0.0f);
        return;
    }

    if(reset_pending_.exchange(false, std::memory_order_acquire))
    {
        position_ = reset_to_.load();
        speed_ = 0.0;
    }

    const double last_index = static_cast<double>(pcm_.size() - 2);
    const double step = static_cast<double>(SOURCE_RATE) / device_rate_;
    const double target = std::clamp(target_frame_.load(), 0.0, last_index);
    const double follow_frames = FOLLOW_SECONDS * SOURCE_RATE;
    // Per-sample smoothing of the needle speed (~15 ms response).
    const double smoothing = 1.0 - std::exp(-1.0 / (0.015 * device_rate_));

    for(unsigned int frame = 0; frame < frame_count; ++frame)
    {
        const double desired = std::clamp(
            (target - position_) / follow_frames,
            -MAX_SPEED,
            MAX_SPEED);
        speed_ += (desired - speed_) * smoothing;
        position_ = std::clamp(position_ + speed_ * step, 0.0, last_index);

        const auto index = static_cast<std::size_t>(position_);
        const double fraction = position_ - static_cast<double>(index);
        const double sample =
            (pcm_[index] * (1.0 - fraction) + pcm_[index + 1] * fraction)
            / 32768.0;
        // Fade out as the needle stops so a still record is silent.
        const double gain =
            std::min(1.0, std::abs(speed_) * 3.0) * OUTPUT_GAIN;
        const float value = static_cast<float>(sample * gain);
        output[frame * 2] = value;
        output[frame * 2 + 1] = value;
    }
}
}
