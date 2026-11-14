#pragma once

#include "core/LayoutSnapshot.h"

#include <QString>

namespace lighttrack::persistence
{
class LayoutFileRepository
{
public:
    bool Save(
        const QString& path,
        const LayoutSnapshot& snapshot,
        QString& error) const;
    bool Load(
        const QString& path,
        LayoutSnapshot& snapshot,
        QString& error) const;
};
}
