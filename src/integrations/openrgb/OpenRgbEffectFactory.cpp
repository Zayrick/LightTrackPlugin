#include "integrations/openrgb/OpenRgbEffectFactory.h"

#include "ColorUtils.h"
#include "EffectListManager.h"
#include "OpenRGBEffectSettings.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace lighttrack::openrgb
{
std::unique_ptr<RGBEffect> EffectFactory::Create(
    const EffectDescriptor& descriptor) const
{
    std::function<RGBEffect*()> constructor =
        EffectListManager::get()->GetEffectConstructor(descriptor.id.toStdString());
    if(!constructor)
    {
        return {};
    }

    std::unique_ptr<RGBEffect> effect(constructor());
    if(!effect)
    {
        return {};
    }

    effect->SetFPS(OpenRGBEffectSettings::globalSettings.fps);
    effect->SetBrightness(OpenRGBEffectSettings::globalSettings.brightness);
    effect->SetTemperature(OpenRGBEffectSettings::globalSettings.temperature);
    effect->SetTint(OpenRGBEffectSettings::globalSettings.tint);

    std::vector<RGBColor> colors;
    colors.reserve(effect->EffectDetails.UserColors);
    for(unsigned int index = 0; index < effect->EffectDetails.UserColors; ++index)
    {
        if(OpenRGBEffectSettings::globalSettings.use_prefered_colors
            && index < OpenRGBEffectSettings::globalSettings.prefered_colors.size())
        {
            colors.push_back(OpenRGBEffectSettings::globalSettings.prefered_colors[index]);
        }
        else
        {
            colors.push_back(ColorUtils::RandomRGBColor());
        }
    }

    effect->SetUserColors(colors);
    effect->SetRandomColorsEnabled(OpenRGBEffectSettings::globalSettings.prefer_random);
    return effect;
}

void EffectFactory::ApplySettings(
    RGBEffect& effect, const nlohmann::json& settings) const
{
    if(!settings.is_object())
    {
        throw std::runtime_error("Invalid effect settings");
    }

    if(settings.contains("EffectClassName"))
    {
        if(!settings["EffectClassName"].is_string()
            || settings["EffectClassName"].get<std::string>()
                != effect.EffectDetails.EffectClassName)
        {
            throw std::runtime_error(
                "Effect settings do not match the timeline effect");
        }
    }

    if(settings.contains("FPS"))
    {
        effect.SetFPS(settings["FPS"].get<unsigned int>());
    }
    if(settings.contains("UserColors"))
    {
        if(!settings["UserColors"].is_array() && !settings["UserColors"].is_null())
        {
            throw std::runtime_error("Invalid effect color list");
        }

        std::vector<RGBColor> colors;
        for(const nlohmann::json& color : settings["UserColors"])
        {
            colors.push_back(color.get<RGBColor>());
        }
        effect.SetUserColors(colors);
    }
    if(settings.contains("Speed"))
    {
        effect.SetSpeed(settings["Speed"].get<unsigned int>());
    }
    if(settings.contains("Slider2Val"))
    {
        effect.SetSlider2Val(settings["Slider2Val"].get<unsigned int>());
    }
    if(settings.contains("RandomColors"))
    {
        effect.SetRandomColorsEnabled(settings["RandomColors"].get<bool>());
    }
    if(settings.contains("AllowOnlyFirst"))
    {
        effect.SetOnlyFirstColorEnabled(settings["AllowOnlyFirst"].get<bool>());
    }
    if(settings.contains("CustomName"))
    {
        effect.EffectDetails.CustomName = settings["CustomName"].get<std::string>();
    }
    if(settings.contains("Brightness"))
    {
        effect.SetBrightness(settings["Brightness"].get<unsigned int>());
    }
    if(settings.contains("Temperature"))
    {
        effect.SetTemperature(settings["Temperature"].get<int>());
    }
    if(settings.contains("Tint"))
    {
        effect.SetTint(settings["Tint"].get<int>());
    }
    if(settings.contains("CustomSettings"))
    {
        effect.LoadCustomSettings(settings["CustomSettings"]);
    }
}
}
