// Renders every LightTrack effect on a 2D test zone and a strip at several
// points in a clip. Fails if an effect never lights anything.
//
// Pass an output folder to also write effects_preview.png: one row per
// effect, one column per sample time (2D zone on top, strip below).

#include "OpenRgbTestHost.h"

#include "EffectListManager.h"
#include "integrations/openrgb/effects/ClipTimedEffect.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>

#include <chrono>
#include <iostream>
#include <thread>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr unsigned int MATRIX_WIDTH = 24;
constexpr unsigned int MATRIX_HEIGHT = 12;
constexpr unsigned int STRIP_LEDS = 30;
constexpr qint64 CLIP_MS = 4000;
const std::vector<qint64> SAMPLE_MS = {0, 400, 1000, 1600, 2400, 3200, 3900};

RGBController_Setup MatrixSetup()
{
    RGBController_Setup setup = TestController::LinearSetup();
    zone& matrix = setup.zones[0];
    matrix.type = ZONE_TYPE_MATRIX;
    matrix.leds_count = MATRIX_WIDTH * MATRIX_HEIGHT;
    matrix.leds_min = matrix.leds_count;
    matrix.leds_max = matrix.leds_count;
    matrix.matrix_map.width = MATRIX_WIDTH;
    matrix.matrix_map.height = MATRIX_HEIGHT;
    matrix.matrix_map.map.resize(matrix.leds_count);
    for(unsigned int index = 0; index < matrix.leds_count; ++index)
    {
        matrix.matrix_map.map[index] = index;
    }
    return setup;
}

RGBController_Setup StripSetup()
{
    RGBController_Setup setup = TestController::LinearSetup();
    setup.zones[0].leds_count = STRIP_LEDS;
    setup.zones[0].leds_min = STRIP_LEDS;
    setup.zones[0].leds_max = STRIP_LEDS;
    return setup;
}

QColor ToQColor(RGBColor color)
{
    return QColor(
        RGBGetRValue(color),
        RGBGetGValue(color),
        RGBGetBValue(color));
}

bool AnyLit(const std::vector<RGBColor>& colors)
{
    for(RGBColor color : colors)
    {
        if(color != 0)
        {
            return true;
        }
    }
    return false;
}
}

namespace
{
// Exposes the clip clock for testing.
struct ClockProbe final : lighttrack::openrgb::ClipTimedEffect
{
    using ClipTimedEffect::ElapsedSeconds;
};

bool ClipClockFollowsRate()
{
    ClockProbe probe;
    // Half speed: 400 ms of real time is 200 ms of song time.
    probe.SetClipTiming(1000, 4000, false, 0.5);
    probe.EffectState(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const double half = probe.ElapsedSeconds();
    if(half < 1.15 || half > 1.30)
    {
        std::cerr << "0.5x clip clock: expected ~1.2 s, got " << half << '\n';
        return false;
    }
    // Paused: the clock holds the playhead time.
    probe.SyncClipTiming(2500, 4000, true, 0.5);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if(probe.ElapsedSeconds() != 2.5)
    {
        std::cerr << "Paused clip clock moved\n";
        return false;
    }
    // Back to normal speed from the playhead.
    probe.SyncClipTiming(2500, 4000, false, 1.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const double normal = probe.ElapsedSeconds();
    if(normal < 2.65 || normal > 2.80)
    {
        std::cerr << "1x clip clock: expected ~2.7 s, got " << normal << '\n';
        return false;
    }
    return true;
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    if(!ClipClockFollowsRate())
    {
        return 1;
    }
    const QString output_dir =
        argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    std::vector<effect_names> effects;
    for(const auto& category :
        EffectListManager::get()->GetCategorizedEffects())
    {
        if(category.first == "LightTrack")
        {
            effects = category.second;
        }
    }
    if(effects.empty())
    {
        std::cerr << "No LightTrack effects registered\n";
        return 1;
    }

    // Flying Windows keeps its own pane colors and spacing in the clip's
    // custom settings (saved in the .lighttrack file).
    {
        std::unique_ptr<RGBEffect> saved(
            EffectListManager::get()->GetEffectConstructor(
                "LightTrackFlyingWindows")());
        if(!saved || !saved->EffectDetails.HasCustomSettings)
        {
            std::cerr << "Flying Windows has no custom settings\n";
            return 1;
        }
        const json defaults = saved->SaveCustomSettings();
        if(defaults["colors"].size() != 4 || defaults["spacing"] != 40)
        {
            std::cerr << "Unexpected Flying Windows defaults: "
                      << defaults.dump() << '\n';
            return 1;
        }
        json custom;
        custom["colors"] = {0x0000FFu, 0x00FF00u, 0xFF0000u, 0xFFFFFFu, 0x00FFFFu};
        custom["spacing"] = 85;
        saved->LoadCustomSettings(custom);
        std::unique_ptr<RGBEffect> loaded(
            EffectListManager::get()->GetEffectConstructor(
                "LightTrackFlyingWindows")());
        loaded->LoadCustomSettings(saved->SaveCustomSettings());
        if(loaded->SaveCustomSettings() != custom)
        {
            std::cerr << "Flying Windows settings did not round-trip: "
                      << loaded->SaveCustomSettings().dump() << '\n';
            return 1;
        }
    }

    constexpr int cell = 6;
    constexpr int label_width = 150;
    constexpr int frame_width = MATRIX_WIDTH * cell;
    constexpr int frame_height = MATRIX_HEIGHT * cell + 4 + cell * 2;
    constexpr int gap = 6;
    QImage sheet(
        label_width + static_cast<int>(SAMPLE_MS.size()) * (frame_width + gap),
        static_cast<int>(effects.size()) * (frame_height + gap) + 20,
        QImage::Format_RGB32);
    sheet.fill(QColor(40, 40, 40));
    QPainter painter(&sheet);
    painter.setPen(Qt::white);
    for(std::size_t sample = 0; sample < SAMPLE_MS.size(); ++sample)
    {
        painter.drawText(
            label_width + static_cast<int>(sample) * (frame_width + gap),
            14,
            QString("%1 ms").arg(SAMPLE_MS[sample]));
    }

    int failures = 0;
    for(std::size_t row = 0; row < effects.size(); ++row)
    {
        const effect_names& names = effects[row];
        std::unique_ptr<RGBEffect> effect(
            EffectListManager::get()->GetEffectConstructor(names.classname)());
        auto* timed =
            dynamic_cast<lighttrack::openrgb::ClipTimedEffect*>(effect.get());
        if(timed == nullptr)
        {
            std::cerr << names.classname << " is not clip-timed\n";
            ++failures;
            continue;
        }

        std::vector<RGBColor> colors = {
            ToRGBColor(255, 0, 0),
            ToRGBColor(0, 80, 255),
            ToRGBColor(0, 255, 0),
            ToRGBColor(255, 255, 255)};
        colors.resize(effect->EffectDetails.UserColors);
        // Two-color effects use the second color as a background or second
        // beam color; keep it visible but distinct.
        effect->SetUserColors(colors);
        effect->SetBrightness(100);

        TestController matrix(MatrixSetup());
        TestController strip(StripSetup());
        ControllerZone matrix_zone(&matrix, 0, false, 100, true, false);
        ControllerZone strip_zone(&strip, 0, false, 100, true, false);

        const int top = 20 + static_cast<int>(row) * (frame_height + gap);
        painter.setPen(Qt::white);
        painter.drawText(
            QRect(4, top, label_width - 8, frame_height),
            Qt::AlignVCenter | Qt::AlignLeft,
            QString::fromStdString(names.ui_name));

        bool lit = false;
        for(std::size_t sample = 0; sample < SAMPLE_MS.size(); ++sample)
        {
            for(ControllerZone* zone : {&matrix_zone, &strip_zone})
            {
                // Frozen at the sample time, as when paused on the playhead.
                timed->SetClipTiming(SAMPLE_MS[sample], CLIP_MS, true);
                effect->EffectState(true);
                effect->StepEffect({zone});
            }
            lit = lit || AnyLit(matrix.colors) || AnyLit(strip.colors);

            const int left = label_width
                + static_cast<int>(sample) * (frame_width + gap);
            for(unsigned int y = 0; y < MATRIX_HEIGHT; ++y)
            {
                for(unsigned int x = 0; x < MATRIX_WIDTH; ++x)
                {
                    painter.fillRect(
                        left + static_cast<int>(x) * cell,
                        top + static_cast<int>(y) * cell,
                        cell - 1,
                        cell - 1,
                        ToQColor(matrix.colors[y * MATRIX_WIDTH + x]));
                }
            }
            const int strip_top = top + MATRIX_HEIGHT * cell + 4;
            for(unsigned int i = 0; i < STRIP_LEDS; ++i)
            {
                const int x0 = left + static_cast<int>(i * frame_width / STRIP_LEDS);
                const int x1 = left + static_cast<int>((i + 1) * frame_width / STRIP_LEDS);
                painter.fillRect(
                    x0,
                    strip_top,
                    std::max(1, x1 - x0 - 1),
                    cell * 2,
                    ToQColor(strip.colors[i]));
            }
        }
        effect->EffectState(false);

        if(!lit)
        {
            std::cerr << names.classname << " never lit any LED\n";
            ++failures;
        }
    }
    painter.end();

    if(!output_dir.isEmpty())
    {
        const QString path = QDir(output_dir).filePath("effects_preview.png");
        if(!sheet.save(path))
        {
            std::cerr << "Could not write " << path.toStdString() << '\n';
            return 1;
        }
        std::cout << "Wrote " << path.toStdString() << '\n';
    }

    std::cout << effects.size() << " LightTrack effects rendered, "
              << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
