#pragma once

#include "core/TimelineTypes.h"

#include "RGBControllerInterface.h"

#include <memory>
#include <vector>

class ControllerZone;
class RGBEffect;
class OpenRGBPluginAPIInterface;

namespace lighttrack::openrgb
{
// How a clip's effect is laid onto its LEDs. Matrix zones get a rotated /
// mirrored matrix map, so every effect renders in the corrected direction;
// strips are reversed by 180 degrees or mirror.
struct LayerOrientation
{
    int rotation = 0; // clockwise degrees: 0, 90, 180 or 270
    bool mirror = false; // flip left/right before rotating

    bool IsIdentity() const { return rotation == 0 && !mirror; }
    bool operator==(const LayerOrientation& other) const
    {
        return rotation == other.rotation && mirror == other.mirror;
    }
    bool operator!=(const LayerOrientation& other) const
    {
        return !(*this == other);
    }
};

// Applies the orientation to a matrix map in place.
void TransformMatrixMap(
    matrix_map_type& matrix,
    const LayerOrientation& orientation);

class OpenRgbColorRouter
{
public:
    struct LayerDefinition
    {
        ClipId clip_id;
        int lane_level = 0;
        int sequence = 0;
        std::vector<ControllerZone*> targets;
        LayerOrientation orientation;
    };

    struct TargetOverride
    {
        ControllerZone* target = nullptr;
        RGBColor color = 0;
    };

    explicit OpenRgbColorRouter(OpenRGBPluginAPIInterface* plugin_api);
    ~OpenRgbColorRouter();

    OpenRgbColorRouter(const OpenRgbColorRouter&) = delete;
    OpenRgbColorRouter& operator=(const OpenRgbColorRouter&) = delete;

    void SetTargets(std::vector<ControllerZone*> targets);
    void ConfigureLayers(std::vector<LayerDefinition> layers);
    void Reset();

    bool HasLayer(ClipId clip_id) const;
    std::vector<ControllerZone*> LayerZones(ClipId clip_id) const;
    void BindEffect(ClipId clip_id, RGBEffect* effect);
    void SetLayerActive(ClipId clip_id, bool active);

    void SetOverrides(std::vector<TargetOverride> overrides);
    void StartOutput();
    void RefreshIdleOutput();
    void StopOutput();
    void Blackout();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
