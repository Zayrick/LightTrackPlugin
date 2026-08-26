#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>
#include <memory>

namespace lighttrack::audio
{
class MiniaudioAudioService
{
public:
    MiniaudioAudioService();
    ~MiniaudioAudioService();

    MiniaudioAudioService(const MiniaudioAudioService&) = delete;
    MiniaudioAudioService& operator=(
        const MiniaudioAudioService&) = delete;

    bool Open(const QString& path);
    void Close();

    bool Play();
    void Pause();
    bool Seek(qint64 position_ms);

    bool IsAtEnd() const;
    qint64 PositionMs() const;
    qint64 DurationMs() const;

    QVector<qreal> AnalyzeWaveform(
        const QString& path,
        int bar_count) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
