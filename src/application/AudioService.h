#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace lighttrack
{
class AudioService
{
public:
    virtual ~AudioService() = default;

    virtual bool Open(const QString& path) = 0;
    virtual void Close() = 0;

    virtual bool Play() = 0;
    virtual void Pause() = 0;
    virtual bool Seek(qint64 position_ms) = 0;

    virtual bool IsAtEnd() const = 0;
    virtual qint64 PositionMs() const = 0;
    virtual qint64 DurationMs() const = 0;

    virtual QVector<qreal> AnalyzeWaveform(
        const QString& path,
        int bar_count) const = 0;
};
}
