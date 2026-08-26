#include "integrations/audio/MiniaudioAudioService.h"

#include "integrations/audio/AudioWaveformAnalyzer.h"

#include <QByteArray>
#include <QDir>

#include <algorithm>
#include <string>

#include "miniaudio.h"

namespace lighttrack::audio
{
class MiniaudioAudioService::Impl
{
public:
    ma_engine engine{};
    ma_sound sound{};
    qint64 duration_ms = 0;
    bool engine_ready = false;
    bool sound_ready = false;
};

MiniaudioAudioService::MiniaudioAudioService() :
    impl_(std::make_unique<Impl>())
{
}

MiniaudioAudioService::~MiniaudioAudioService()
{
    Close();
}

bool MiniaudioAudioService::Open(const QString& path)
{
    Close();
    if(path.isEmpty())
    {
        return false;
    }

    if(!impl_->engine_ready)
    {
        impl_->engine_ready =
            ma_engine_init(nullptr, &impl_->engine)
            == MA_SUCCESS;
        if(!impl_->engine_ready)
        {
            return false;
        }
    }

    const ma_uint32 flags =
        MA_SOUND_FLAG_STREAM
        | MA_SOUND_FLAG_NO_SPATIALIZATION;
#ifdef _WIN32
    const std::wstring native_path =
        QDir::toNativeSeparators(path).toStdWString();
    const ma_result result = ma_sound_init_from_file_w(
        &impl_->engine,
        native_path.c_str(),
        flags,
        nullptr,
        nullptr,
        &impl_->sound);
#else
    const QByteArray native_path = path.toLocal8Bit();
    const ma_result result = ma_sound_init_from_file(
        &impl_->engine,
        native_path.constData(),
        flags,
        nullptr,
        nullptr,
        &impl_->sound);
#endif

    if(result != MA_SUCCESS)
    {
        ma_engine_uninit(&impl_->engine);
        impl_->engine_ready = false;
        return false;
    }

    impl_->sound_ready = true;
    float duration_seconds = 0.0f;
    if(ma_sound_get_length_in_seconds(
        &impl_->sound,
        &duration_seconds) == MA_SUCCESS)
    {
        impl_->duration_ms = std::max<qint64>(
            0,
            static_cast<qint64>(
                duration_seconds * 1000.0f + 0.5f));
    }
    return true;
}

void MiniaudioAudioService::Close()
{
    if(impl_->sound_ready)
    {
        ma_sound_stop(&impl_->sound);
        ma_sound_uninit(&impl_->sound);
    }
    impl_->sound_ready = false;
    impl_->duration_ms = 0;

    if(impl_->engine_ready)
    {
        ma_engine_uninit(&impl_->engine);
        impl_->engine_ready = false;
    }
}

bool MiniaudioAudioService::Play()
{
    return impl_->sound_ready
        && ma_sound_start(&impl_->sound) == MA_SUCCESS;
}

void MiniaudioAudioService::Pause()
{
    if(impl_->sound_ready)
    {
        ma_sound_stop(&impl_->sound);
    }
}

bool MiniaudioAudioService::Seek(qint64 position_ms)
{
    if(!impl_->sound_ready)
    {
        return false;
    }

    const qint64 maximum =
        impl_->duration_ms > 0
        ? impl_->duration_ms
        : std::max<qint64>(0, position_ms);
    const qint64 clamped = std::clamp<qint64>(
        position_ms,
        0,
        maximum);
    return ma_sound_seek_to_second(
        &impl_->sound,
        static_cast<float>(clamped) / 1000.0f)
        == MA_SUCCESS;
}

bool MiniaudioAudioService::IsAtEnd() const
{
    return impl_->sound_ready
        && ma_sound_at_end(&impl_->sound);
}

qint64 MiniaudioAudioService::PositionMs() const
{
    if(!impl_->sound_ready)
    {
        return 0;
    }

    float position_seconds = 0.0f;
    if(ma_sound_get_cursor_in_seconds(
        &impl_->sound,
        &position_seconds) != MA_SUCCESS)
    {
        return 0;
    }

    return std::max<qint64>(
        0,
        static_cast<qint64>(
            position_seconds * 1000.0f + 0.5f));
}

qint64 MiniaudioAudioService::DurationMs() const
{
    return impl_->duration_ms;
}

QVector<qreal> MiniaudioAudioService::AnalyzeWaveform(
    const QString& path,
    int bar_count) const
{
    return lighttrack::audio::AnalyzeWaveform(
        path,
        bar_count);
}

}
