#pragma once

#include "RGBControllerInterface.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace lighttrack::openrgb
{
// In-memory color buffer shaped like a real controller (same zones,
// segments and matrix maps). Each timeline clip draws into one of these.
//
// Unlike controllers created through the OpenRGB plugin API, it owns no
// device thread: OpenRGB starts a thread per controller that wakes every
// millisecond, so hundreds of clips meant hundreds of busy threads.
class LayerBufferController final : public RGBControllerInterface
{
public:
    explicit LayerBufferController(RGBController_Setup new_setup) :
        setup(std::move(new_setup))
    {
        unsigned int offset = 0;
        for(zone& value : setup.zones)
        {
            value.start_idx = offset;
            offset += StoredColors(value);
        }
        colors.assign(offset, 0);
        for(zone& value : setup.zones)
        {
            value.colors = colors.empty() ? nullptr : colors.data() + value.start_idx;
        }
    }

    // Effects-only resizable zones store a single color (as in OpenRGB).
    static unsigned int StoredColors(const zone& value)
    {
        return (value.flags & ZONE_FLAG_MANUALLY_CONFIGURABLE_SIZE_EFFECTS_ONLY)
            && value.leds_count > 1 ? 1 : value.leds_count;
    }

    std::string GetName() override { return setup.name; }
    std::string GetDisplayName() override { return setup.name; }
    std::string GetVendor() override { return setup.vendor; }
    std::string GetDescription() override { return setup.description; }
    std::string GetVersion() override { return setup.version; }
    std::string GetSerial() override { return setup.serial; }
    std::string GetLocation() override { return setup.location; }
    device_type GetDeviceType() override { return setup.type; }
    controller_flags GetFlags() override { return setup.flags; }
    bool GetHidden() override { return false; }

    zone GetZone(unsigned int index) override { return ZoneAt(index); }
    unsigned int GetZoneCount() override { return static_cast<unsigned int>(setup.zones.size()); }
    std::string GetZoneName(unsigned int index) override { return ZoneAt(index).name; }
    std::string GetZoneDisplayName(unsigned int index) override
    {
        const zone& value = ZoneAt(index);
        return (value.flags & ZONE_FLAG_MANUALLY_CONFIGURED_NAME)
            ? value.display_name
            : value.name;
    }
    zone_type GetZoneType(unsigned int index) override { return ZoneAt(index).type; }
    zone_flags GetZoneFlags(unsigned int index) override { return ZoneAt(index).flags; }
    unsigned int GetZoneLEDsCount(unsigned int index) override { return ZoneAt(index).leds_count; }
    unsigned int GetLEDsInZone(unsigned int index) override { return StoredColors(ZoneAt(index)); }
    unsigned int GetZoneStartIndex(unsigned int index) override { return ZoneAt(index).start_idx; }
    RGBColor* GetZoneColorsPointer(unsigned int index) override { return ZoneAt(index).colors; }
    RGBColor GetZoneColor(unsigned int index, unsigned int color_index) override
    {
        return GetColor(ZoneAt(index).start_idx + color_index);
    }
    matrix_map_type GetZoneMatrixMap(unsigned int index) override { return ZoneAt(index).matrix_map; }
    const unsigned int* GetZoneMatrixMapData(unsigned int index) override { return ZoneAt(index).matrix_map.map.data(); }
    unsigned int GetZoneMatrixMapWidth(unsigned int index) override { return ZoneAt(index).matrix_map.width; }
    unsigned int GetZoneMatrixMapHeight(unsigned int index) override { return ZoneAt(index).matrix_map.height; }

    unsigned int GetZoneSegmentCount(unsigned int index) override { return static_cast<unsigned int>(ZoneAt(index).segments.size()); }
    std::string GetZoneSegmentName(unsigned int z, unsigned int s) override { return SegmentAt(z, s).name; }
    unsigned int GetZoneSegmentType(unsigned int z, unsigned int s) override { return SegmentAt(z, s).type; }
    unsigned int GetZoneSegmentStartIndex(unsigned int z, unsigned int s) override { return SegmentAt(z, s).start_idx; }
    unsigned int GetZoneSegmentLEDsCount(unsigned int z, unsigned int s) override { return SegmentAt(z, s).leds_count; }
    const unsigned int* GetZoneSegmentMatrixMapData(unsigned int z, unsigned int s) override { return SegmentAt(z, s).matrix_map.map.data(); }
    unsigned int GetZoneSegmentMatrixMapWidth(unsigned int z, unsigned int s) override { return SegmentAt(z, s).matrix_map.width; }
    unsigned int GetZoneSegmentMatrixMapHeight(unsigned int z, unsigned int s) override { return SegmentAt(z, s).matrix_map.height; }

    unsigned int GetModeCount() override { return static_cast<unsigned int>(setup.modes.size()); }
    std::string GetModeName(unsigned int index) override
    {
        return index < setup.modes.size() ? setup.modes[index].name : std::string();
    }
    int GetActiveMode() override { return setup.active_mode; }

    unsigned int GetLEDCount() override { return static_cast<unsigned int>(colors.size()); }
    RGBColor GetColor(unsigned int index) override
    {
        return index < colors.size() ? colors[index] : 0;
    }
    void SetColor(unsigned int index, RGBColor color) override
    {
        if(index < colors.size())
        {
            colors[index] = color;
        }
    }
    void SetAllColors(RGBColor color) override
    {
        std::fill(colors.begin(), colors.end(), color);
    }
    void SetAllZoneColors(int index, RGBColor color) override
    {
        if(index < 0 || index >= static_cast<int>(setup.zones.size()))
        {
            return;
        }
        const zone& value = setup.zones[static_cast<std::size_t>(index)];
        std::fill_n(colors.begin() + value.start_idx, StoredColors(value), color);
    }

    // No device: "updating" just tells the listeners (the color router)
    // synchronously, on the effect thread that drew the frame.
    void UpdateLEDs() override
    {
        SignalUpdate(RGBCONTROLLER_UPDATE_REASON_UPDATELEDS);
    }

    void RegisterUpdateCallback(RGBControllerCallback callback, void* context) override
    {
        std::lock_guard<std::mutex> guard(callback_mutex);
        callbacks[context] = callback;
    }
    void UnregisterUpdateCallback(void* context) override
    {
        std::lock_guard<std::mutex> guard(callback_mutex);
        callbacks.erase(context);
    }
    void SignalUpdate(unsigned int reason) override
    {
        std::vector<std::pair<void*, RGBControllerCallback>> copy;
        {
            std::lock_guard<std::mutex> guard(callback_mutex);
            copy.assign(callbacks.begin(), callbacks.end());
        }
        for(const auto& callback : copy)
        {
            callback.second(callback.first, reason, this);
        }
    }

#include "integrations/openrgb/LayerBufferControllerDefaults.inc"

private:
    const zone& ZoneAt(unsigned int index) const
    {
        static const zone empty_zone;
        return index < setup.zones.size() ? setup.zones[index] : empty_zone;
    }

    const segment& SegmentAt(unsigned int zone_index, unsigned int segment_index) const
    {
        static const segment empty_segment;
        const zone& value = ZoneAt(zone_index);
        return segment_index < value.segments.size()
            ? value.segments[segment_index]
            : empty_segment;
    }

    RGBController_Setup setup;
    std::vector<RGBColor> colors;
    std::mutex callback_mutex;
    std::map<void*, RGBControllerCallback> callbacks;
};
}
