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
}

QVector<LightTrackEffectGroup> LoadOpenRGBEffectsCatalog()
{
    QVector<LightTrackEffectGroup> groups;
    const auto categories = EffectListManager::get()->GetCategorizedEffects();

    for(const auto& category : categories)
    {
        LightTrackEffectGroup group{QString::fromStdString(category.first), {}};

        for(const effect_names& effect : category.second)
        {
            const QString id = QString::fromStdString(effect.classname);
            group.effects.push_back({id, QString::fromStdString(effect.ui_name), group.name, ColorForEffect(id)});
        }

        std::sort(group.effects.begin(), group.effects.end(), [](const LightTrackEffectInfo& a, const LightTrackEffectInfo& b)
        {
            return a.name < b.name;
        });

        groups.push_back(group);
    }

    std::sort(groups.begin(), groups.end(), [](const LightTrackEffectGroup& a, const LightTrackEffectGroup& b)
    {
        return a.name < b.name;
    });

    return groups;
}

bool FindOpenRGBEffect(const QString& id, LightTrackEffectInfo* effect)
{
    for(const LightTrackEffectGroup& group : LoadOpenRGBEffectsCatalog())
    {
        for(const LightTrackEffectInfo& candidate : group.effects)
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
    }

    return false;
}
