#include "integrations/openrgb/PlaybackDiagnostics.h"

#include "integrations/openrgb/OpenRgbRenderCoordinator.h"

#include "OpenRGBPluginInterface.h"
#include "RGBControllerInterface.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

#include <atomic>
#include <functional>
#include <thread>

namespace lighttrack::openrgb
{
namespace
{
constexpr std::size_t MAX_RECORDS = 60000;
std::atomic<PlaybackDiagnostics*> active_instance{nullptr};
}

PlaybackDiagnostics::PlaybackDiagnostics(OpenRGBPluginAPIInterface* plugin_api) :
    plugin_api_(plugin_api)
{
}

PlaybackDiagnostics::~PlaybackDiagnostics()
{
    Stop();
}

double PlaybackDiagnostics::NowMs() const
{
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start_).count();
}

void PlaybackDiagnostics::Start()
{
    // Opt-in: hooking every device costs time on every frame.
    static const bool enabled =
        qEnvironmentVariableIntValue("LIGHTTRACK_DIAGNOSTICS") != 0;
    if(!enabled || running_ || plugin_api_ == nullptr)
    {
        return;
    }

    start_ = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> guard(mutex_);
        records_.clear();
        records_.reserve(8192);
        notes_.clear();
    }
    controller_names_.clear();
    contexts_.clear();

    const std::vector<RGBControllerInterface*> controllers =
        plugin_api_->GetRGBControllers();
    for(int index = 0; index < static_cast<int>(controllers.size()); ++index)
    {
        RGBControllerInterface* controller = controllers[index];
        if(controller == nullptr)
        {
            continue;
        }
        QStringList zones;
        for(unsigned int zone = 0; zone < controller->GetZoneCount(); ++zone)
        {
            zones << QString::fromStdString(controller->GetZoneDisplayName(zone));
        }
        controller_names_ << QString("%1 | %2 | %3 | zones: %4")
            .arg(index)
            .arg(QString::fromStdString(controller->GetName()))
            .arg(QString::fromStdString(controller->GetVendor()))
            .arg(zones.join("; "));
        contexts_.push_back(std::make_unique<Context>(
            Context{this, controller, index}));
        controller->RegisterUpdateCallback(
            &PlaybackDiagnostics::Callback,
            contexts_.back().get());
    }
    running_ = true;
    active_instance.store(this);
    Note("playback started");
}

void PlaybackDiagnostics::Stop()
{
    if(!running_)
    {
        return;
    }
    Note("playback stopped");
    active_instance.store(nullptr);
    for(const auto& context : contexts_)
    {
        context->controller->UnregisterUpdateCallback(context.get());
    }
    running_ = false;
    WriteFile();
    // Contexts stay alive until the next Start(): a callback may still be
    // finishing on another thread right after unregistering.
}

void PlaybackDiagnostics::Note(const QString& message)
{
    PlaybackDiagnostics* instance = active_instance.load();
    if(instance == nullptr)
    {
        return;
    }
    std::lock_guard<std::mutex> guard(instance->mutex_);
    instance->notes_ << QString("%1 ms: %2")
        .arg(instance->NowMs(), 0, 'f', 1)
        .arg(message);
}

void PlaybackDiagnostics::Callback(void* context, unsigned int reason, void*)
{
    if(reason != RGBCONTROLLER_UPDATE_REASON_UPDATELEDS)
    {
        return;
    }
    const auto* typed = static_cast<const Context*>(context);
    typed->owner->Capture(*typed);
}

void PlaybackDiagnostics::Capture(const Context& context)
{
    Record record;
    record.time_ms = NowMs();
    record.controller = context.index;
    record.thread = std::hash<std::thread::id>()(std::this_thread::get_id());
    record.lighttrack = IsSubmittingOutput();

    RGBControllerInterface* controller = context.controller;
    const unsigned int zone_count = controller->GetZoneCount();
    record.zones.reserve(zone_count);
    for(unsigned int zone = 0; zone < zone_count; ++zone)
    {
        const unsigned int start = controller->GetZoneStartIndex(zone);
        const unsigned int count = controller->GetLEDsInZone(zone);
        unsigned long long sum = 0;
        for(unsigned int led = 0; led < count; ++led)
        {
            const RGBColor color = controller->GetColor(start + led);
            sum += (RGBGetRValue(color) + RGBGetGValue(color) + RGBGetBValue(color)) / 3;
        }
        record.zones.push_back(static_cast<std::uint8_t>(
            count > 0 ? sum / count : 0));
    }

    std::lock_guard<std::mutex> guard(mutex_);
    if(records_.size() < MAX_RECORDS)
    {
        records_.push_back(std::move(record));
    }
}

void PlaybackDiagnostics::WriteFile()
{
    QFile file(QDir::temp().filePath("lighttrack_playback_diag.csv"));
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        return;
    }
    QTextStream out(&file);
    std::lock_guard<std::mutex> guard(mutex_);
    out << "# controllers (index | name | vendor | zones)\n";
    for(const QString& name : controller_names_)
    {
        out << "# " << name << "\n";
    }
    out << "# notes\n";
    for(const QString& note : notes_)
    {
        out << "# " << note << "\n";
    }
    out << "time_ms,controller,thread,lighttrack,zone_brightness...\n";
    for(const Record& record : records_)
    {
        out << QString::number(record.time_ms, 'f', 2) << ','
            << record.controller << ','
            << QString::number(static_cast<qulonglong>(record.thread % 100000)) << ','
            << (record.lighttrack ? 1 : 0);
        for(std::uint8_t value : record.zones)
        {
            out << ',' << static_cast<int>(value);
        }
        out << "\n";
    }
}
}
