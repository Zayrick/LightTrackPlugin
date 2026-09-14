#include "EffectManager.h"

#include "integrations/openrgb/OpenRgbRenderCoordinator.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <utility>

EffectManager* EffectManager::instance;

EffectManager::EffectManager() :
    clock(new std::chrono::steady_clock())
{
}

EffectManager* EffectManager::Get()
{
    if(!instance)
    {
        instance = new EffectManager();
    }
    return instance;
}

void EffectManager::SetEffectActive(RGBEffect* effect)
{
    effect->EffectState(true);

    std::lock_guard<std::mutex> guard(lock);
    if(EffectThreads.find(effect) != EffectThreads.end())
    {
        return;
    }

    ActiveEffects.push_back(effect);
    EffectThreads[effect] = nullptr;
    EffectThreads[effect] = new std::thread(
        &EffectManager::EffectThreadFunction,
        this,
        effect);
}

void EffectManager::SetEffectUnActive(RGBEffect* effect)
{
    std::thread* thread = nullptr;
    {
        std::lock_guard<std::mutex> guard(lock);
        const auto found = EffectThreads.find(effect);
        if(found != EffectThreads.end())
        {
            thread = found->second;
            EffectThreads.erase(found);

            const auto active = std::find(
                ActiveEffects.begin(),
                ActiveEffects.end(),
                effect);
            if(active != ActiveEffects.end())
            {
                ActiveEffects.erase(active);
            }
        }
    }

    if(thread != nullptr)
    {
        thread->join();
        delete thread;
    }

    // Stop effect-owned resources only after StepEffect can no longer run.
    effect->EffectState(false);
}

bool EffectManager::IsActive(RGBEffect* effect)
{
    std::lock_guard<std::mutex> guard(lock);
    return EffectThreads.find(effect) != EffectThreads.end();
}

void EffectManager::RemoveMapping(RGBEffect* effect)
{
    std::lock_guard<std::mutex> guard(lock);
    effect_zones.erase(effect);
    unresolved_zones.erase(effect);
    previews.erase(effect);
}

void EffectManager::ClearAssignments()
{
    std::lock_guard<std::mutex> guard(lock);
    effect_zones.clear();
    unresolved_zones.clear();
    previews.clear();
}

void EffectManager::Assign(
    std::vector<ControllerZone*> controller_zones,
    RGBEffect* effect)
{
    printf(
        "[OpenRGBEffectsPlugin] Assigning %zu zones to %s\n",
        controller_zones.size(),
        effect->EffectDetails.EffectName.c_str());

    std::lock_guard<std::mutex> guard(lock);
    effect_zones[effect] = controller_zones;

    for(auto& entry : effect_zones)
    {
        RGBEffect* other_effect = entry.first;
        if(other_effect == effect)
        {
            continue;
        }

        std::vector<ControllerZone*> remaining_zones;
        for(ControllerZone* zone : entry.second)
        {
            if(std::find(
                    controller_zones.begin(),
                    controller_zones.end(),
                    zone) == controller_zones.end())
            {
                remaining_zones.push_back(zone);
            }
        }
        entry.second = std::move(remaining_zones);
    }

    std::set<RGBControllerInterface*> controllers;
    for(ControllerZone* controller_zone : controller_zones)
    {
        if(controller_zone != nullptr
            && controller_zone->controller != nullptr)
        {
            controllers.insert(controller_zone->controller);
        }
    }

    for(RGBControllerInterface* controller : controllers)
    {
        for(unsigned int index = 0;
            index < controller->GetModeCount();
            ++index)
        {
            if(controller->GetModeName(index) == "Direct")
            {
                if(controller->GetActiveMode() != static_cast<int>(index))
                {
                    controller->SetActiveMode(index);
                }
                break;
            }
        }
    }

    NotifySelectionChanged(effect);
}

void EffectManager::SetUnresolvedZones(RGBEffect* effect,
    const std::vector<nlohmann::json>& zones)
{
    std::lock_guard<std::mutex> guard(lock);
    if(zones.empty())
    {
        unresolved_zones.erase(effect);
    }
    else
    {
        unresolved_zones[effect] = zones;
        effect_zones.emplace(effect, std::vector<ControllerZone*>{});
    }
}

std::vector<nlohmann::json> EffectManager::GetUnresolvedZones(RGBEffect* effect)
{
    std::lock_guard<std::mutex> guard(lock);
    const auto found = unresolved_zones.find(effect);
    return found == unresolved_zones.end()
        ? std::vector<nlohmann::json>{} : found->second;
}

void EffectManager::RemapAssignedZones(const std::vector<ControllerZone*>& new_zones)
{
    std::lock_guard<std::mutex> guard(lock);
    std::lock_guard<std::mutex> frame_guard(
        lighttrack::openrgb::ControllerFrameMutex());
    std::set<ControllerZone*> claimed;
    for(auto& entry : effect_zones)
    {
        std::vector<ControllerZone*> remapped;
        std::vector<nlohmann::json> descriptors = unresolved_zones[entry.first];
        for(ControllerZone* previous : entry.second)
        {
            if(std::find(new_zones.begin(), new_zones.end(), previous) != new_zones.end())
            {
                if(claimed.insert(previous).second)
                {
                    remapped.push_back(previous);
                }
            }
            else
            {
                // As in the upstream API, callers must keep old controllers
                // alive until the remap finishes. Detection uses ClearAssignments.
                descriptors.push_back(previous->to_json());
            }
        }
        std::vector<nlohmann::json> missing;
        for(const auto& descriptor : descriptors)
        {
            const auto found = std::find_if(new_zones.begin(), new_zones.end(),
                [&](ControllerZone* candidate)
                {
                    return claimed.count(candidate) == 0 && candidate->matches_json(descriptor);
                });
            if(found == new_zones.end())
            {
                missing.push_back(descriptor);
                continue;
            }
            (*found)->reverse = descriptor.value("reverse", false);
            (*found)->self_brightness = descriptor.value("self_brightness", 100U);
            claimed.insert(*found);
            remapped.push_back(*found);
        }
        entry.second = std::move(remapped);
        if(missing.empty())
        {
            unresolved_zones.erase(entry.first);
        }
        else
        {
            unresolved_zones[entry.first] = std::move(missing);
        }
        NotifySelectionChanged(entry.first);
    }
}

std::vector<ControllerZone*> EffectManager::GetAssignedZones(
    RGBEffect* effect)
{
    std::lock_guard<std::mutex> guard(lock);
    return effect_zones[effect];
}

std::map<RGBEffect*, std::vector<ControllerZone*>>
EffectManager::GetEffectsMapping()
{
    std::lock_guard<std::mutex> guard(lock);
    return effect_zones;
}

void EffectManager::EffectThreadFunction(RGBEffect* effect)
{
    printf(
        "[OpenRGBEffectsPlugin] Effect %s thread started\n",
        effect->EffectDetails.EffectName.c_str());

    const TCount effect_start = clock->now();
    int last_total_duration = -1;

    while(true)
    {
        const TCount start = clock->now();
        int fps = 1;
        bool emit_measure = false;
        int duration_us = 0;
        int total_duration = 0;

        {
            std::unique_lock<std::mutex> state_guard(lock);
            if(EffectThreads.find(effect) == EffectThreads.end())
            {
                break;
            }

            std::vector<ControllerZone*> controller_zones =
                effect_zones[effect];
            const auto preview = previews.find(effect);
            if(preview != previews.end())
            {
                controller_zones.push_back(preview->second);
            }

            std::lock_guard<std::mutex> frame_guard(
                lighttrack::openrgb::ControllerFrameMutex());
            lighttrack::openrgb::SetCurrentRenderingEffect(effect);
            effect->StepEffect(controller_zones);

            std::set<RGBControllerInterface*> controllers;
            for(ControllerZone* controller_zone : controller_zones)
            {
                if(controller_zone != nullptr
                    && controller_zone->controller != nullptr)
                {
                    controllers.insert(controller_zone->controller);
                }
            }
            for(RGBControllerInterface* controller : controllers)
            {
                controller->UpdateLEDs();
            }
            lighttrack::openrgb::SetCurrentRenderingEffect(nullptr);

            const TCount end = clock->now();
            fps = static_cast<int>(std::max(1U, effect->GetFPS()));
            duration_us = static_cast<int>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    end - start).count());
            total_duration = static_cast<int>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    end - effect_start).count());
            if(total_duration > last_total_duration)
            {
                last_total_duration = total_duration;
                emit_measure = true;
            }
        }

        if(emit_measure)
        {
            effect->EmitMeasure(
                duration_us * 0.001,
                total_duration);
        }

        const int frame_delay_us =
            static_cast<int>(1000000.0 / fps);
        const int remaining_us = frame_delay_us - duration_us;
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::microseconds(std::max(1000, remaining_us));

        // Poll at 1 ms so SetEffectUnActive can join promptly without the
        // unsynchronised map read used by the upstream implementation.
        while(std::chrono::steady_clock::now() < deadline)
        {
            const auto remaining = deadline - std::chrono::steady_clock::now();
            std::this_thread::sleep_for(
                std::min(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        remaining),
                    std::chrono::microseconds(1000)));

            std::lock_guard<std::mutex> guard(lock);
            if(EffectThreads.find(effect) == EffectThreads.end())
            {
                printf(
                    "[OpenRGBEffectsPlugin] Effect %s thread ended\n",
                    effect->EffectDetails.EffectName.c_str());
                return;
            }
        }
    }

    printf(
        "[OpenRGBEffectsPlugin] Effect %s thread ended\n",
        effect->EffectDetails.EffectName.c_str());
}

bool EffectManager::HasActiveEffects()
{
    std::lock_guard<std::mutex> guard(lock);
    return !ActiveEffects.empty();
}

void EffectManager::AddPreview(
    RGBEffect* effect,
    ControllerZone* preview)
{
    std::lock_guard<std::mutex> guard(lock);
    previews[effect] = preview;
    NotifySelectionChanged(effect);
}

void EffectManager::RemovePreview(RGBEffect* effect)
{
    std::lock_guard<std::mutex> guard(lock);
    previews.erase(effect);
    NotifySelectionChanged(effect);
}

void EffectManager::NotifySelectionChanged(RGBEffect* effect)
{
    std::vector<ControllerZone*> new_zones = effect_zones[effect];
    const auto preview = previews.find(effect);
    if(preview != previews.end())
    {
        new_zones.push_back(preview->second);
    }
    effect->OnControllerZonesListChanged(new_zones);
}
