#pragma once

#include <QString>
#include <QStringList>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

class OpenRGBPluginAPIInterface;
class RGBControllerInterface;

namespace lighttrack::openrgb
{
// Temporary flicker diagnostic. While playing, records every LED update on
// every controller (from any plugin): time, sending thread, whether it came
// from LightTrack, and each zone's average brightness. Stop() writes
// %TEMP%/lighttrack_playback_diag.csv.
class PlaybackDiagnostics
{
public:
    explicit PlaybackDiagnostics(OpenRGBPluginAPIInterface* plugin_api);
    ~PlaybackDiagnostics();

    PlaybackDiagnostics(const PlaybackDiagnostics&) = delete;
    PlaybackDiagnostics& operator=(const PlaybackDiagnostics&) = delete;

    void Start();
    void Stop();
    bool IsRunning() const { return running_; }

    // Adds a timestamped note (clip start/end, paused effects, ...).
    static void Note(const QString& message);

private:
    struct Context
    {
        PlaybackDiagnostics* owner;
        RGBControllerInterface* controller;
        int index;
    };
    struct Record
    {
        double time_ms;
        int controller;
        std::size_t thread;
        bool lighttrack;
        std::vector<std::uint8_t> zones;
    };

    static void Callback(void* context, unsigned int reason, void*);
    void Capture(const Context& context);
    double NowMs() const;
    void WriteFile();

    OpenRGBPluginAPIInterface* plugin_api_;
    bool running_ = false;
    std::chrono::steady_clock::time_point start_;
    std::vector<std::unique_ptr<Context>> contexts_;
    QStringList controller_names_;
    std::mutex mutex_;
    std::vector<Record> records_;
    QStringList notes_;
};
}
