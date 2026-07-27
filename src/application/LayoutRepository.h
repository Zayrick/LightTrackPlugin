#pragma once

#include "core/LayoutSnapshot.h"

#include <QString>

namespace lighttrack
{
class LayoutRepository
{
public:
    virtual ~LayoutRepository() = default;

    virtual bool Save(
        const QString& path,
        const LayoutSnapshot& snapshot,
        QString* error = nullptr) const = 0;
    virtual bool Load(
        const QString& path,
        LayoutSnapshot* snapshot,
        QString* error = nullptr) const = 0;
};
}
