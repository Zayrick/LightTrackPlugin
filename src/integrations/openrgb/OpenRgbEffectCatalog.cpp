#include "integrations/openrgb/OpenRgbEffectCatalog.h"

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

namespace lighttrack::openrgb
{
QVector<EffectGroup> LoadEffectCatalog()
{
    QVector<EffectGroup> groups;
    const auto categories = EffectListManager::get()->GetCategorizedEffects();

    for(const auto& category : categories)
    {
        EffectGroup group{QString::fromStdString(category.first), {}};

        for(const effect_names& effect : category.second)
        {
            const QString id = QString::fromStdString(effect.classname);
            group.effects.push_back({id, QString::fromStdString(effect.ui_name), group.name, ColorForEffect(id)});
        }

        std::sort(group.effects.begin(), group.effects.end(), [](const EffectDescriptor& a, const EffectDescriptor& b)
        {
            return a.name < b.name;
        });

        groups.push_back(group);
    }

    std::sort(groups.begin(), groups.end(), [](const EffectGroup& a, const EffectGroup& b)
    {
        return a.name < b.name;
    });

    return groups;
}

bool FindEffect(const QString& id, EffectDescriptor* effect)
{
    for(const EffectGroup& group : LoadEffectCatalog())
    {
        for(const EffectDescriptor& candidate : group.effects)
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
}
