// Simple, clip-aware effects for the LightTrack timeline.
//
// Each effect registers itself with the Effects plugin's EffectListManager,
// so it shows up in the timeline's effect list under the "LightTrack"
// category. Settings use only the standard effect page controls (colors,
// Speed and one named slider), so no custom UI forms are needed.

#include "integrations/openrgb/effects/ClipTimedEffect.h"

#include "ColorUtils.h"
#include "EffectRegisterer.h"
#include "hsv.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <vector>

namespace lighttrack::openrgb::effects
{
namespace
{
constexpr const char* CATEGORY = "LightTrack";

int Channel(double value)
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, 255);
}

RGBColor Scale(RGBColor color, double factor)
{
    factor = std::clamp(factor, 0.0, 1.0);
    const int r = Channel(RGBGetRValue(color) * factor);
    const int g = Channel(RGBGetGValue(color) * factor);
    const int b = Channel(RGBGetBValue(color) * factor);
    return ToRGBColor(r, g, b);
}

double Fraction(double value)
{
    return value - std::floor(value);
}
}

// Shared helpers for the effects below.
class SimpleEffect : public ClipTimedEffect
{
public:
    SimpleEffect(
        const std::string& class_name,
        const std::string& ui_name,
        const std::string& description,
        unsigned int user_colors)
    {
        EffectDetails.EffectClassName = class_name;
        EffectDetails.EffectName = ui_name;
        EffectDetails.EffectDescription = description;
        EffectDetails.UserColors = user_colors;
        EffectDetails.SupportsRandom = false;
        EffectDetails.HasCustomSettings = false;
    }

protected:
    void UseSlider2(
        const std::string& name,
        int min_value,
        int max_value,
        int default_value)
    {
        EffectDetails.Slider2Name = name;
        EffectDetails.MinSlider2Val = min_value;
        EffectDetails.MaxSlider2Val = max_value;
        SetSlider2Val(static_cast<unsigned int>(default_value));
    }

    void UseSpeed(int min_value, int max_value, int default_value)
    {
        EffectDetails.MinSpeed = min_value;
        EffectDetails.MaxSpeed = max_value;
        SetSpeed(static_cast<unsigned int>(default_value));
    }

    RGBColor Color(std::size_t index) const
    {
        return index < UserColors.size()
            ? UserColors[index]
            : ToRGBColor(255, 255, 255);
    }

    void Fill(
        const std::vector<ControllerZone*>& zones,
        RGBColor color)
    {
        for(ControllerZone* zone : zones)
        {
            zone->SetAllZoneLEDs(
                color,
                static_cast<int>(Brightness),
                Temperature,
                Tint);
        }
    }

    // Where each LED sits along the zone, 0 (start) .. 1 (end).
    // Linear zones use LED order (honoring the zone's reverse flag);
    // matrix zones use the LED's column in the OpenRGB matrix map, so
    // sweeps run left to right across the shape instead of along the wiring.
    struct LedLayout
    {
        std::vector<double> position;
        int resolution = 1; // distinct steps along the sweep
    };

    static LedLayout Layout(ControllerZone* zone)
    {
        LedLayout layout;
        const int count = static_cast<int>(zone->leds_count());
        layout.position.assign(static_cast<std::size_t>(count), 0.0);

        const zone_type type = zone->type();
        const bool is_matrix = type == ZONE_TYPE_MATRIX
            || type == ZONE_TYPE_MATRIX_LOOP_X
            || type == ZONE_TYPE_MATRIX_LOOP_Y;
        const unsigned int width = zone->matrix_map_width();
        const unsigned int height = zone->matrix_map_height();
        const unsigned int* map = zone->map();

        if(is_matrix && map != nullptr && width > 0 && height > 0)
        {
            layout.resolution = static_cast<int>(width);
            for(unsigned int y = 0; y < height; ++y)
            {
                for(unsigned int x = 0; x < width; ++x)
                {
                    const unsigned int led = map[y * width + x];
                    if(led < static_cast<unsigned int>(count))
                    {
                        layout.position[led] =
                            static_cast<double>(x) / width;
                    }
                }
            }
            return layout;
        }

        layout.resolution = std::max(1, count);
        for(int i = 0; i < count; ++i)
        {
            const int step = zone->reverse ? count - 1 - i : i;
            layout.position[static_cast<std::size_t>(i)] =
                static_cast<double>(step) / layout.resolution;
        }
        return layout;
    }

    // LED centers in grid units (x right, y down). Matrix zones use their
    // matrix map. Strips are laid out as a single row inside a virtual
    // canvas count/3 tall, at strip_y (0 = top, 1 = bottom), so 2D shapes
    // like beams show up on a strip where they cross that row.
    struct Geometry
    {
        double width = 1.0;
        double height = 1.0;
        std::vector<double> x;
        std::vector<double> y;
    };

    static Geometry Points(ControllerZone* zone, double strip_y)
    {
        Geometry geometry;
        const int count = static_cast<int>(zone->leds_count());
        geometry.x.assign(static_cast<std::size_t>(count), -1000.0);
        geometry.y.assign(static_cast<std::size_t>(count), -1000.0);

        const zone_type type = zone->type();
        const bool is_matrix = type == ZONE_TYPE_MATRIX
            || type == ZONE_TYPE_MATRIX_LOOP_X
            || type == ZONE_TYPE_MATRIX_LOOP_Y;
        const unsigned int width = zone->matrix_map_width();
        const unsigned int height = zone->matrix_map_height();
        const unsigned int* map = zone->map();

        if(is_matrix && map != nullptr && width > 0 && height > 0)
        {
            geometry.width = width;
            geometry.height = height;
            for(unsigned int y = 0; y < height; ++y)
            {
                for(unsigned int x = 0; x < width; ++x)
                {
                    const unsigned int led = map[y * width + x];
                    if(led < static_cast<unsigned int>(count))
                    {
                        geometry.x[led] = x + 0.5;
                        geometry.y[led] = y + 0.5;
                    }
                }
            }
            return geometry;
        }

        geometry.width = std::max(1, count);
        geometry.height = std::max(1.0, count / 3.0);
        for(int i = 0; i < count; ++i)
        {
            const int step = zone->reverse ? count - 1 - i : i;
            geometry.x[static_cast<std::size_t>(i)] = step + 0.5;
            geometry.y[static_cast<std::size_t>(i)] =
                strip_y * geometry.height;
        }
        return geometry;
    }

    // Additive light accumulator (beams overlapping get brighter).
    struct Light
    {
        double r = 0.0;
        double g = 0.0;
        double b = 0.0;

        void Add(RGBColor color, double amount)
        {
            if(amount <= 0.0)
            {
                return;
            }
            r += RGBGetRValue(color) * amount;
            g += RGBGetGValue(color) * amount;
            b += RGBGetBValue(color) * amount;
        }

        RGBColor ToColor() const
        {
            return ToRGBColor(Channel(r), Channel(g), Channel(b));
        }
    };

    // Brightness of a beam from (ex, ey) pointing at angle (radians,
    // 0 = straight up, positive = right) at point (px, py). width is the
    // beam's half-width at its origin; spread widens it with distance.
    static double Beam(
        double px,
        double py,
        double ex,
        double ey,
        double angle,
        double width,
        double spread)
    {
        const double dx = std::sin(angle);
        const double dy = -std::cos(angle);
        const double vx = px - ex;
        const double vy = py - ey;
        const double along = vx * dx + vy * dy;
        if(along < -0.5)
        {
            return 0.0;
        }
        const double across = std::abs(vx * dy - vy * dx);
        const double half_width = width + std::max(0.0, along) * spread;
        return std::max(0.0, 1.0 - across / half_width);
    }

    void SetLed(ControllerZone* zone, int index, RGBColor color)
    {
        zone->SetLED(
            index,
            color,
            static_cast<int>(Brightness),
            Temperature,
            Tint);
    }
};

#define LIGHTTRACK_EFFECT(Class, UiName)                              \
    static std::string const ClassName() { return "LightTrack" #Class; } \
    static std::string const UI_Name() { return UiName; }              \
    EFFECT_REGISTERER(                                                 \
        ClassName(),                                                   \
        UI_Name(),                                                     \
        CATEGORY,                                                      \
        []() { return new Class; })

/*-------------------------------------------------------------*\
| Solid Color: one color for the whole clip.                    |
\*-------------------------------------------------------------*/
class SolidColor final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(SolidColor, "Solid Color");

    SolidColor() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "A single steady color",
            1)
    {
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        Fill(zones, Color(0));
    }
};
REGISTER_EFFECT(SolidColor);

/*-------------------------------------------------------------*\
| Fade In / Fade Out: black <-> color across the clip.          |
| Squared progress looks even to the eye.                       |
\*-------------------------------------------------------------*/
class FadeIn final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(FadeIn, "Fade In");

    FadeIn() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "Fades from black to the color over the clip",
            1)
    {
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        const double p = ClipProgress();
        Fill(zones, Scale(Color(0), p * p));
    }
};
REGISTER_EFFECT(FadeIn);

class FadeOut final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(FadeOut, "Fade Out");

    FadeOut() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "Fades from the color to black over the clip",
            1)
    {
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        const double p = 1.0 - ClipProgress();
        Fill(zones, Scale(Color(0), p * p));
    }
};
REGISTER_EFFECT(FadeOut);

/*-------------------------------------------------------------*\
| Color Fade: color 1 -> color 2 across the clip.               |
\*-------------------------------------------------------------*/
class ColorFade final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(ColorFade, "Color Fade");

    ColorFade() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "Blends from the first color to the second over the clip",
            2)
    {
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        Fill(
            zones,
            ColorUtils::Interpolate(
                Color(0),
                Color(1),
                static_cast<float>(ClipProgress())));
    }
};
REGISTER_EFFECT(ColorFade);

/*-------------------------------------------------------------*\
| Flash: full color at the clip start, then decays to black.    |
\*-------------------------------------------------------------*/
class Flash final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(Flash, "Flash");

    Flash() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "A single hit at the start of the clip that fades out",
            1)
    {
        UseSlider2("Decay (ms)", 50, 3000, 500);
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        const double decay_s =
            std::max(1u, GetSlider2Val()) / 1000.0;
        const double k =
            std::max(0.0, 1.0 - ElapsedSeconds() / decay_s);
        Fill(zones, Scale(Color(0), k * k));
    }
};
REGISTER_EFFECT(Flash);

/*-------------------------------------------------------------*\
| Strobe: hard on/off flashes.                                  |
\*-------------------------------------------------------------*/
class Strobe final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(Strobe, "Strobe");

    Strobe() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "Hard on/off flashing",
            1)
    {
        UseSlider2("Flashes / sec", 1, 30, 10);
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        constexpr double on_fraction = 0.35;
        const double rate = std::max(1u, GetSlider2Val());
        const bool on =
            Fraction(ElapsedSeconds() * rate) < on_fraction;
        Fill(zones, on ? Color(0) : ToRGBColor(0, 0, 0));
    }
};
REGISTER_EFFECT(Strobe);

/*-------------------------------------------------------------*\
| Beat Pulse: a hit on every beat that decays until the next.   |
| Beat 1 lands on the clip start.                               |
\*-------------------------------------------------------------*/
class BeatPulse final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(BeatPulse, "Beat Pulse");

    BeatPulse() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "Pulses on every beat, starting at the clip start",
            1)
    {
        UseSlider2("BPM", 30, 240, 120);
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        const double beats_per_second =
            std::max(1u, GetSlider2Val()) / 60.0;
        const double phase =
            Fraction(ElapsedSeconds() * beats_per_second);
        const double k = 1.0 - phase;
        Fill(zones, Scale(Color(0), k * k));
    }
};
REGISTER_EFFECT(BeatPulse);

/*-------------------------------------------------------------*\
| Beat Alternate: switches between two colors on every beat.    |
\*-------------------------------------------------------------*/
class BeatAlternate final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(BeatAlternate, "Beat Alternate");

    BeatAlternate() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "Swaps between two colors on every beat",
            2)
    {
        UseSlider2("BPM", 30, 240, 120);
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        const double beats_per_second =
            std::max(1u, GetSlider2Val()) / 60.0;
        const auto beat = static_cast<long long>(
            std::floor(ElapsedSeconds() * beats_per_second));
        Fill(zones, Color(beat % 2 == 0 ? 0 : 1));
    }
};
REGISTER_EFFECT(BeatAlternate);

/*-------------------------------------------------------------*\
| Wipe: color 1 fills the LEDs over color 2 across the clip.    |
\*-------------------------------------------------------------*/
class WipeBase : public SimpleEffect
{
public:
    WipeBase(
        const std::string& class_name,
        const std::string& ui_name,
        const std::string& description,
        bool reverse) :
        SimpleEffect(class_name, ui_name, description, 2),
        reverse(reverse)
    {
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        const double p = ClipProgress();
        for(ControllerZone* zone : zones)
        {
            const LedLayout layout = Layout(zone);
            const int count = static_cast<int>(layout.position.size());
            for(int i = 0; i < count; ++i)
            {
                double position = layout.position[static_cast<std::size_t>(i)];
                if(reverse)
                {
                    position = 1.0 - 1.0 / layout.resolution - position;
                }
                // One step of soft edge so the front moves smoothly.
                const double amount = std::clamp(
                    (p - position) * layout.resolution,
                    0.0,
                    1.0);
                SetLed(
                    zone,
                    i,
                    ColorUtils::Interpolate(
                        Color(1),
                        Color(0),
                        static_cast<float>(amount)));
            }
        }
    }

private:
    bool reverse;
};

class Wipe final : public WipeBase
{
public:
    LIGHTTRACK_EFFECT(Wipe, "Wipe");

    Wipe() :
        WipeBase(
            ClassName(),
            UI_Name(),
            "The first color fills the LEDs from start to end over the clip",
            false)
    {
    }
};
REGISTER_EFFECT(Wipe);

class WipeReverse final : public WipeBase
{
public:
    LIGHTTRACK_EFFECT(WipeReverse, "Wipe (Reverse)");

    WipeReverse() :
        WipeBase(
            ClassName(),
            UI_Name(),
            "The first color fills the LEDs from end to start over the clip",
            true)
    {
    }
};
REGISTER_EFFECT(WipeReverse);

/*-------------------------------------------------------------*\
| Runner: a short block of LEDs travels from the start of the   |
| zone (A) to the end (B) over the clip. On 2D zones it runs    |
| across the columns; use the clip Direction to turn it.        |
\*-------------------------------------------------------------*/
class Runner final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(Runner, "Runner");

    Runner() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "A few LEDs travel from start to end over the clip",
            2)
    {
        UseSlider2("Length (LEDs)", 1, 60, 5);
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        const double p = ClipProgress();
        for(ControllerZone* zone : zones)
        {
            const LedLayout layout = Layout(zone);
            const double steps = layout.resolution;
            const double length = std::min<double>(
                std::max(1u, GetSlider2Val()),
                steps);
            // Block covers [start, start + length) in LED steps; it starts
            // flush with A and ends flush with B.
            const double start = p * (steps - length);
            const double end = start + length;

            const int count = static_cast<int>(layout.position.size());
            for(int i = 0; i < count; ++i)
            {
                const double cell =
                    layout.position[static_cast<std::size_t>(i)] * steps;
                // How much of this LED's cell [cell, cell + 1) is covered,
                // so the block glides smoothly between LEDs.
                const double covered = std::clamp(
                    std::min(cell + 1.0, end) - std::max(cell, start),
                    0.0,
                    1.0);
                SetLed(
                    zone,
                    i,
                    ColorUtils::Interpolate(
                        Color(1),
                        Color(0),
                        static_cast<float>(covered)));
            }
        }
    }
};
REGISTER_EFFECT(Runner);

/*-------------------------------------------------------------*\
| Ripple: rings start at the center and spread to the edges,    |
| fading as they grow, like a drop in water. The clip emits     |
| "Ripples" rings evenly; the last one leaves as the clip ends. |
| Strips ripple outward from their middle. Ripple (Inward)      |
| runs the same rings from the edges to the center.             |
\*-------------------------------------------------------------*/
class RippleBase : public SimpleEffect
{
public:
    RippleBase(
        const std::string& class_name,
        const std::string& ui_name,
        const std::string& description,
        bool inward) :
        SimpleEffect(class_name, ui_name, description, 2),
        inward(inward)
    {
        UseSlider2("Ripples", 1, 20, 1);
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        const int ripples = static_cast<int>(std::max(1u, GetSlider2Val()));
        // Progress through the current ring's lifetime.
        const double phase = Fraction(ClipProgress() * ripples);
        const double clip_progress = ClipProgress();
        const bool finished = ClipSeconds() > 0.0 && clip_progress >= 1.0;

        for(ControllerZone* zone : zones)
        {
            const int count = static_cast<int>(zone->leds_count());
            std::vector<double> distance(static_cast<std::size_t>(count), 0.0);
            const double max_radius = Distances(zone, distance);

            constexpr double ring_width = 1.5; // in LEDs
            // Outward rings grow from the center and lose strength as they
            // spread; inward rings start beyond the edge and gather
            // strength as they close in on the center.
            const double travel = inward ? 1.0 - phase : phase;
            const double radius = travel * (max_radius + ring_width);
            const double strength = finished
                ? 0.0
                : (inward ? 0.4 + 0.6 * phase : 1.0 - 0.6 * phase);

            for(int i = 0; i < count; ++i)
            {
                const double offset = std::abs(
                    distance[static_cast<std::size_t>(i)] - radius);
                const double k =
                    std::max(0.0, 1.0 - offset / ring_width) * strength;
                SetLed(
                    zone,
                    i,
                    ColorUtils::Interpolate(
                        Color(1),
                        Color(0),
                        static_cast<float>(k)));
            }
        }
    }

private:
    // Fills each LED's distance from the zone center (in LED spacings) and
    // returns the farthest distance.
    static double Distances(ControllerZone* zone, std::vector<double>& distance)
    {
        const int count = static_cast<int>(distance.size());
        const zone_type type = zone->type();
        const bool is_matrix = type == ZONE_TYPE_MATRIX
            || type == ZONE_TYPE_MATRIX_LOOP_X
            || type == ZONE_TYPE_MATRIX_LOOP_Y;
        const unsigned int width = zone->matrix_map_width();
        const unsigned int height = zone->matrix_map_height();
        const unsigned int* map = zone->map();

        if(is_matrix && map != nullptr && width > 0 && height > 0)
        {
            const double center_x = width / 2.0;
            const double center_y = height / 2.0;
            for(unsigned int y = 0; y < height; ++y)
            {
                for(unsigned int x = 0; x < width; ++x)
                {
                    const unsigned int led = map[y * width + x];
                    if(led < static_cast<unsigned int>(count))
                    {
                        distance[led] = std::hypot(
                            x + 0.5 - center_x,
                            y + 0.5 - center_y);
                    }
                }
            }
            return std::hypot(center_x, center_y);
        }

        const double center = count / 2.0;
        for(int i = 0; i < count; ++i)
        {
            distance[static_cast<std::size_t>(i)] =
                std::abs(i + 0.5 - center);
        }
        return center;
    }

    bool inward;
};

class Ripple final : public RippleBase
{
public:
    LIGHTTRACK_EFFECT(Ripple, "Ripple");

    Ripple() :
        RippleBase(
            ClassName(),
            UI_Name(),
            "Rings spread from the center to the edges, like a water ripple",
            false)
    {
    }
};
REGISTER_EFFECT(Ripple);

class RippleInward final : public RippleBase
{
public:
    LIGHTTRACK_EFFECT(RippleInward, "Ripple (Inward)");

    RippleInward() :
        RippleBase(
            ClassName(),
            UI_Name(),
            "Rings start at the edges and close in on the center",
            true)
    {
    }
};
REGISTER_EFFECT(RippleInward);

/*=============================================================*\
| Laser-show looks (beams, cuts, splits, fireworks).            |
| Beams start from the bottom edge and point up; use the clip   |
| Direction to aim them elsewhere. On strips, beams show where  |
| they cross the strip.                                         |
\*=============================================================*/
constexpr double kPi = 3.14159265358979323846;

double DegreesToRadians(double degrees)
{
    return degrees * kPi / 180.0;
}

/*-------------------------------------------------------------*\
| Laser Fan: a row of thin beams from the bottom edge that fan  |
| open and closed while the whole fan sways. Beams alternate    |
| between the two colors.                                       |
\*-------------------------------------------------------------*/
class LaserFan final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(LaserFan, "Laser Fan");

    LaserFan() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "Laser beams fan out from the bottom edge and swing",
            2)
    {
        UseSlider2("Beams", 2, 16, 8);
        UseSpeed(1, 100, 30);
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        const int beams = static_cast<int>(std::max(2u, GetSlider2Val()));
        const double t = ElapsedSeconds() * GetSpeed() / 20.0;
        const double opening = 0.35 + 0.65 * (0.5 + 0.5 * std::sin(t * 2.0 * kPi));
        const double sway = DegreesToRadians(25.0) * std::sin(t * kPi);
        const double max_spread = DegreesToRadians(55.0);

        for(ControllerZone* zone : zones)
        {
            const Geometry geometry = Points(zone, 0.0);
            const int count = static_cast<int>(geometry.x.size());
            for(int i = 0; i < count; ++i)
            {
                Light light;
                for(int beam = 0; beam < beams; ++beam)
                {
                    const double k =
                        2.0 * beam / (beams - 1) - 1.0; // -1 .. 1
                    const double emitter_x =
                        (beam + 0.5) / beams * geometry.width;
                    const double angle = sway + opening * max_spread * k;
                    light.Add(
                        Color(beam % 2),
                        Beam(geometry.x[i], geometry.y[i],
                            emitter_x, geometry.height,
                            angle, 0.6, 0.0));
                }
                SetLed(zone, i, light.ToColor());
            }
        }
    }
};
REGISTER_EFFECT(LaserFan);

/*-------------------------------------------------------------*\
| Laser Crossfire: beams from the bottom-left and bottom-right  |
| corners aim across each other and scissor in and out.         |
\*-------------------------------------------------------------*/
class LaserCrossfire final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(LaserCrossfire, "Laser Crossfire");

    LaserCrossfire() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "Beams from both corners cross each other in a scissor motion "
            "(color 1 = left, color 2 = right)",
            2)
    {
        UseSlider2("Beams per side", 1, 8, 4);
        UseSpeed(1, 100, 30);
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        const int beams = static_cast<int>(std::max(1u, GetSlider2Val()));
        const double t = ElapsedSeconds() * GetSpeed() / 20.0;
        const double scissor = 0.5 + 0.5 * std::sin(t * 2.0 * kPi);

        for(ControllerZone* zone : zones)
        {
            const Geometry geometry = Points(zone, 0.0);
            const int count = static_cast<int>(geometry.x.size());
            for(int i = 0; i < count; ++i)
            {
                Light light;
                for(int beam = 0; beam < beams; ++beam)
                {
                    const double k = beams > 1
                        ? static_cast<double>(beam) / (beams - 1)
                        : 0.5;
                    // From near-vertical to steeply across.
                    const double angle = DegreesToRadians(
                        8.0 + (20.0 + 45.0 * scissor) * k + 15.0 * scissor);
                    light.Add(
                        Color(0),
                        Beam(geometry.x[i], geometry.y[i],
                            0.0, geometry.height, angle, 0.6, 0.0));
                    light.Add(
                        Color(1),
                        Beam(geometry.x[i], geometry.y[i],
                            geometry.width, geometry.height,
                            -angle, 0.6, 0.0));
                }
                SetLed(zone, i, light.ToColor());
            }
        }
    }
};
REGISTER_EFFECT(LaserCrossfire);

/*-------------------------------------------------------------*\
| Spotlight Fan: wide, soft beams radiating up from the bottom  |
| center like moving-head spotlights, slowly sweeping.          |
\*-------------------------------------------------------------*/
class SpotlightFan final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(SpotlightFan, "Spotlight Fan");

    SpotlightFan() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "Wide soft spotlight beams radiate up from the bottom center",
            1)
    {
        UseSlider2("Beams", 3, 15, 7);
        UseSpeed(1, 100, 15);
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        const int beams = static_cast<int>(std::max(3u, GetSlider2Val()));
        const double t = ElapsedSeconds() * GetSpeed() / 20.0;
        const double sweep = DegreesToRadians(12.0) * std::sin(t * kPi);
        const double spread = DegreesToRadians(75.0);

        for(ControllerZone* zone : zones)
        {
            const Geometry geometry = Points(zone, 0.0);
            const double reach = std::hypot(geometry.width, geometry.height);
            const int count = static_cast<int>(geometry.x.size());
            for(int i = 0; i < count; ++i)
            {
                Light light;
                const double px = geometry.x[i];
                const double py = geometry.y[i];
                const double distance = std::hypot(
                    px - geometry.width / 2.0,
                    py - geometry.height);
                const double falloff = 1.0 - 0.6 * distance / reach;
                for(int beam = 0; beam < beams; ++beam)
                {
                    const double k = 2.0 * beam / (beams - 1) - 1.0;
                    const double angle = sweep + spread * k;
                    const double amount = Beam(px, py,
                        geometry.width / 2.0, geometry.height,
                        angle, 0.5, 0.09);
                    light.Add(Color(0), amount * amount * falloff);
                }
                SetLed(zone, i, light.ToColor());
            }
        }
    }
};
REGISTER_EFFECT(SpotlightFan);

/*-------------------------------------------------------------*\
| Color Cut: hard color change on every beat through 4 colors,  |
| with a short black gap so each hit punches.                   |
\*-------------------------------------------------------------*/
class ColorCut final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(ColorCut, "Color Cut");

    ColorCut() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "Hard cut to the next color on every beat (4 colors)",
            4)
    {
        UseSlider2("BPM", 30, 300, 128);
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        constexpr double gap = 0.12; // fraction of each beat left black
        const double beats =
            ElapsedSeconds() * std::max(1u, GetSlider2Val()) / 60.0;
        const auto beat = static_cast<long long>(std::floor(beats));
        const bool dark = Fraction(beats) >= 1.0 - gap;
        Fill(
            zones,
            dark ? ToRGBColor(0, 0, 0)
                 : Color(static_cast<std::size_t>(beat % 4)));
    }
};
REGISTER_EFFECT(ColorCut);

/*-------------------------------------------------------------*\
| Split Colors: first half one color, second half the other;    |
| the halves swap on every beat.                                |
\*-------------------------------------------------------------*/
class SplitColors final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(SplitColors, "Split Colors");

    SplitColors() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "Two halves in two colors that swap on every beat",
            2)
    {
        UseSlider2("BPM", 30, 300, 128);
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        const double beats =
            ElapsedSeconds() * std::max(1u, GetSlider2Val()) / 60.0;
        const bool swapped =
            static_cast<long long>(std::floor(beats)) % 2 != 0;
        for(ControllerZone* zone : zones)
        {
            const LedLayout layout = Layout(zone);
            const int count = static_cast<int>(layout.position.size());
            for(int i = 0; i < count; ++i)
            {
                const bool first_half =
                    layout.position[static_cast<std::size_t>(i)] < 0.5;
                SetLed(zone, i, Color(first_half != swapped ? 0 : 1));
            }
        }
    }
};
REGISTER_EFFECT(SplitColors);

/*-------------------------------------------------------------*\
| Firework: a shell rises from the bottom, bursts into a ring   |
| of sparks that fall and fade. "Fireworks" sets how many go    |
| off over the clip.                                            |
\*-------------------------------------------------------------*/
class Firework final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(Firework, "Firework");

    Firework() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "A shell rises and bursts into falling sparks "
            "(sparks alternate between the two colors)",
            2)
    {
        UseSlider2("Fireworks", 1, 10, 1);
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        const int shells = static_cast<int>(std::max(1u, GetSlider2Val()));
        const double progress = ClipProgress(3.0) * shells;
        const int shell = std::min(
            shells - 1,
            static_cast<int>(std::floor(progress)));
        const double phase = std::min(1.0, progress - shell);
        const bool finished = ClipSeconds() > 0.0 && ClipProgress() >= 1.0;

        // Same launch spot for a given shell every time it plays.
        const double spot = Fraction(std::sin((shell + 1) * 12.9898) * 43758.5453);
        constexpr double launch = 0.3; // fraction of the shell's time rising
        constexpr int sparks = 14;
        const RGBColor trail = ColorUtils::Interpolate(
            Color(0), ToRGBColor(255, 255, 255), 0.5f);

        for(ControllerZone* zone : zones)
        {
            const Geometry geometry = Points(zone, 0.3);
            const double burst_x = geometry.width * (0.3 + 0.4 * spot);
            const double burst_y = geometry.height * 0.3;
            const double radius_max =
                std::max(geometry.width, geometry.height) * 0.45;
            const int count = static_cast<int>(geometry.x.size());

            for(int i = 0; i < count; ++i)
            {
                Light light;
                const double px = geometry.x[i];
                const double py = geometry.y[i];
                if(!finished && phase < launch)
                {
                    // Rising shell with a short tail.
                    const double u = phase / launch;
                    const double shell_y =
                        geometry.height + (burst_y - geometry.height) * u;
                    const double dx = std::abs(px - burst_x);
                    const double dy = py - shell_y; // tail hangs below
                    constexpr double half_width = 1.5;
                    constexpr double tail = 4.0;
                    if(dx < half_width && dy > -1.0 && dy < tail)
                    {
                        light.Add(
                            trail,
                            (1.0 - dx / half_width)
                                * (1.0 - std::max(0.0, dy) / tail) * 1.6);
                    }
                }
                else if(!finished)
                {
                    const double u = (phase - launch) / (1.0 - launch);
                    const double radius = radius_max * std::sqrt(u);
                    const double drop = geometry.height * 0.25 * u * u;
                    const double fade = 1.0 - u;
                    // Bright flash at the moment of the burst.
                    if(u < 0.12)
                    {
                        const double d = std::hypot(px - burst_x, py - burst_y);
                        const double flash = 1.0 - u / 0.12;
                        light.Add(
                            ToRGBColor(255, 255, 255),
                            std::max(0.0, 1.0 - d / 3.0) * flash);
                    }
                    for(int spark = 0; spark < sparks; ++spark)
                    {
                        const double a = 2.0 * kPi * spark / sparks;
                        const double sx = burst_x + std::cos(a) * radius;
                        const double sy = burst_y + std::sin(a) * radius + drop;
                        const double d = std::hypot(px - sx, py - sy);
                        light.Add(
                            Color(spark % 2),
                            std::max(0.0, 1.0 - d / 1.7) * fade * 1.4);
                    }
                }
                SetLed(zone, i, light.ToColor());
            }
        }
    }
};
REGISTER_EFFECT(Firework);

/*-------------------------------------------------------------*\
| Gradient: a fixed blend from color 1 to color 2 along LEDs.   |
\*-------------------------------------------------------------*/
class Gradient final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(Gradient, "Gradient");

    Gradient() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "A steady blend from the first color to the second along the LEDs",
            2)
    {
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        for(ControllerZone* zone : zones)
        {
            const LedLayout layout = Layout(zone);
            const int count = static_cast<int>(layout.position.size());
            // Stretch so the last step reaches the second color exactly.
            const double stretch = layout.resolution > 1
                ? static_cast<double>(layout.resolution)
                    / (layout.resolution - 1)
                : 0.0;
            for(int i = 0; i < count; ++i)
            {
                const float t = static_cast<float>(std::min(
                    1.0,
                    layout.position[static_cast<std::size_t>(i)]
                        * stretch));
                SetLed(
                    zone,
                    i,
                    ColorUtils::Interpolate(Color(0), Color(1), t));
            }
        }
    }
};
REGISTER_EFFECT(Gradient);

/*-------------------------------------------------------------*\
| Rainbow Sweep: whole zone cycles through the hues over the    |
| clip.                                                         |
\*-------------------------------------------------------------*/
class RainbowSweep final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(RainbowSweep, "Rainbow Sweep");

    RainbowSweep() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "Cycles through the rainbow over the clip",
            0)
    {
        UseSlider2("Cycles", 1, 10, 1);
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        const double cycles = std::max(1u, GetSlider2Val());
        hsv_t hsv;
        hsv.hue = static_cast<unsigned int>(
            Fraction(ClipProgress(4.0) * cycles) * 360.0) % 360;
        hsv.saturation = 255;
        hsv.value = 255;
        Fill(zones, hsv2rgb(&hsv));
    }
};
REGISTER_EFFECT(RainbowSweep);

/*-------------------------------------------------------------*\
| Sparkle: random LEDs flash color 1 over a color 2 background. |
\*-------------------------------------------------------------*/
class Sparkle final : public SimpleEffect
{
public:
    LIGHTTRACK_EFFECT(Sparkle, "Sparkle");

    Sparkle() :
        SimpleEffect(
            ClassName(),
            UI_Name(),
            "Random twinkles over a background color (Speed = density)",
            2),
        random(std::random_device{}())
    {
        UseSpeed(1, 100, 20);
        UseSlider2("Fade (ms)", 50, 2000, 300);
    }

    void EffectState(bool enabled) override
    {
        if(enabled)
        {
            levels.clear();
            last_seconds = -1.0;
        }
        SimpleEffect::EffectState(enabled);
    }

    void StepEffect(std::vector<ControllerZone*> zones) override
    {
        const double now = ElapsedSeconds();
        const double dt = last_seconds < 0.0
            ? 0.0
            : std::clamp(now - last_seconds, 0.0, 0.25);
        last_seconds = now;

        const double fade_s =
            std::max(1u, GetSlider2Val()) / 1000.0;
        const double decay = dt / fade_s;
        // Speed 1..100 -> 0.05..5 new sparkles per LED per second.
        const double spawn_chance = GetSpeed() * 0.05 * dt;
        std::uniform_real_distribution<double> roll(0.0, 1.0);

        for(ControllerZone* zone : zones)
        {
            const int count = static_cast<int>(zone->leds_count());
            std::vector<double>& zone_levels = levels[zone];
            zone_levels.resize(static_cast<std::size_t>(count), 0.0);

            for(int i = 0; i < count; ++i)
            {
                double& level = zone_levels[static_cast<std::size_t>(i)];
                level = std::max(0.0, level - decay);
                if(roll(random) < spawn_chance)
                {
                    level = 1.0;
                }
                SetLed(
                    zone,
                    i,
                    ColorUtils::Interpolate(
                        Color(1),
                        Color(0),
                        static_cast<float>(level * level)));
            }
        }
    }

private:
    std::map<ControllerZone*, std::vector<double>> levels;
    std::mt19937 random;
    double last_seconds = -1.0;
};
REGISTER_EFFECT(Sparkle);

#undef LIGHTTRACK_EFFECT
}
