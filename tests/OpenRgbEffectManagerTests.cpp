#include "EffectManager.h"

#include <QApplication>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace
{
class CountingEffect final : public RGBEffect
{
public:
    CountingEffect(
        std::atomic<int>& concurrent_steps,
        std::atomic<int>& maximum_concurrent_steps) :
        concurrent_steps_(concurrent_steps),
        maximum_concurrent_steps_(maximum_concurrent_steps)
    {
        EffectDetails.EffectName = "Effect manager test";
    }

    void StepEffect(std::vector<ControllerZone*>) override
    {
        const int concurrent = concurrent_steps_.fetch_add(1) + 1;
        int maximum = maximum_concurrent_steps_.load();
        while(concurrent > maximum
            && !maximum_concurrent_steps_.compare_exchange_weak(
                maximum,
                concurrent))
        {
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        steps_.fetch_add(1);
        concurrent_steps_.fetch_sub(1);
    }

    int Steps() const
    {
        return steps_.load();
    }

private:
    std::atomic<int>& concurrent_steps_;
    std::atomic<int>& maximum_concurrent_steps_;
    std::atomic<int> steps_{0};
};

bool WaitForSteps(
    const CountingEffect& first,
    const CountingEffect& second,
    int minimum_steps)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while(std::chrono::steady_clock::now() < deadline)
    {
        if(first.Steps() >= minimum_steps
            && second.Steps() >= minimum_steps)
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

#define CHECK(condition)                                                    \
    do                                                                      \
    {                                                                       \
        if(!(condition))                                                    \
        {                                                                   \
            std::cerr << "CHECK failed at line " << __LINE__ << ": "       \
                      << #condition << '\n';                                \
            return 1;                                                       \
        }                                                                   \
    } while(false)
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);

    std::atomic<int> concurrent_steps{0};
    std::atomic<int> maximum_concurrent_steps{0};
    CountingEffect first(concurrent_steps, maximum_concurrent_steps);
    CountingEffect second(concurrent_steps, maximum_concurrent_steps);
    first.SetFPS(60);
    second.SetFPS(60);

    EffectManager* manager = EffectManager::Get();
    manager->Assign({}, &first);
    manager->Assign({}, &second);
    manager->SetEffectActive(&first);
    manager->SetEffectActive(&second);

    CHECK(WaitForSteps(first, second, 3));
    manager->SetEffectUnActive(&first);
    manager->SetEffectUnActive(&second);

    CHECK(!manager->IsActive(&first));
    CHECK(!manager->IsActive(&second));
    CHECK(maximum_concurrent_steps.load() == 1);

    const int first_stopped_at = first.Steps();
    const int second_stopped_at = second.Steps();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(first.Steps() == first_stopped_at);
    CHECK(second.Steps() == second_stopped_at);

    // A malformed persisted FPS of zero must not divide by zero, and stopping
    // a one-FPS worker must not wait for its full frame interval.
    first.SetFPS(0);
    const auto stop_started = std::chrono::steady_clock::now();
    manager->SetEffectActive(&first);
    const auto step_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while(first.Steps() == first_stopped_at
        && std::chrono::steady_clock::now() < step_deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool zero_fps_stepped = first.Steps() > first_stopped_at;
    manager->SetEffectUnActive(&first);
    CHECK(zero_fps_stepped);
    const auto stop_duration =
        std::chrono::steady_clock::now() - stop_started;
    CHECK(stop_duration < std::chrono::milliseconds(250));

    manager->RemoveMapping(&first);
    manager->RemoveMapping(&second);
    return 0;
}
