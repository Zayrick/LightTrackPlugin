#pragma once

#include <QVector>

namespace lighttrack
{
// Per-channel waveform peaks for drawing the music track: for every bucket
// (1 / buckets_per_second seconds) the lowest and highest sample of the left
// and right channel, normalized so the loudest peak is 1.
struct WaveformPeaks
{
    double buckets_per_second = 0.0;
    QVector<float> min[2];
    QVector<float> max[2];

    bool IsEmpty() const { return min[0].isEmpty(); }
    int BucketCount() const { return static_cast<int>(min[0].size()); }
};
}
