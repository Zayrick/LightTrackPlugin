#pragma once

#include "core/WaveformPeaks.h"

#include <QVector>

class QString;

namespace lighttrack::audio
{
// Produces UI-ready normalized samples without exposing decoder state.
QVector<qreal> AnalyzeWaveform(const QString& path, int bar_count);
// Per-channel min/max peaks (left/right) for the detailed waveform.
WaveformPeaks AnalyzeWaveformPeaks(const QString& path);
}
