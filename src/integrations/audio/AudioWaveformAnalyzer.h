#pragma once

#include <QVector>

class QString;

namespace lighttrack::audio
{
// Produces UI-ready normalized samples without exposing decoder state.
QVector<qreal> AnalyzeWaveform(const QString& path, int bar_count);
}
