#include "integrations/openrgb/PerformanceStats.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

#include <algorithm>
#include <map>
#include <mutex>

namespace lighttrack::openrgb::performance
{
namespace
{
struct Stat
{
    long long count = 0;
    double total = 0.0;
    double max = 0.0;

    void Add(double value)
    {
        ++count;
        total += value;
        max = std::max(max, value);
    }

    QString Format() const
    {
        return count == 0
            ? QString("-")
            : QString("avg %1 ms, max %2 ms")
                  .arg(total / count, 0, 'f', 2)
                  .arg(max, 0, 'f', 2);
    }
};

struct EffectStats
{
    unsigned int target_fps = 0;
    Stat wait_step;
    Stat step;
    Stat wait_publish;
    Stat publish;
};

std::mutex& StatsMutex()
{
    static std::mutex mutex;
    return mutex;
}

struct State
{
    Clock::time_point start = Clock::now();
    std::map<std::string, EffectStats> effects;
    std::map<std::string, Stat> ui;
};

State& GetState()
{
    static State state;
    return state;
}
}

void Reset()
{
    std::lock_guard<std::mutex> guard(StatsMutex());
    GetState() = State{};
}

void RecordEffectFrame(
    const std::string& effect_name,
    unsigned int target_fps,
    double wait_step_ms,
    double step_ms,
    double wait_publish_ms,
    double publish_ms)
{
    std::lock_guard<std::mutex> guard(StatsMutex());
    EffectStats& stats = GetState().effects[effect_name];
    stats.target_fps = target_fps;
    stats.wait_step.Add(wait_step_ms);
    stats.step.Add(step_ms);
    stats.wait_publish.Add(wait_publish_ms);
    stats.publish.Add(publish_ms);
}

void RecordUi(const char* name, double ms)
{
    std::lock_guard<std::mutex> guard(StatsMutex());
    GetState().ui[name].Add(ms);
}

void WriteReport()
{
    std::lock_guard<std::mutex> guard(StatsMutex());
    const State& state = GetState();
    if(state.ui.count("playback tick") == 0 && state.effects.empty())
    {
        return;
    }
    const double seconds = std::max(
        0.001,
        std::chrono::duration<double>(Clock::now() - state.start).count());

    QFile file(QDir::temp().filePath("lighttrack_perf.txt"));
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        return;
    }
    const QString newline = QStringLiteral("\n");
    QTextStream out(&file);
    out << "LightTrack playback performance over "
        << QString::number(seconds, 'f', 1) << " s" << newline << newline;
    out << "UI thread:" << newline;
    for(const auto& entry : state.ui)
    {
        out << "- " << QString::fromStdString(entry.first) << ": "
            << entry.second.count << " times ("
            << QString::number(entry.second.count / seconds, 'f', 1)
            << "/s), " << entry.second.Format() << newline;
    }
    out << newline;
    out << "Effects (frames summed over every clip using that effect):"
        << newline;
    for(const auto& entry : state.effects)
    {
        const EffectStats& stats = entry.second;
        out << "- " << QString::fromStdString(entry.first)
            << ": " << stats.step.count << " frames ("
            << QString::number(stats.step.count / seconds, 'f', 1)
            << " fps total, target " << stats.target_fps << " per clip)"
            << newline
            << "    wait for turn: " << stats.wait_step.Format() << newline
            << "    compute:       " << stats.step.Format() << newline
            << "    wait for lock: " << stats.wait_publish.Format() << newline
            << "    publish:       " << stats.publish.Format() << newline;
    }
}
}
