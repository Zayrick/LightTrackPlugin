#pragma once

#include "application/AudioService.h"

#include <memory>

namespace lighttrack::audio
{
class MiniaudioAudioService final : public AudioService
{
public:
    MiniaudioAudioService();
    ~MiniaudioAudioService() override;

    MiniaudioAudioService(const MiniaudioAudioService&) = delete;
    MiniaudioAudioService& operator=(
        const MiniaudioAudioService&) = delete;

    bool Open(const QString& path) override;
    void Close() override;

    bool Play() override;
    void Pause() override;
    bool Seek(qint64 position_ms) override;

    bool IsAtEnd() const override;
    qint64 PositionMs() const override;
    qint64 DurationMs() const override;

    QVector<qreal> AnalyzeWaveform(
        const QString& path,
        int bar_count) const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<AudioService> CreateMiniaudioAudioService();
}
