#include "OpenRgbTestHost.h"
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
class RecordingController final : public TestController
{
public:
    using TestController::TestController;

    void UpdateLEDs() override
    {
        frames.push_back(colors);
        TestController::UpdateLEDs();
    }

    std::vector<std::vector<RGBColor>> frames;
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

    TestPluginAPI api;
    TestController output(TestController::LinearSetup());
    ControllerZone output_zone(&output, 0, false, 100, true, false);
    lighttrack::openrgb::OpenRgbColorRouter router(&api);
    router.SetTargets({&output_zone});
    router.ConfigureLayers({
        {lighttrack::ClipId(1), 0, 0, {&output_zone}},
        {lighttrack::ClipId(2), 1, 1, {&output_zone}}
    });

    const RGBColor white = ToRGBColor(255, 255, 255);
    output.SetAllColors(white);
    router.RefreshIdleOutput();
    CHECK(output.updates == 0);
    CHECK(output.colors[0] == white);

    router.StartOutput();
    CHECK(output.colors[0] == ColorUtils::OFF());
    // Pre-roll keeps clearing outside writes, even before any clip starts.
    for(int frame = 0; frame < 3; ++frame)
    {
        const unsigned int updates = output.updates;
        output.SetAllColors(white);
        router.RefreshIdleOutput();
        CHECK(output.updates == updates + 1);
        CHECK(output.colors == std::vector<RGBColor>(2, ColorUtils::OFF()));
    }

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
    const unsigned int active_updates = output.updates;
    router.RefreshIdleOutput();
    CHECK(output.updates == active_updates);
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
    CHECK(lower[0]->controller != upper[0]->controller);
    CHECK(lower[0]->controller->GetColor(0) == ToRGBColor(0, 255, 0));
    CHECK(upper[0]->controller->GetColor(0) == ToRGBColor(0, 0, 255));
    // Ending the last clip must not stop black frames during the music tail.
    for(int frame = 0; frame < 3; ++frame)
    {
        const unsigned int updates = output.updates;
        output.SetAllColors(white);
        router.RefreshIdleOutput();
        CHECK(output.updates == updates + 1);
        CHECK(output.colors == std::vector<RGBColor>(2, ColorUtils::OFF()));
    }

    // Pause/stop/end disables refreshes; a later playback can be clip-free.
    router.StopOutput();
    const unsigned int stopped_updates = output.updates;
    output.SetAllColors(white);
    for(int frame = 0; frame < 3; ++frame)
    {
        router.RefreshIdleOutput();
    }
    CHECK(output.updates == stopped_updates);
    CHECK(output.colors == std::vector<RGBColor>(2, white));
    router.ConfigureLayers({});
    router.StartOutput();
    const unsigned int resumed_updates = output.updates;
    for(int frame = 0; frame < 3; ++frame)
    {
        router.RefreshIdleOutput();
    }
    CHECK(output.updates == resumed_updates + 3);
    CHECK(output.colors == std::vector<RGBColor>(2, ColorUtils::OFF()));
    router.Reset();
    const unsigned int reset_updates = output.updates;
    router.RefreshIdleOutput();
    CHECK(output.updates == reset_updates);
    CHECK(api.virtual_controllers.empty());
    CHECK(api.created == api.deleted);

    RGBController_Setup setup = TestController::LinearSetup();
    setup.zones[0].type = ZONE_TYPE_MATRIX;
    setup.zones[0].leds_count = 4;
    setup.zones[0].matrix_map.width = 2;
    setup.zones[0].matrix_map.height = 2;
    setup.zones[0].matrix_map.map = {0, 1, 2, 3};
    segment part;
    part.name = "Middle";
    part.type = ZONE_TYPE_LINEAR;
    part.start_idx = 1;
    part.leds_count = 2;
    setup.zones[0].segments.push_back(part);
    segment tail = part;
    tail.name = "Tail";
    tail.start_idx = 3;
    tail.leds_count = 1;
    setup.zones[0].segments.push_back(tail);
    setup.zones.push_back(TestController::LinearSetup().zones[0]);
    RecordingController segmented(std::move(setup));
    ControllerZone segment_zone(&segmented, 0, false, 100, true, true, 0);
    ControllerZone idle_segment(&segmented, 0, false, 100, true, true, 1);
    ControllerZone second_zone(&segmented, 1, false, 100, true, false);
    router.SetTargets({&segment_zone, &idle_segment, &second_zone});
    router.ConfigureLayers({
        {lighttrack::ClipId(3), 0, 0, {&segment_zone, &second_zone}},
        {lighttrack::ClipId(4), 2, 1, {&segment_zone}},
        {lighttrack::ClipId(5), 1, 2, {&second_zone}}
    });
    const auto base = router.LayerZones(lighttrack::ClipId(3));
    const auto overlay = router.LayerZones(lighttrack::ClipId(4));
    CHECK(base.size() == 2 && overlay.size() == 1);
    CHECK(base[0]->is_segment && base[0]->segment_idx == 0);
    CHECK(base[0]->start_idx() == 1 && base[0]->leds_count() == 2);
    CHECK(base[1]->start_idx() == 4);
    CHECK(base[0]->controller->GetZoneMatrixMap(0).map == std::vector<unsigned int>({0, 1, 2, 3}));
    segmented.setup.zones[0].matrix_map.map[0] = 99;
    CHECK(base[0]->controller->GetZoneMatrixMap(0).map[0] == 0);
    router.StartOutput();
    router.SetLayerActive(lighttrack::ClipId(3), true);
    base[0]->SetAllZoneLEDs(ToRGBColor(255, 0, 0), 100, 0, 0);
    base[1]->SetAllZoneLEDs(ToRGBColor(0, 0, 255), 100, 0, 0);
    base[0]->controller->UpdateLEDs();
    CHECK(segmented.colors == std::vector<RGBColor>({0, ToRGBColor(255, 0, 0), ToRGBColor(255, 0, 0), 0, ToRGBColor(0, 0, 255), ToRGBColor(0, 0, 255)}));
    router.SetLayerActive(lighttrack::ClipId(4), true);
    overlay[0]->SetAllZoneLEDs(ToRGBColor(0, 255, 0), 100, 0, 0);
    overlay[0]->controller->UpdateLEDs();
    CHECK(segmented.colors[1] == ToRGBColor(0, 255, 0));
    CHECK(segmented.colors[4] == ToRGBColor(0, 0, 255));
    router.SetLayerActive(lighttrack::ClipId(4), false);
    CHECK(segmented.colors[1] == ToRGBColor(255, 0, 0));

    // Only the child segment plays; both parent lanes are empty. Every
    // submitted frame must retain its color while idle siblings go black.
    router.SetLayerActive(lighttrack::ClipId(3), false);
    router.SetLayerActive(lighttrack::ClipId(4), true);
    overlay[0]->SetAllZoneLEDs(ToRGBColor(0, 255, 0), 100, 0, 0);
    overlay[0]->controller->UpdateLEDs();
    segmented.colors[0] = white; // Outside all configured segments.
    for(int frame = 0; frame < 3; ++frame)
    {
        idle_segment.SetAllZoneLEDs(white, 100, 0, 0);
        second_zone.SetAllZoneLEDs(white, 100, 0, 0);
        const std::size_t submitted = segmented.frames.size();
        router.RefreshIdleOutput();
        CHECK(segmented.frames.size() == submitted + 1);
        CHECK(segmented.frames.back() == std::vector<RGBColor>({
            white, ToRGBColor(0, 255, 0), ToRGBColor(0, 255, 0), 0, 0, 0}));
    }

    // The zone-level effect also survives idle segments on the same device.
    router.SetLayerActive(lighttrack::ClipId(4), false);
    router.SetLayerActive(lighttrack::ClipId(5), true);
    const auto zone_effect = router.LayerZones(lighttrack::ClipId(5));
    zone_effect[0]->SetAllZoneLEDs(ToRGBColor(0, 0, 255), 100, 0, 0);
    zone_effect[0]->controller->UpdateLEDs();
    for(int frame = 0; frame < 3; ++frame)
    {
        segment_zone.SetAllZoneLEDs(white, 100, 0, 0);
        idle_segment.SetAllZoneLEDs(white, 100, 0, 0);
        const std::size_t submitted = segmented.frames.size();
        router.RefreshIdleOutput();
        CHECK(segmented.frames.size() == submitted + 1);
        CHECK(segmented.frames.back() == std::vector<RGBColor>({
            white, 0, 0, 0, ToRGBColor(0, 0, 255), ToRGBColor(0, 0, 255)}));
    }

    // Highlight/disable overrides still take precedence over idle and effects.
    router.SetOverrides({{&idle_segment, white}, {&second_zone, ColorUtils::OFF()}});
    idle_segment.SetAllZoneLEDs(ColorUtils::OFF(), 100, 0, 0);
    second_zone.SetAllZoneLEDs(white, 100, 0, 0);
    router.RefreshIdleOutput();
    CHECK(segmented.frames.back() == std::vector<RGBColor>({white, 0, 0, white, 0, 0}));
    router.SetOverrides({});
    router.RefreshIdleOutput();
    CHECK(segmented.frames.back() == std::vector<RGBColor>({
        white, 0, 0, 0, ToRGBColor(0, 0, 255), ToRGBColor(0, 0, 255)}));
    router.StopOutput();
    router.Reset();
    CHECK(api.virtual_controllers.empty());
    CHECK(api.created == api.deleted);

    manager->SetUnresolvedZones(&first, {segment_zone.to_json()});
    CHECK(manager->GetUnresolvedZones(&first).size() == 1);
    manager->RemapAssignedZones({&segment_zone});
    CHECK(manager->GetUnresolvedZones(&first).empty());
    CHECK(manager->GetAssignedZones(&first) == std::vector<ControllerZone*>({&segment_zone}));
    manager->RemoveMapping(&first);
    manager->SetUnresolvedZones(&first, {segment_zone.to_json()});
    manager->ClearAssignments();
    CHECK(manager->GetUnresolvedZones(&first).empty());
    return 0;
}
