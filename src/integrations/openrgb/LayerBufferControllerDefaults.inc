// Generated from RGBControllerInterface by
// gen_buffer_controller.py: inert defaults for the parts of the
// controller API that a layer buffer does not need.
    void SetHidden(bool /*hidden*/) override { }
    int GetZoneActiveMode(unsigned int /*zone*/) override { return {}; }
    unsigned int GetZoneLEDsMax(unsigned int /*zone*/) override { return {}; }
    unsigned int GetZoneLEDsMin(unsigned int /*zone*/) override { return {}; }
    unsigned int GetZoneModeCount(unsigned int /*zone*/) override { return {}; }
    unsigned int GetZoneModeBrightness(unsigned int /*zone*/, unsigned int /*mode*/) override { return {}; }
    unsigned int GetZoneModeBrightnessMax(unsigned int /*zone*/, unsigned int /*mode*/) override { return {}; }
    unsigned int GetZoneModeBrightnessMin(unsigned int /*zone*/, unsigned int /*mode*/) override { return {}; }
    RGBColor GetZoneModeColor(unsigned int /*zone*/, unsigned int /*mode*/, unsigned int /*color_index*/) override { return {}; }
    unsigned int GetZoneModeColorMode(unsigned int /*zone*/, unsigned int /*mode*/) override { return {}; }
    unsigned int GetZoneModeColorsCount(unsigned int /*zone*/, unsigned int /*mode*/) override { return {}; }
    unsigned int GetZoneModeColorsMax(unsigned int /*zone*/, unsigned int /*mode*/) override { return {}; }
    unsigned int GetZoneModeColorsMin(unsigned int /*zone*/, unsigned int /*mode*/) override { return {}; }
    unsigned int GetZoneModeDirection(unsigned int /*zone*/, unsigned int /*mode*/) override { return {}; }
    unsigned int GetZoneModeFlags(unsigned int /*zone*/, unsigned int /*mode*/) override { return {}; }
    std::string GetZoneModeName(unsigned int /*zone*/, unsigned int /*mode*/) override { return {}; }
    unsigned int GetZoneModeSpeed(unsigned int /*zone*/, unsigned int /*mode*/) override { return {}; }
    unsigned int GetZoneModeSpeedMax(unsigned int /*zone*/, unsigned int /*mode*/) override { return {}; }
    unsigned int GetZoneModeSpeedMin(unsigned int /*zone*/, unsigned int /*mode*/) override { return {}; }
    segment_flags GetZoneSegmentFlags(unsigned int /*zone*/, unsigned int /*segment*/) override { return {}; }
    matrix_map_type GetZoneSegmentMatrixMap(unsigned int /*zone*/, unsigned int /*segment*/) override { return {}; }
    void SetZoneActiveMode(unsigned int /*zone*/, int /*mode*/) override { }
    void SetZoneColor(unsigned int /*zone*/, unsigned int /*color_index*/, RGBColor /*color*/) override { }
    void SetZoneModeBrightness(unsigned int /*zone*/, unsigned int /*mode*/, unsigned int /*brightness*/) override { }
    void SetZoneModeColor(unsigned int /*zone*/, unsigned int /*mode*/, unsigned int /*color_index*/, RGBColor /*color*/) override { }
    void SetZoneModeColorMode(unsigned int /*zone*/, unsigned int /*mode*/, unsigned int /*color_mode*/) override { }
    void SetZoneModeColorsCount(unsigned int /*zone*/, unsigned int /*mode*/, unsigned int /*count*/) override { }
    void SetZoneModeDirection(unsigned int /*zone*/, unsigned int /*mode*/, unsigned int /*direction*/) override { }
    void SetZoneModeSpeed(unsigned int /*zone*/, unsigned int /*mode*/, unsigned int /*speed*/) override { }
    bool SupportsPerZoneModes() override { return {}; }
    unsigned int GetModeBrightness(unsigned int /*mode*/) override { return {}; }
    unsigned int GetModeBrightnessMax(unsigned int /*mode*/) override { return {}; }
    unsigned int GetModeBrightnessMin(unsigned int /*mode*/) override { return {}; }
    RGBColor GetModeColor(unsigned int /*mode*/, unsigned int /*color_index*/) override { return {}; }
    unsigned int GetModeColorMode(unsigned int /*mode*/) override { return {}; }
    unsigned int GetModeColorsCount(unsigned int /*mode*/) override { return {}; }
    unsigned int GetModeColorsMax(unsigned int /*mode*/) override { return {}; }
    unsigned int GetModeColorsMin(unsigned int /*mode*/) override { return {}; }
    unsigned int GetModeDirection(unsigned int /*mode*/) override { return {}; }
    unsigned int GetModeFlags(unsigned int /*mode*/) override { return {}; }
    unsigned int GetModeSpeed(unsigned int /*mode*/) override { return {}; }
    unsigned int GetModeSpeedMax(unsigned int /*mode*/) override { return {}; }
    unsigned int GetModeSpeedMin(unsigned int /*mode*/) override { return {}; }
    void SetModeBrightness(unsigned int /*mode*/, unsigned int /*brightness*/) override { }
    void SetModeColor(unsigned int /*mode*/, unsigned int /*color_index*/, RGBColor /*color*/) override { }
    void SetModeColorMode(unsigned int /*mode*/, unsigned int /*color_mode*/) override { }
    void SetModeColorsCount(unsigned int /*mode*/, unsigned int /*count*/) override { }
    void SetModeDirection(unsigned int /*mode*/, unsigned int /*direction*/) override { }
    void SetModeSpeed(unsigned int /*mode*/, unsigned int /*speed*/) override { }
    void SetActiveMode(int /*mode*/) override { }
    void SetCustomMode() override { }
    std::string GetLEDName(unsigned int /*led*/) override { return {}; }
    std::string GetLEDDisplayName(unsigned int /*led*/) override { return {}; }
    RGBColor* GetColorsPointer() override { return {}; }
    nlohmann::json GetDeviceSpecificConfigurationSchema() override { return {}; }
    nlohmann::json GetDeviceSpecificConfiguration() override { return {}; }
    void SetDeviceSpecificConfiguration(nlohmann::json /*configuration_json*/) override { }
    nlohmann::json GetDeviceSpecificZoneConfigurationSchema(int /*zone*/) override { return {}; }
    nlohmann::json GetDeviceSpecificZoneConfiguration(int /*zone*/) override { return {}; }
    void SetDeviceSpecificZoneConfiguration(int /*zone*/, nlohmann::json /*configuration_json*/) override { }
    void ClearCallbacks() override { }
    void UpdateZoneLEDs(int /*zone*/) override { }
    void UpdateSingleLED(int /*led*/) override { }
    void UpdateMode() override { }
    void UpdateZoneMode(int /*zone*/) override { }
    void SaveMode() override { }
    void ClearSegments(int /*zone*/) override { }
    void AddSegment(int /*zone*/, segment /*new_segment*/) override { }
    void ConfigureZone(int /*zone_idx*/, zone /*new_zone*/) override { }
    void ResizeZone(int /*zone*/, int /*new_size*/) override { }
    void ConfigureDevice(controller_flags /*new_flags*/, std::string /*new_name*/) override { }
