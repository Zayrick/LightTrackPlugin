#pragma once

#include "application/LayoutRepository.h"

#include <memory>

namespace lighttrack::persistence
{
class LayoutFileRepository final : public LayoutRepository
{
public:
    bool Save(
        const QString& path,
        const LayoutSnapshot& snapshot,
        QString* error = nullptr) const override;
    bool Load(
        const QString& path,
        LayoutSnapshot* snapshot,
        QString* error = nullptr) const override;
};

std::unique_ptr<LayoutRepository> CreateLayoutFileRepository();
}
