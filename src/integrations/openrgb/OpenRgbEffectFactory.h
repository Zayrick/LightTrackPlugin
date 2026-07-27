#pragma once

#include <memory>

#include "core/effects/EffectDescriptor.h"
#include <nlohmann/json.hpp>
#include "RGBEffect.h"

namespace lighttrack::openrgb
{
class EffectFactory
{
public:
    std::unique_ptr<RGBEffect> Create(const EffectDescriptor& descriptor) const;
    void ApplySettings(RGBEffect& effect, const nlohmann::json& settings) const;
};
}
