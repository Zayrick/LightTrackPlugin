#pragma once

#include "core/TimelineTypes.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace lighttrack
{
struct LayoutMusicSnapshot
{
    QString path;
    qint64 duration_ms = 0;

    friend bool operator==(
        const LayoutMusicSnapshot& left,
        const LayoutMusicSnapshot& right)
    {
        return left.path == right.path
            && left.duration_ms == right.duration_ms;
    }
};

struct LayoutClipSnapshot
{
    // Invalid means that this clip came from a legacy version-1 file that
    // predates persisted clip IDs. New snapshots always use a valid ID.
    ClipId id;
    QString effect_id;
    QByteArray serialized_lane;
    qint64 start_ms = 0;
    qint64 end_ms = 0;
    QByteArray serialized_effect_settings;

    friend bool operator==(
        const LayoutClipSnapshot& left,
        const LayoutClipSnapshot& right)
    {
        return left.id == right.id
            && left.effect_id == right.effect_id
            && left.serialized_lane == right.serialized_lane
            && left.start_ms == right.start_ms
            && left.end_ms == right.end_ms
            && left.serialized_effect_settings
                == right.serialized_effect_settings;
    }
};

struct LayoutSnapshot
{
    LayoutMusicSnapshot music;
    QVector<LayoutClipSnapshot> clips;

    friend bool operator==(
        const LayoutSnapshot& left,
        const LayoutSnapshot& right)
    {
        if(!(left.music == right.music)
            || left.clips.size() != right.clips.size())
        {
            return false;
        }

        for(int index = 0; index < left.clips.size(); ++index)
        {
            if(!(left.clips[index] == right.clips[index]))
            {
                return false;
            }
        }
        return true;
    }
};
}
