#pragma once

#include <QString>
#include <QVector>

#include "core/effects/EffectDescriptor.h"

namespace lighttrack::openrgb
{
QVector<EffectGroup> LoadEffectCatalog();
bool FindEffect(const QString& id, EffectDescriptor* effect);
}
