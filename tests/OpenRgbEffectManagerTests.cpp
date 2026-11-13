#include "OpenRgbTestHost.h"
#include "EffectManager.h"
#include "integrations/openrgb/OpenRgbColorRouter.h"
#include "integrations/openrgb/OpenRgbRenderCoordinator.h"

#include "ColorUtils.h"

#include <QApplication>
#include <QElapsedTimer>

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

// An effect whose StepEffect waits until released, to check that a slow
// effect does not hold the frame lock the UI thread needs.
class BlockingEffect final : public RGBEffect
{
public:
    void StepEffect(std::vector<ControllerZone*>) override
    {
        inside.store(true);
        while(!release.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        inside.store(false);
    }

    std::atomic<bool> inside{false};
    std::atomic<bool> release{false};
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

    // A slow effect computing its frame must not block the frame lock the
    // UI thread takes on every playback tick.
    {
        BlockingEffect slow;
        slow.SetFPS(60);
        manager->Assign({}, &slow);
        manager->SetEffectActive(&slow);
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while(!slow.inside.load()
            && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(slow.inside.load());
        bool locked = lighttrack::openrgb::ControllerFrameMutex().try_lock();
        if(locked)
        {
            lighttrack::openrgb::ControllerFrameMutex().unlock();
        }
        slow.release.store(true);
        manager->SetEffectUnActive(&slow);
        manager->RemoveMapping(&slow);
        CHECK(locked);
    }

    manager->RemoveMapping(&first);
    manager->RemoveMapping(&second);

    TestPluginAPI api;
    // Physical hardware: idle zones are refreshed with black while playing.
    RGBController_Setup physical_setup = TestController::LinearSetup();
    physical_setup.type = DEVICE_TYPE_LEDSTRIP;
    TestController output(physical_setup);
    ControllerZone output_zone(&output, 0, false, 100, true, false);
    lighttrack::openrgb::OpenRgbColorRouter router(&api);
    router.SetTargets({&output_zone});
    router.ConfigureLayers({
        {lighttrack::ClipId(1), 0, 0, {&output_zone}},
        {lighttrack::ClipId(2), 1, 1, {&output_zone}}
    });
    // Clip layers are in-memory buffers: no OpenRGB virtual controllers,
    // so no per-clip device threads.
    CHECK(api.created == 0);

    const RGBColor white = ToRGBColor(255, 255, 255);
    output.SetAllColors(white);
    router.RefreshIdleOutput();
    CHECK(output.updates == 0);
    CHECK(output.colors[0] == white);

    router.StartOutput();
    CHECK(output.colors[0] == ColorUtils::OFF());
    // Idle zones are cleared once at start; afterwards outside writes (e.g.
    // Visual Map painting the same LEDs) are left alone instead of fought.
    {
        const unsigned int updates = output.updates;
        output.SetAllColors(white);
        for(int frame = 0; frame < 3; ++frame)
        {
            router.RefreshIdleOutput();
        }
        CHECK(output.updates == updates);
        CHECK(output.colors == std::vector<RGBColor>(2, white));
        output.SetAllColors(ColorUtils::OFF());
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
    // Ending the last clip cleared the zone once (checked above); after that
    // the zone is not repainted every frame.
    {
        const unsigned int updates = output.updates;
        output.SetAllColors(white);
        for(int frame = 0; frame < 3; ++frame)
        {
            router.RefreshIdleOutput();
        }
        CHECK(output.updates == updates);
        CHECK(output.colors == std::vector<RGBColor>(2, white));
    }
    // A clip playing again and ending clears it once more.
    router.SetLayerActive(lighttrack::ClipId(1), true);
    router.SetLayerActive(lighttrack::ClipId(1), false);
    CHECK(output.colors == std::vector<RGBColor>(2, ColorUtils::OFF()));

    // Pause/stop/end disables refreshes.
    router.StopOutput();
    const unsigned int stopped_updates = output.updates;
    output.SetAllColors(white);
    for(int frame = 0; frame < 3; ++frame)
    {
        router.RefreshIdleOutput();
    }
    CHECK(output.updates == stopped_updates);
    CHECK(output.colors == std::vector<RGBColor>(2, white));

    // A device without any clip is never written during playback: another
    // plugin (e.g. a Visual Map virtual device) may be driving it.
    router.ConfigureLayers({});
    router.StartOutput();
    const unsigned int resumed_updates = output.updates;
    for(int frame = 0; frame < 3; ++frame)
    {
        router.RefreshIdleOutput();
    }
    router.StopOutput();
    CHECK(output.updates == resumed_updates);
    CHECK(output.colors == std::vector<RGBColor>(2, white));
    router.Reset();
    const unsigned int reset_updates = output.updates;
    router.RefreshIdleOutput();
    CHECK(output.updates == reset_updates);
    CHECK(api.virtual_controllers.empty());
    CHECK(api.created == api.deleted);

    RGBController_Setup setup = TestController::LinearSetup();
    setup.type = DEVICE_TYPE_LEDSTRIP;
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

    // Only the child segment plays; both parent lanes are empty. Idle
    // siblings were cleared once; after that, colors another writer puts
    // there (e.g. Visual Map on the same controller) survive every effect
    // frame and idle refresh instead of being blacked out each time.
    router.SetLayerActive(lighttrack::ClipId(3), false);
    router.SetLayerActive(lighttrack::ClipId(4), true);
    overlay[0]->SetAllZoneLEDs(ToRGBColor(0, 255, 0), 100, 0, 0);
    overlay[0]->controller->UpdateLEDs();
    CHECK(segmented.colors[3] == 0 && segmented.colors[4] == 0);
    segmented.colors[0] = white; // Outside all configured segments.
    idle_segment.SetAllZoneLEDs(white, 100, 0, 0);
    second_zone.SetAllZoneLEDs(white, 100, 0, 0);
    for(int frame = 0; frame < 3; ++frame)
    {
        const std::size_t submitted = segmented.frames.size();
        overlay[0]->controller->UpdateLEDs();
        router.RefreshIdleOutput();
        CHECK(segmented.frames.size() == submitted + 1);
        CHECK(segmented.frames.back() == std::vector<RGBColor>({
            white, ToRGBColor(0, 255, 0), ToRGBColor(0, 255, 0),
            white, white, white}));
    }

    // The zone-level effect also leaves cleared idle segments alone.
    router.SetLayerActive(lighttrack::ClipId(4), false);
    // Segment 0 just went idle, so it was cleared once.
    CHECK(segmented.colors[1] == 0 && segmented.colors[2] == 0);
    router.SetLayerActive(lighttrack::ClipId(5), true);
    const auto zone_effect = router.LayerZones(lighttrack::ClipId(5));
    zone_effect[0]->SetAllZoneLEDs(ToRGBColor(0, 0, 255), 100, 0, 0);
    zone_effect[0]->controller->UpdateLEDs();
    segment_zone.SetAllZoneLEDs(white, 100, 0, 0);
    idle_segment.SetAllZoneLEDs(white, 100, 0, 0);
    for(int frame = 0; frame < 3; ++frame)
    {
        const std::size_t submitted = segmented.frames.size();
        zone_effect[0]->controller->UpdateLEDs();
        router.RefreshIdleOutput();
        CHECK(segmented.frames.size() == submitted + 1);
        CHECK(segmented.frames.back() == std::vector<RGBColor>({
            white, white, white, white,
            ToRGBColor(0, 0, 255), ToRGBColor(0, 0, 255)}));
    }

    // Highlight/disable overrides take precedence over idle and effects
    // when they are set...
    router.SetOverrides({{&idle_segment, white}, {&second_zone, ColorUtils::OFF()}});
    CHECK(segmented.frames.back() == std::vector<RGBColor>({
        white, white, white, white, 0, 0}));
    // ...but are applied once: later outside writes (e.g. a Visual Map device
    // painting the same LEDs) are not fought by idle refreshes or by effect
    // frames of other clips on the same controller.
    idle_segment.SetAllZoneLEDs(ToRGBColor(0, 255, 0), 100, 0, 0);
    second_zone.SetAllZoneLEDs(ToRGBColor(255, 0, 0), 100, 0, 0);
    for(int frame = 0; frame < 3; ++frame)
    {
        router.RefreshIdleOutput();
        zone_effect[0]->controller->UpdateLEDs();
        CHECK(segmented.colors[3] == ToRGBColor(0, 255, 0));
        CHECK(segmented.colors[4] == ToRGBColor(255, 0, 0));
    }
    idle_segment.SetAllZoneLEDs(white, 100, 0, 0);
    second_zone.SetAllZoneLEDs(ColorUtils::OFF(), 100, 0, 0);
    router.SetOverrides({});
    router.RefreshIdleOutput();
    // Removing the override clears that segment once; the effect returns.
    CHECK(segmented.frames.back() == std::vector<RGBColor>({
        white, white, white, 0, ToRGBColor(0, 0, 255), ToRGBColor(0, 0, 255)}));
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

    // Back-to-back clips on one zone hand over without a black frame: the
    // new clip is not shown until its effect draws, and the old clip ending
    // in the meantime keeps its last frame on the LEDs.
    {
        RGBController_Setup strip_setup = TestController::LinearSetup();
        strip_setup.type = DEVICE_TYPE_LEDSTRIP;
        TestController strip(strip_setup);
        ControllerZone strip_zone(&strip, 0, false, 100, true, false);
        lighttrack::openrgb::OpenRgbColorRouter handoff_router(&api);
        handoff_router.SetTargets({&strip_zone});
        handoff_router.ConfigureLayers({
            {lighttrack::ClipId(30), 0, 0, {&strip_zone}},
            {lighttrack::ClipId(31), 0, 1, {&strip_zone}}
        });
        handoff_router.StartOutput();
        const auto first_clip = handoff_router.LayerZones(lighttrack::ClipId(30));
        const auto next_clip = handoff_router.LayerZones(lighttrack::ClipId(31));
        handoff_router.SetLayerActive(lighttrack::ClipId(30), true);
        first_clip[0]->SetAllZoneLEDs(ToRGBColor(255, 0, 0), 100, 0, 0);
        first_clip[0]->controller->UpdateLEDs();
        CHECK(strip.colors[0] == ToRGBColor(255, 0, 0));

        // Next clip starts (not drawn yet), then the first one ends.
        handoff_router.SetLayerActive(lighttrack::ClipId(31), true);
        CHECK(strip.colors[0] == ToRGBColor(255, 0, 0));
        handoff_router.SetLayerActive(lighttrack::ClipId(30), false);
        CHECK(strip.colors[0] == ToRGBColor(255, 0, 0));
        handoff_router.RefreshIdleOutput();
        CHECK(strip.colors[0] == ToRGBColor(255, 0, 0));

        next_clip[0]->SetAllZoneLEDs(ToRGBColor(0, 0, 255), 100, 0, 0);
        next_clip[0]->controller->UpdateLEDs();
        CHECK(strip.colors[0] == ToRGBColor(0, 0, 255));

        // Let resends from earlier clears (start of playback) finish.
        {
            QElapsedTimer drain;
            drain.start();
            while(drain.elapsed() < 300)
            {
                QApplication::processEvents(QEventLoop::AllEvents, 20);
            }
        }
        // A clip that ends with nothing following still clears once.
        handoff_router.SetLayerActive(lighttrack::ClipId(31), false);
        CHECK(strip.colors[0] == ColorUtils::OFF());
        // That one-off clear is resent shortly after, in case the device
        // dropped the request while busy (OpenRGB update-flag race).
        const unsigned int cleared_updates = strip.updates;
        QElapsedTimer settle_timer;
        settle_timer.start();
        while(settle_timer.elapsed() < 300)
        {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        CHECK(strip.updates == cleared_updates + 2);
        CHECK(strip.colors[0] == ColorUtils::OFF());

        // Reset cancels resends that are still pending.
        handoff_router.SetLayerActive(lighttrack::ClipId(31), true);
        next_clip[0]->controller->UpdateLEDs();
        handoff_router.SetLayerActive(lighttrack::ClipId(31), false);
        const unsigned int reset_updates = strip.updates;
        handoff_router.Reset();
        settle_timer.restart();
        while(settle_timer.elapsed() < 300)
        {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        CHECK(strip.updates == reset_updates);
    }

    // Virtual devices (e.g. overlapping Visual Map devices): one that has a
    // clip elsewhere in the timeline must not get idle black while another
    // virtual device plays, or the two fight over the shared real LEDs.
    {
        TestController wall(TestController::LinearSetup());
        TestController pc(TestController::LinearSetup());
        CHECK(wall.GetDeviceType() == DEVICE_TYPE_VIRTUAL);
        ControllerZone wall_zone(&wall, 0, false, 100, true, false);
        ControllerZone pc_zone(&pc, 0, false, 100, true, false);
        lighttrack::openrgb::OpenRgbColorRouter virtual_router(&api);
        virtual_router.SetTargets({&wall_zone, &pc_zone});
        virtual_router.ConfigureLayers({
            {lighttrack::ClipId(20), 0, 0, {&wall_zone}},
            {lighttrack::ClipId(21), 0, 1, {&pc_zone}}
        });
        virtual_router.StartOutput();
        virtual_router.SetLayerActive(lighttrack::ClipId(20), true);
        const auto wall_layer = virtual_router.LayerZones(lighttrack::ClipId(20));
        wall_layer[0]->SetAllZoneLEDs(ToRGBColor(255, 0, 0), 100, 0, 0);
        wall_layer[0]->controller->UpdateLEDs();
        CHECK(wall.colors[0] == ToRGBColor(255, 0, 0));
        const unsigned int pc_updates = pc.updates;
        pc.SetAllColors(ToRGBColor(255, 0, 0)); // what Visual Map shows
        for(int frame = 0; frame < 3; ++frame)
        {
            virtual_router.RefreshIdleOutput();
        }
        CHECK(pc.updates == pc_updates);
        CHECK(pc.colors[0] == ToRGBColor(255, 0, 0));
        // Its own clip ending still clears it once.
        virtual_router.SetLayerActive(lighttrack::ClipId(21), true);
        virtual_router.SetLayerActive(lighttrack::ClipId(21), false);
        CHECK(pc.colors[0] == ColorUtils::OFF());
        virtual_router.Reset();
    }

    // Clip direction: rotate / mirror a 3x2 matrix map.
    //   0 1 2
    //   3 4 5
    using lighttrack::openrgb::LayerOrientation;
    using lighttrack::openrgb::TransformMatrixMap;
    const auto transformed = [](int rotation, bool mirror)
    {
        matrix_map_type matrix;
        matrix.width = 3;
        matrix.height = 2;
        matrix.map = {0, 1, 2, 3, 4, 5};
        TransformMatrixMap(matrix, LayerOrientation{rotation, mirror});
        return matrix;
    };
    const matrix_map_type rotated_90 = transformed(90, false);
    CHECK(rotated_90.width == 2 && rotated_90.height == 3);
    CHECK(rotated_90.map == std::vector<unsigned int>({3, 0, 4, 1, 5, 2}));
    CHECK(transformed(180, false).map == std::vector<unsigned int>({5, 4, 3, 2, 1, 0}));
    const matrix_map_type rotated_270 = transformed(270, false);
    CHECK(rotated_270.width == 2 && rotated_270.height == 3);
    CHECK(rotated_270.map == std::vector<unsigned int>({2, 5, 1, 4, 0, 3}));
    CHECK(transformed(0, true).map == std::vector<unsigned int>({2, 1, 0, 5, 4, 3}));
    CHECK(transformed(0, false).map == std::vector<unsigned int>({0, 1, 2, 3, 4, 5}));

    // A clip layer gets the oriented map on matrix zones and a reversed
    // strip on linear zones; other clips on the same device are unaffected.
    RGBController_Setup oriented_setup = TestController::LinearSetup();
    oriented_setup.zones[0].type = ZONE_TYPE_MATRIX;
    oriented_setup.zones[0].leds_count = 6;
    oriented_setup.zones[0].leds_min = 6;
    oriented_setup.zones[0].leds_max = 6;
    oriented_setup.zones[0].matrix_map.width = 3;
    oriented_setup.zones[0].matrix_map.height = 2;
    oriented_setup.zones[0].matrix_map.map = {0, 1, 2, 3, 4, 5};
    oriented_setup.zones.push_back(TestController::LinearSetup().zones[0]);
    TestController oriented(oriented_setup);
    ControllerZone matrix_zone(&oriented, 0, false, 100, true, false);
    ControllerZone strip_zone(&oriented, 1, false, 100, true, false);
    lighttrack::openrgb::OpenRgbColorRouter oriented_router(&api);
    oriented_router.SetTargets({&matrix_zone, &strip_zone});
    oriented_router.ConfigureLayers({
        {lighttrack::ClipId(10), 0, 0, {&matrix_zone, &strip_zone},
            LayerOrientation{90, false}},
        {lighttrack::ClipId(11), 0, 1, {&matrix_zone, &strip_zone}}
    });
    const auto turned = oriented_router.LayerZones(lighttrack::ClipId(10));
    const auto plain = oriented_router.LayerZones(lighttrack::ClipId(11));
    CHECK(turned.size() == 2 && plain.size() == 2);
    CHECK(turned[0]->matrix_map_width() == 2 && turned[0]->matrix_map_height() == 3);
    CHECK(turned[0]->controller->GetZoneMatrixMap(0).map
        == std::vector<unsigned int>({3, 0, 4, 1, 5, 2}));
    CHECK(!turned[1]->reverse); // 90 degrees leaves strips alone
    CHECK(plain[0]->matrix_map_width() == 3);
    CHECK(!plain[1]->reverse);
    oriented_router.ConfigureLayers({
        {lighttrack::ClipId(12), 0, 0, {&strip_zone}, LayerOrientation{0, true}},
        {lighttrack::ClipId(13), 0, 1, {&strip_zone},
            LayerOrientation{180, true}}
    });
    CHECK(oriented_router.LayerZones(lighttrack::ClipId(12))[0]->reverse);
    CHECK(!oriented_router.LayerZones(lighttrack::ClipId(13))[0]->reverse);
    oriented_router.Reset();
    return 0;
}
