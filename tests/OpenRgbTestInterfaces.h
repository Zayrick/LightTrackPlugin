#pragma once

#include "OpenRGBPluginInterface.h"
#include <stdexcept>

// Inert defaults for API methods outside the test host's supported surface.

class UnusedControllerAPI : public RGBControllerInterface
{
public:
    std::string GetName() override { throw std::logic_error("Unexpected controller call: GetName"); }
    std::string GetVendor() override { throw std::logic_error("Unexpected controller call: GetVendor"); }
    std::string GetDescription() override { throw std::logic_error("Unexpected controller call: GetDescription"); }
    std::string GetVersion() override { throw std::logic_error("Unexpected controller call: GetVersion"); }
    std::string GetSerial() override { throw std::logic_error("Unexpected controller call: GetSerial"); }
    std::string GetLocation() override { throw std::logic_error("Unexpected controller call: GetLocation"); }
    std::string GetDisplayName() override { throw std::logic_error("Unexpected controller call: GetDisplayName"); }
    device_type GetDeviceType() override { throw std::logic_error("Unexpected controller call: GetDeviceType"); }
    controller_flags GetFlags() override { throw std::logic_error("Unexpected controller call: GetFlags"); }
    bool GetHidden() override { throw std::logic_error("Unexpected controller call: GetHidden"); }
    void SetHidden(bool hidden) override { throw std::logic_error("Unexpected controller call: SetHidden"); }
    zone GetZone(unsigned int zone_idx) override { throw std::logic_error("Unexpected controller call: GetZone"); }
    int GetZoneActiveMode(unsigned int zone) override { throw std::logic_error("Unexpected controller call: GetZoneActiveMode"); }
    RGBColor GetZoneColor(unsigned int zone, unsigned int color_index) override { throw std::logic_error("Unexpected controller call: GetZoneColor"); }
    RGBColor* GetZoneColorsPointer(unsigned int zone) override { throw std::logic_error("Unexpected controller call: GetZoneColorsPointer"); }
    unsigned int GetZoneCount() override { throw std::logic_error("Unexpected controller call: GetZoneCount"); }
    std::string GetZoneDisplayName(unsigned int zone) override { throw std::logic_error("Unexpected controller call: GetZoneDisplayName"); }
    zone_flags GetZoneFlags(unsigned int zone) override { throw std::logic_error("Unexpected controller call: GetZoneFlags"); }
    unsigned int GetZoneLEDsCount(unsigned int zone) override { throw std::logic_error("Unexpected controller call: GetZoneLEDsCount"); }
    unsigned int GetZoneLEDsMax(unsigned int zone) override { throw std::logic_error("Unexpected controller call: GetZoneLEDsMax"); }
    unsigned int GetZoneLEDsMin(unsigned int zone) override { throw std::logic_error("Unexpected controller call: GetZoneLEDsMin"); }
    matrix_map_type GetZoneMatrixMap(unsigned int zone) override { throw std::logic_error("Unexpected controller call: GetZoneMatrixMap"); }
    const unsigned int* GetZoneMatrixMapData(unsigned int zone) override { throw std::logic_error("Unexpected controller call: GetZoneMatrixMapData"); }
    unsigned int GetZoneMatrixMapHeight(unsigned int zone) override { throw std::logic_error("Unexpected controller call: GetZoneMatrixMapHeight"); }
    unsigned int GetZoneMatrixMapWidth(unsigned int zone) override { throw std::logic_error("Unexpected controller call: GetZoneMatrixMapWidth"); }
    unsigned int GetZoneModeCount(unsigned int zone) override { throw std::logic_error("Unexpected controller call: GetZoneModeCount"); }
    unsigned int GetZoneModeBrightness(unsigned int zone, unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetZoneModeBrightness"); }
    unsigned int GetZoneModeBrightnessMax(unsigned int zone, unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetZoneModeBrightnessMax"); }
    unsigned int GetZoneModeBrightnessMin(unsigned int zone, unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetZoneModeBrightnessMin"); }
    RGBColor GetZoneModeColor(unsigned int zone, unsigned int mode, unsigned int color_index) override { throw std::logic_error("Unexpected controller call: GetZoneModeColor"); }
    unsigned int GetZoneModeColorMode(unsigned int zone, unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetZoneModeColorMode"); }
    unsigned int GetZoneModeColorsCount(unsigned int zone, unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetZoneModeColorsCount"); }
    unsigned int GetZoneModeColorsMax(unsigned int zone, unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetZoneModeColorsMax"); }
    unsigned int GetZoneModeColorsMin(unsigned int zone, unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetZoneModeColorsMin"); }
    unsigned int GetZoneModeDirection(unsigned int zone, unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetZoneModeDirection"); }
    unsigned int GetZoneModeFlags(unsigned int zone, unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetZoneModeFlags"); }
    std::string GetZoneModeName(unsigned int zone, unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetZoneModeName"); }
    unsigned int GetZoneModeSpeed(unsigned int zone, unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetZoneModeSpeed"); }
    unsigned int GetZoneModeSpeedMax(unsigned int zone, unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetZoneModeSpeedMax"); }
    unsigned int GetZoneModeSpeedMin(unsigned int zone, unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetZoneModeSpeedMin"); }
    std::string GetZoneName(unsigned int zone) override { throw std::logic_error("Unexpected controller call: GetZoneName"); }
    unsigned int GetZoneSegmentCount(unsigned int zone) override { throw std::logic_error("Unexpected controller call: GetZoneSegmentCount"); }
    segment_flags GetZoneSegmentFlags(unsigned int zone, unsigned int segment) override { throw std::logic_error("Unexpected controller call: GetZoneSegmentFlags"); }
    unsigned int GetZoneSegmentLEDsCount(unsigned int zone, unsigned int segment) override { throw std::logic_error("Unexpected controller call: GetZoneSegmentLEDsCount"); }
    matrix_map_type GetZoneSegmentMatrixMap(unsigned int zone, unsigned int segment) override { throw std::logic_error("Unexpected controller call: GetZoneSegmentMatrixMap"); }
    const unsigned int * GetZoneSegmentMatrixMapData(unsigned int zone, unsigned int segment) override { throw std::logic_error("Unexpected controller call: GetZoneSegmentMatrixMapData"); }
    unsigned int GetZoneSegmentMatrixMapHeight(unsigned int zone, unsigned int segment) override { throw std::logic_error("Unexpected controller call: GetZoneSegmentMatrixMapHeight"); }
    unsigned int GetZoneSegmentMatrixMapWidth(unsigned int zone, unsigned int segment) override { throw std::logic_error("Unexpected controller call: GetZoneSegmentMatrixMapWidth"); }
    std::string GetZoneSegmentName(unsigned int zone, unsigned int segment) override { throw std::logic_error("Unexpected controller call: GetZoneSegmentName"); }
    unsigned int GetZoneSegmentStartIndex(unsigned int zone, unsigned int segment) override { throw std::logic_error("Unexpected controller call: GetZoneSegmentStartIndex"); }
    unsigned int GetZoneSegmentType(unsigned int zone, unsigned int segment) override { throw std::logic_error("Unexpected controller call: GetZoneSegmentType"); }
    unsigned int GetZoneStartIndex(unsigned int zone) override { throw std::logic_error("Unexpected controller call: GetZoneStartIndex"); }
    zone_type GetZoneType(unsigned int zone) override { throw std::logic_error("Unexpected controller call: GetZoneType"); }
    unsigned int GetLEDsInZone(unsigned int zone) override { throw std::logic_error("Unexpected controller call: GetLEDsInZone"); }
    void SetZoneActiveMode(unsigned int zone, int mode) override { throw std::logic_error("Unexpected controller call: SetZoneActiveMode"); }
    void SetZoneColor(unsigned int zone, unsigned int color_index, RGBColor color) override { throw std::logic_error("Unexpected controller call: SetZoneColor"); }
    void SetZoneModeBrightness(unsigned int zone, unsigned int mode, unsigned int brightness) override { throw std::logic_error("Unexpected controller call: SetZoneModeBrightness"); }
    void SetZoneModeColor(unsigned int zone, unsigned int mode, unsigned int color_index, RGBColor color) override { throw std::logic_error("Unexpected controller call: SetZoneModeColor"); }
    void SetZoneModeColorMode(unsigned int zone, unsigned int mode, unsigned int color_mode) override { throw std::logic_error("Unexpected controller call: SetZoneModeColorMode"); }
    void SetZoneModeColorsCount(unsigned int zone, unsigned int mode, unsigned int count) override { throw std::logic_error("Unexpected controller call: SetZoneModeColorsCount"); }
    void SetZoneModeDirection(unsigned int zone, unsigned int mode, unsigned int direction) override { throw std::logic_error("Unexpected controller call: SetZoneModeDirection"); }
    void SetZoneModeSpeed(unsigned int zone, unsigned int mode, unsigned int speed) override { throw std::logic_error("Unexpected controller call: SetZoneModeSpeed"); }
    bool SupportsPerZoneModes() override { throw std::logic_error("Unexpected controller call: SupportsPerZoneModes"); }
    unsigned int GetModeCount() override { throw std::logic_error("Unexpected controller call: GetModeCount"); }
    unsigned int GetModeBrightness(unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetModeBrightness"); }
    unsigned int GetModeBrightnessMax(unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetModeBrightnessMax"); }
    unsigned int GetModeBrightnessMin(unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetModeBrightnessMin"); }
    RGBColor GetModeColor(unsigned int mode, unsigned int color_index) override { throw std::logic_error("Unexpected controller call: GetModeColor"); }
    unsigned int GetModeColorMode(unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetModeColorMode"); }
    unsigned int GetModeColorsCount(unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetModeColorsCount"); }
    unsigned int GetModeColorsMax(unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetModeColorsMax"); }
    unsigned int GetModeColorsMin(unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetModeColorsMin"); }
    unsigned int GetModeDirection(unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetModeDirection"); }
    unsigned int GetModeFlags(unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetModeFlags"); }
    std::string GetModeName(unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetModeName"); }
    unsigned int GetModeSpeed(unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetModeSpeed"); }
    unsigned int GetModeSpeedMax(unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetModeSpeedMax"); }
    unsigned int GetModeSpeedMin(unsigned int mode) override { throw std::logic_error("Unexpected controller call: GetModeSpeedMin"); }
    void SetModeBrightness(unsigned int mode, unsigned int brightness) override { throw std::logic_error("Unexpected controller call: SetModeBrightness"); }
    void SetModeColor(unsigned int mode, unsigned int color_index, RGBColor color) override { throw std::logic_error("Unexpected controller call: SetModeColor"); }
    void SetModeColorMode(unsigned int mode, unsigned int color_mode) override { throw std::logic_error("Unexpected controller call: SetModeColorMode"); }
    void SetModeColorsCount(unsigned int mode, unsigned int count) override { throw std::logic_error("Unexpected controller call: SetModeColorsCount"); }
    void SetModeDirection(unsigned int mode, unsigned int direction) override { throw std::logic_error("Unexpected controller call: SetModeDirection"); }
    void SetModeSpeed(unsigned int mode, unsigned int speed) override { throw std::logic_error("Unexpected controller call: SetModeSpeed"); }
    int GetActiveMode() override { throw std::logic_error("Unexpected controller call: GetActiveMode"); }
    void SetActiveMode(int mode) override { throw std::logic_error("Unexpected controller call: SetActiveMode"); }
    void SetCustomMode() override { throw std::logic_error("Unexpected controller call: SetCustomMode"); }
    unsigned int GetLEDCount() override { throw std::logic_error("Unexpected controller call: GetLEDCount"); }
    std::string GetLEDName(unsigned int led) override { throw std::logic_error("Unexpected controller call: GetLEDName"); }
    std::string GetLEDDisplayName(unsigned int led) override { throw std::logic_error("Unexpected controller call: GetLEDDisplayName"); }
    RGBColor GetColor(unsigned int led) override { throw std::logic_error("Unexpected controller call: GetColor"); }
    RGBColor* GetColorsPointer() override { throw std::logic_error("Unexpected controller call: GetColorsPointer"); }
    void SetColor(unsigned int led, RGBColor color) override { throw std::logic_error("Unexpected controller call: SetColor"); }
    void SetAllColors(RGBColor color) override { throw std::logic_error("Unexpected controller call: SetAllColors"); }
    void SetAllZoneColors(int zone, RGBColor color) override { throw std::logic_error("Unexpected controller call: SetAllZoneColors"); }
    nlohmann::json GetDeviceSpecificConfigurationSchema() override { throw std::logic_error("Unexpected controller call: GetDeviceSpecificConfigurationSchema"); }
    nlohmann::json GetDeviceSpecificConfiguration() override { throw std::logic_error("Unexpected controller call: GetDeviceSpecificConfiguration"); }
    void SetDeviceSpecificConfiguration(nlohmann::json configuration_json) override { throw std::logic_error("Unexpected controller call: SetDeviceSpecificConfiguration"); }
    nlohmann::json GetDeviceSpecificZoneConfigurationSchema(int zone) override { throw std::logic_error("Unexpected controller call: GetDeviceSpecificZoneConfigurationSchema"); }
    nlohmann::json GetDeviceSpecificZoneConfiguration(int zone) override { throw std::logic_error("Unexpected controller call: GetDeviceSpecificZoneConfiguration"); }
    void SetDeviceSpecificZoneConfiguration(int zone, nlohmann::json configuration_json) override { throw std::logic_error("Unexpected controller call: SetDeviceSpecificZoneConfiguration"); }
    void RegisterUpdateCallback(RGBControllerCallback new_callback, void * new_callback_arg) override { throw std::logic_error("Unexpected controller call: RegisterUpdateCallback"); }
    void UnregisterUpdateCallback(void * callback_arg) override { throw std::logic_error("Unexpected controller call: UnregisterUpdateCallback"); }
    void ClearCallbacks() override { throw std::logic_error("Unexpected controller call: ClearCallbacks"); }
    void SignalUpdate(unsigned int update_reason) override { throw std::logic_error("Unexpected controller call: SignalUpdate"); }
    void UpdateLEDs() override { throw std::logic_error("Unexpected controller call: UpdateLEDs"); }
    void UpdateZoneLEDs(int zone) override { throw std::logic_error("Unexpected controller call: UpdateZoneLEDs"); }
    void UpdateSingleLED(int led) override { throw std::logic_error("Unexpected controller call: UpdateSingleLED"); }
    void UpdateMode() override { throw std::logic_error("Unexpected controller call: UpdateMode"); }
    void UpdateZoneMode(int zone) override { throw std::logic_error("Unexpected controller call: UpdateZoneMode"); }
    void SaveMode() override { throw std::logic_error("Unexpected controller call: SaveMode"); }
    void ClearSegments(int zone) override { throw std::logic_error("Unexpected controller call: ClearSegments"); }
    void AddSegment(int zone, segment new_segment) override { throw std::logic_error("Unexpected controller call: AddSegment"); }
    void ConfigureZone(int zone_idx, zone new_zone) override { throw std::logic_error("Unexpected controller call: ConfigureZone"); }
    void ResizeZone(int zone, int new_size) override { throw std::logic_error("Unexpected controller call: ResizeZone"); }
    void ConfigureDevice(controller_flags new_flags, std::string new_name) override { throw std::logic_error("Unexpected controller call: ConfigureDevice"); }
};

class UnusedPluginAPI : public OpenRGBPluginAPIInterface
{
public:
    void LogEntry(const char* filename, int line, unsigned int level, const char* fmt, ...) override {  }
    RGBControllerInterface* CreateVirtualRGBController(RGBController_Setup* setup) override { return {}; }
    void DeleteVirtualRGBController(RGBControllerInterface* rgb_controller) override {  }
    void RegisterVirtualRGBController(RGBControllerInterface* rgb_controller) override {  }
    void RegisterVirtualRGBControllerInThread(RGBControllerInterface* rgb_controller) override {  }
    void UnregisterVirtualRGBController(RGBControllerInterface* rgb_controller) override {  }
    void UpdateVirtualRGBController(RGBControllerInterface* rgb_controller, RGBController_Setup* setup) override {  }
    void UnregisterVirtualRGBControllerInThread(RGBControllerInterface* rgb_controller) override {  }
    void ClearActiveProfile() override {  }
    std::vector<std::string> GetProfileList() override { return {}; }
    bool LoadProfile(std::string profile_name) override { return {}; }
    bool SaveProfileFromPlugin(std::string profile_name, std::string plugin_name, nlohmann::json plugin_data) override { return {}; }
    filesystem::path GetConfigurationDirectory() override { return {}; }
    bool GetDetectionEnabled() override { return {}; }
    unsigned int GetDetectionPercent() override { return {}; }
    std::string GetDetectionString() override { return {}; }
    void RescanDevices() override {  }
    void WaitForDetection() override {  }
    std::vector<RGBControllerInterface*> GetRGBControllers() override { return {}; }
    nlohmann::json GetDeviceDescriptionJSON(RGBControllerInterface* controller) override { return {}; }
    nlohmann::json GetLEDDescriptionJSON(led led) override { return {}; }
    nlohmann::json GetMatrixMapDescriptionJSON(matrix_map_type matrix_map) override { return {}; }
    nlohmann::json GetModeDescriptionJSON(mode mode) override { return {}; }
    nlohmann::json GetSegmentDescriptionJSON(segment segment) override { return {}; }
    nlohmann::json GetZoneDescriptionJSON(zone zone) override { return {}; }
    RGBControllerInterface* SetDeviceDescriptionJSON(nlohmann::json controller_json) override { return {}; }
    led SetLEDDescriptionJSON(nlohmann::json led_json) override { return {}; }
    matrix_map_type SetMatrixMapDescriptionJSON(nlohmann::json matrix_map_json) override { return {}; }
    mode SetModeDescriptionJSON(nlohmann::json mode_json) override { return {}; }
    segment SetSegmentDescriptionJSON(nlohmann::json segment_json) override { return {}; }
    zone SetZoneDescriptionJSON(nlohmann::json zone_json) override { return {}; }
    bool CompareControllers(RGBControllerInterface* controller_1, RGBControllerInterface* controller_2) override { return {}; }
    std::string DeviceTypeToString(device_type type) override { return {}; }
    bool SetModeValuesFromMode(mode& destination, mode& source) override { return {}; }
    nlohmann::json GetSettings(std::string settings_key) override { return {}; }
    void SaveSettings() override {  }
    void SetSettings(std::string settings_key, nlohmann::json new_settings) override {  }
};
