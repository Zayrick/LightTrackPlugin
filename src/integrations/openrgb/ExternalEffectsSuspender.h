#pragma once

#include <QPointer>
#include <QVector>

class QAbstractButton;

namespace lighttrack::openrgb
{
// Pauses effects running in a separately installed OpenRGB Effects plugin
// while LightTrack playback owns the controllers, then restarts exactly the
// effects it stopped. The Effects plugin lives in another library, so it is
// reached through its Qt widgets rather than through shared C++ state.
class ExternalEffectsSuspender
{
public:
    ExternalEffectsSuspender() = default;
    ~ExternalEffectsSuspender();

    ExternalEffectsSuspender(const ExternalEffectsSuspender&) = delete;
    ExternalEffectsSuspender& operator=(
        const ExternalEffectsSuspender&) = delete;

    void Suspend();
    void Resume();

private:
    QVector<QPointer<QAbstractButton>> stopped_toggles;
    bool suspended = false;
};
}
