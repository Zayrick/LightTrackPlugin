#include "integrations/audio/AudioWaveformAnalyzer.h"

#include <QByteArray>
#include <QDir>
#include <QString>

#include <algorithm>
#include <cmath>

#include "miniaudio.h"

namespace lighttrack::audio
{
QVector<qreal> AnalyzeWaveform(const QString& path, int bar_count)
{
    if(path.isEmpty() || bar_count <= 0)
    {
        return {};
    }

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 0);
    ma_decoder decoder{};

#ifdef _WIN32
    const std::wstring native_path = QDir::toNativeSeparators(path).toStdWString();
    const ma_result init_result = ma_decoder_init_file_w(native_path.c_str(), &config, &decoder);
#else
    const QByteArray native_path = path.toLocal8Bit();
    const ma_result init_result = ma_decoder_init_file(native_path.constData(), &config, &decoder);
#endif

    if(init_result != MA_SUCCESS)
    {
        return {};
    }

    QVector<float> chunk(4096);
    ma_uint64 total_frame_count = 0;
    if(ma_decoder_get_length_in_pcm_frames(
        &decoder,
        &total_frame_count) != MA_SUCCESS
        || total_frame_count == 0)
    {
        // Some decoder backends cannot report their length up front. Count a
        // first pass without retaining samples, then rewind for aggregation.
        for(;;)
        {
            ma_uint64 frames_read = 0;
            const ma_result read_result =
                ma_decoder_read_pcm_frames(
                    &decoder,
                    chunk.data(),
                    static_cast<ma_uint64>(chunk.size()),
                    &frames_read);
            total_frame_count += frames_read;
            if(frames_read == 0 || read_result != MA_SUCCESS)
            {
                break;
            }
        }

        if(total_frame_count == 0
            || ma_decoder_seek_to_pcm_frame(
                &decoder,
                0) != MA_SUCCESS)
        {
            ma_decoder_uninit(&decoder);
            return {};
        }
    }

    QVector<qreal> squared_sums(bar_count, 0.0);
    QVector<ma_uint64> sample_counts(bar_count, 0);
    ma_uint64 frame_index = 0;
    for(;;)
    {
        ma_uint64 frames_read = 0;
        const ma_result read_result =
            ma_decoder_read_pcm_frames(
                &decoder,
                chunk.data(),
                static_cast<ma_uint64>(chunk.size()),
                &frames_read);

        for(ma_uint64 index = 0; index < frames_read; ++index)
        {
            const long double scaled_position =
                static_cast<long double>(frame_index + index)
                * static_cast<long double>(bar_count)
                / static_cast<long double>(total_frame_count);
            const int bar = std::min(
                bar_count - 1,
                static_cast<int>(scaled_position));
            const qreal sample =
                chunk[static_cast<int>(index)];
            squared_sums[bar] += sample * sample;
            ++sample_counts[bar];
        }
        frame_index += frames_read;

        if(frames_read == 0 || read_result != MA_SUCCESS)
        {
            break;
        }
    }

    ma_decoder_uninit(&decoder);
    if(frame_index == 0)
    {
        return {};
    }

    QVector<qreal> levels(bar_count, 0.0);
    qreal peak = 0.0;
    for(int bar = 0; bar < bar_count; ++bar)
    {
        if(sample_counts[bar] == 0)
        {
            continue;
        }

        levels[bar] = std::sqrt(
            squared_sums[bar]
            / static_cast<qreal>(sample_counts[bar]));
        peak = std::max(peak, levels[bar]);
    }

    if(peak > 0.0)
    {
        for(qreal& level : levels)
        {
            level = std::clamp<qreal>(level / peak, 0.02, 1.0);
        }
    }

    return levels;
}
}
