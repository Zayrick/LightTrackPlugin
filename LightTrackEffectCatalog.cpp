#include "LightTrackEffectCatalog.h"

#include "EffectListManager.h"

#include <algorithm>

namespace
{
QColor ColorForEffect(const QString& id)
{
    const uint hash = qHash(id);
    return QColor::fromHsv(static_cast<int>(hash % 360), 148 + static_cast<int>((hash >> 8) % 58), 168 + static_cast<int>((hash >> 16) % 56));
}

QVector<LightTrackEffectInfo> LoadEffects()
{
    QVector<LightTrackEffectInfo> effects;
    const auto categories = EffectListManager::get()->GetCategorizedEffects();

    for(const auto& category : categories)
    {
        for(const effect_names& effect : category.second)
        {
            const QString id = QString::fromStdString(effect.classname);
            effects.push_back({id, QString::fromStdString(effect.ui_name), QString::fromStdString(category.first), ColorForEffect(id)});
        }
    }

    std::sort(effects.begin(), effects.end(), [](const LightTrackEffectInfo& a, const LightTrackEffectInfo& b)
    {
        if(a.category != b.category)
        {
            return a.category < b.category;
        }
        return a.name < b.name;
    });

    return effects;
}
}

QVector<LightTrackEffectGroup> LoadOpenRGBEffectsCatalog()
{
    QVector<LightTrackEffectGroup> groups;

    for(const LightTrackEffectInfo& effect : LoadEffects())
    {
        auto group = std::find_if(groups.begin(), groups.end(), [&effect](const LightTrackEffectGroup& candidate)
        {
            return candidate.name == effect.category;
        });

        if(group == groups.end())
        {
            groups.push_back({effect.category, {}});
            group = groups.end() - 1;
        }

        group->effects.push_back(effect);
    }

    return groups;
}

bool FindOpenRGBEffect(const QString& id, LightTrackEffectInfo* effect)
{
    for(const LightTrackEffectInfo& candidate : LoadEffects())
    {
        if(candidate.id == id)
        {
            if(effect != nullptr)
            {
                *effect = candidate;
            }
            return true;
        }
    }

    return false;
}
