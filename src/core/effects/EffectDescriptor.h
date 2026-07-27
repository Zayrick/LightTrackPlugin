#pragma once

#include <QColor>
#include <QString>
#include <QVector>

namespace lighttrack
{
struct EffectDescriptor
{
    QString id;
    QString name;
    QString category;
    QColor color;
};

struct EffectGroup
{
    QString name;
    QVector<EffectDescriptor> effects;
};
}
