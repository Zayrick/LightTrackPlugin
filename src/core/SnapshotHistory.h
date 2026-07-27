#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace lighttrack
{
template<typename Snapshot>
class SnapshotHistory
{
public:
    explicit SnapshotHistory(std::size_t limit = 100) :
        limit(limit)
    {
    }

    void Reset(Snapshot initial)
    {
        current = std::move(initial);
        undo.clear();
        redo.clear();
    }

    void Clear()
    {
        current.reset();
        undo.clear();
        redo.clear();
    }

    bool IsInitialized() const
    {
        return current.has_value();
    }

    const Snapshot& Current() const
    {
        return current.value();
    }

    void ReplaceCurrent(Snapshot snapshot)
    {
        current = std::move(snapshot);
    }

    bool Commit(Snapshot next)
    {
        if(!current.has_value())
        {
            Reset(std::move(next));
            return true;
        }

        if(*current == next)
        {
            return false;
        }

        Push(undo, std::move(*current));
        current = std::move(next);
        redo.clear();
        return true;
    }

    bool CanUndo() const
    {
        return !undo.empty();
    }

    bool CanRedo() const
    {
        return !redo.empty();
    }

    const Snapshot* UndoTarget() const
    {
        return CanUndo() ? &undo.back() : nullptr;
    }

    const Snapshot* RedoTarget() const
    {
        return CanRedo() ? &redo.back() : nullptr;
    }

    void AcceptUndo()
    {
        if(!current.has_value() || undo.empty())
        {
            return;
        }

        Push(redo, std::move(*current));
        current = std::move(undo.back());
        undo.pop_back();
    }

    void AcceptRedo()
    {
        if(!current.has_value() || redo.empty())
        {
            return;
        }

        Push(undo, std::move(*current));
        current = std::move(redo.back());
        redo.pop_back();
    }

private:
    void Push(std::vector<Snapshot>& destination, Snapshot snapshot)
    {
        if(limit == 0)
        {
            return;
        }

        if(destination.size() >= limit)
        {
            destination.erase(destination.begin());
        }
        destination.push_back(std::move(snapshot));
    }

    std::size_t limit;
    std::optional<Snapshot> current;
    std::vector<Snapshot> undo;
    std::vector<Snapshot> redo;
};
}
