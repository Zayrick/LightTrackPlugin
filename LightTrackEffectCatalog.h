#pragma once

#include <QColor>
#include <QString>
#include <QVector>

struct LightTrackEffectInfo
{
    QString id;
    QString name;
    QString category;
    QColor color;
};

struct LightTrackEffectGroup
{
    QString name;
    QVector<LightTrackEffectInfo> effects;
};

QVector<LightTrackEffectGroup> LoadOpenRGBEffectsCatalog();
bool FindOpenRGBEffect(const QString& id, LightTrackEffectInfo* effect);
