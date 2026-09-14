#pragma once

#include "OpenRgbTestInterfaces.h"

#include <QTemporaryDir>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <thread>

class TestController : public UnusedControllerAPI
{
public:
    explicit TestController(RGBController_Setup setup) : setup(std::move(setup))
    {
        unsigned int offset = 0;
        for(zone& value : this->setup.zones)
        {
            value.start_idx = offset;
            offset += StoredColors(value);
        }
        colors.resize(offset);
        for(zone& value : this->setup.zones)
        {
            value.colors = colors.empty() ? nullptr : colors.data() + value.start_idx;
        }
    }

    static RGBController_Setup LinearSetup()
    {
        RGBController_Setup setup{};
        setup.name = "Router output";
        setup.type = DEVICE_TYPE_VIRTUAL;
        setup.zones.resize(1);
        setup.zones[0].name = "Output";
        setup.zones[0].type = ZONE_TYPE_LINEAR;
        setup.zones[0].leds_count = 2;
        setup.zones[0].leds_min = 2;
        setup.zones[0].leds_max = 2;
        setup.modes.resize(1);
        setup.modes[0].name = "Direct";
        setup.modes[0].color_mode = MODE_COLORS_PER_LED;
        return setup;
    }

    static unsigned int StoredColors(const zone& value)
    {
        return (value.flags & ZONE_FLAG_MANUALLY_CONFIGURABLE_SIZE_EFFECTS_ONLY)
            && value.leds_count > 1 ? 1 : value.leds_count;
    }

    std::string GetName() override
    {
        if(retired) { ++retired_reads; }
        return setup.name;
    }
    std::string GetDisplayName() override { return setup.name; }
    std::string GetVendor() override { return setup.vendor; }
    std::string GetDescription() override { return setup.description; }
    std::string GetVersion() override { return setup.version; }
    std::string GetSerial() override { return setup.serial; }
    std::string GetLocation() override { return setup.location; }
    device_type GetDeviceType() override { return setup.type; }
    controller_flags GetFlags() override { return setup.flags; }
    bool GetHidden() override { return false; }
    zone GetZone(unsigned int index) override { return setup.zones.at(index); }
    unsigned int GetZoneCount() override { return static_cast<unsigned int>(setup.zones.size()); }
    std::string GetZoneName(unsigned int index) override { return setup.zones.at(index).name; }
    std::string GetZoneDisplayName(unsigned int index) override { return GetZoneName(index); }
    zone_type GetZoneType(unsigned int index) override { return setup.zones.at(index).type; }
    zone_flags GetZoneFlags(unsigned int index) override { return setup.zones.at(index).flags; }
    unsigned int GetZoneLEDsCount(unsigned int index) override { return setup.zones.at(index).leds_count; }
    unsigned int GetLEDsInZone(unsigned int index) override { return StoredColors(setup.zones.at(index)); }
    unsigned int GetZoneStartIndex(unsigned int index) override { return setup.zones.at(index).start_idx; }
    RGBColor* GetZoneColorsPointer(unsigned int index) override { return setup.zones.at(index).colors; }
    matrix_map_type GetZoneMatrixMap(unsigned int index) override { return setup.zones.at(index).matrix_map; }
    const unsigned int* GetZoneMatrixMapData(unsigned int index) override { return setup.zones.at(index).matrix_map.map.data(); }
    unsigned int GetZoneMatrixMapWidth(unsigned int index) override { return setup.zones.at(index).matrix_map.width; }
    unsigned int GetZoneMatrixMapHeight(unsigned int index) override { return setup.zones.at(index).matrix_map.height; }
    unsigned int GetZoneSegmentCount(unsigned int index) override { return static_cast<unsigned int>(setup.zones.at(index).segments.size()); }
    std::string GetZoneSegmentName(unsigned int z, unsigned int s) override { return setup.zones.at(z).segments.at(s).name; }
    unsigned int GetZoneSegmentType(unsigned int z, unsigned int s) override { return setup.zones.at(z).segments.at(s).type; }
    unsigned int GetZoneSegmentStartIndex(unsigned int z, unsigned int s) override { return setup.zones.at(z).segments.at(s).start_idx; }
    unsigned int GetZoneSegmentLEDsCount(unsigned int z, unsigned int s) override { return setup.zones.at(z).segments.at(s).leds_count; }
    const unsigned int* GetZoneSegmentMatrixMapData(unsigned int z, unsigned int s) override { return setup.zones.at(z).segments.at(s).matrix_map.map.data(); }
    unsigned int GetZoneSegmentMatrixMapWidth(unsigned int z, unsigned int s) override { return setup.zones.at(z).segments.at(s).matrix_map.width; }
    unsigned int GetZoneSegmentMatrixMapHeight(unsigned int z, unsigned int s) override { return setup.zones.at(z).segments.at(s).matrix_map.height; }
    unsigned int GetModeCount() override { return static_cast<unsigned int>(setup.modes.size()); }
    std::string GetModeName(unsigned int index) override { return setup.modes.at(index).name; }
    int GetActiveMode() override { return setup.active_mode; }
    void SetActiveMode(int index) override { setup.active_mode = index; }
    void SetCustomMode() override { setup.active_mode = 0; }
    void UpdateMode() override {}
    unsigned int GetLEDCount() override { return static_cast<unsigned int>(colors.size()); }
    RGBColor GetColor(unsigned int index) override { return colors.at(index); }
    void SetColor(unsigned int index, RGBColor color) override { colors.at(index) = color; }
    void SetAllColors(RGBColor color) override { std::fill(colors.begin(), colors.end(), color); }
    void SetAllZoneColors(int index, RGBColor color) override
    {
        const zone& value = setup.zones.at(index);
        std::fill_n(colors.begin() + value.start_idx, StoredColors(value), color);
    }
    void UpdateLEDs() override
    {
        ++updates;
        SignalUpdate(RGBCONTROLLER_UPDATE_REASON_UPDATELEDS);
    }

    void RegisterUpdateCallback(RGBControllerCallback callback, void* context) override
    {
        callbacks[context] = callback;
    }
    void UnregisterUpdateCallback(void* context) override { callbacks.erase(context); }
    void SignalUpdate(unsigned int reason) override
    {
        for(const auto& callback : callbacks)
        {
            callback.second(callback.first, reason, this);
        }
    }

    RGBController_Setup setup;
    std::vector<RGBColor> colors;
    unsigned int updates = 0;
    bool retired = false;
    unsigned int retired_reads = 0;
    std::map<void*, RGBControllerCallback> callbacks;
};

class TestPluginAPI : public UnusedPluginAPI
{
public:
    QTemporaryDir configuration;
    std::vector<RGBControllerInterface*> devices;
    std::map<RGBControllerInterface*, std::unique_ptr<TestController>> virtual_controllers;
    std::function<void()> on_detection;
    unsigned int device_queries = 0;
    unsigned int detection_waits = 0;
    unsigned int created = 0;
    unsigned int deleted = 0;

    filesystem::path GetConfigurationDirectory() override { return configuration.path().toStdString(); }
    std::vector<RGBControllerInterface*> GetRGBControllers() override { ++device_queries; return devices; }
    RGBControllerInterface* CreateVirtualRGBController(RGBController_Setup* setup) override
    {
        auto value = std::make_unique<TestController>(*setup);
        auto* result = value.get();
        virtual_controllers.emplace(result, std::move(value));
        ++created;
        return result;
    }
    void DeleteVirtualRGBController(RGBControllerInterface* controller) override
    {
        if(!virtual_controllers.at(controller)->callbacks.empty())
        {
            throw std::logic_error("Virtual controller callbacks were not removed");
        }
        if(virtual_controllers.erase(controller) != 1)
        {
            throw std::logic_error("Double deletion of a virtual controller");
        }
        ++deleted;
    }
    void WaitForDetection() override
    {
        ++detection_waits;
        if(on_detection)
        {
            std::thread detection(on_detection);
            detection.join();
        }
    }
};
