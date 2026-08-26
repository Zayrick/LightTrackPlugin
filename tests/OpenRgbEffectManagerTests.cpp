#include "EffectManager.h"
#include "integrations/openrgb/OpenRgbColorRouter.h"

#include "ColorUtils.h"

#include <QApplication>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace
{
class RecordingController final : public RGBController
{
public:
    RecordingController()
    {
        name = "Router output";
        type = DEVICE_TYPE_VIRTUAL;
        zones.resize(1);
        zones[0].name = "Output";
        zones[0].type = ZONE_TYPE_LINEAR;
        zones[0].leds_count = 2;
        zones[0].leds_min = 2;
        zones[0].leds_max = 2;
        leds.resize(2);
        SetupColors();

        modes.resize(1);
        modes[0].name = "Direct";
        modes[0].color_mode = MODE_COLORS_PER_LED;
    }

    void UpdateLEDs() override
    {
        updates++;
    }

    void SetupZones() override
    {
    }

    void ResizeZone(int, int) override
    {
    }

    void DeviceUpdateLEDs() override
    {
    }

    void UpdateZoneLEDs(int) override
    {
    }

    void UpdateSingleLED(int) override
    {
    }

    void DeviceUpdateMode() override
    {
    }

    int updates = 0;
};

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

    manager->RemoveMapping(&first);
    manager->RemoveMapping(&second);

    RecordingController output;
    ControllerZone output_zone(&output, 0, false, 100, false);
    lighttrack::openrgb::OpenRgbColorRouter router;
    router.SetTargets({&output_zone});
    router.ConfigureLayers({
        {lighttrack::ClipId(1), 0, 0, {&output_zone}},
        {lighttrack::ClipId(2), 1, 1, {&output_zone}}
    });

    router.StartOutput();
    CHECK(output.colors[0] == ColorUtils::OFF());

    const std::vector<ControllerZone*> lower =
        router.LayerZones(lighttrack::ClipId(1));
    const std::vector<ControllerZone*> upper =
        router.LayerZones(lighttrack::ClipId(2));
    CHECK(lower.size() == 1);
    CHECK(upper.size() == 1);
    CHECK(lower[0] != upper[0]);
    CHECK(lower[0]->controller != &output);

    router.SetLayerActive(lighttrack::ClipId(1), true);
    lower[0]->SetAllZoneLEDs(ToRGBColor(255, 0, 0), 100, 0, 0);
    lower[0]->controller->UpdateLEDs();
    CHECK(output.colors[0] == ToRGBColor(255, 0, 0));

    router.SetLayerActive(lighttrack::ClipId(2), true);
    upper[0]->SetAllZoneLEDs(ToRGBColor(0, 0, 255), 100, 0, 0);
    upper[0]->controller->UpdateLEDs();
    CHECK(output.colors[0] == ToRGBColor(0, 0, 255));

    lower[0]->SetAllZoneLEDs(ToRGBColor(0, 255, 0), 100, 0, 0);
    lower[0]->controller->UpdateLEDs();
    CHECK(output.colors[0] == ToRGBColor(0, 0, 255));

    router.SetLayerActive(lighttrack::ClipId(2), false);
    CHECK(output.colors[0] == ToRGBColor(0, 255, 0));
    router.SetLayerActive(lighttrack::ClipId(1), false);
    CHECK(output.colors[0] == ColorUtils::OFF());
    return 0;
}
