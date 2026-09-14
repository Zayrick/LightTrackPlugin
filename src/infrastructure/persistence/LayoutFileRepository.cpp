#include "infrastructure/persistence/LayoutFileRepository.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <nlohmann/json.hpp>

#include <exception>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
using json = nlohmann::json;
using lighttrack::ClipId;
using lighttrack::LayoutClipSnapshot;
using lighttrack::LayoutLaneSnapshot;
using lighttrack::LayoutSnapshot;

constexpr int LIGHTTRACK_LAYOUT_VERSION = 1;
constexpr const char* LIGHTTRACK_LAYOUT_FORMAT =
    "OpenRGB LightTrack Layout";

std::string ToUtf8(const QString& value)
{
    const QByteArray utf8 = value.toUtf8();
    return std::string(
        utf8.constData(),
        static_cast<std::size_t>(utf8.size()));
}

QString FromUtf8(const std::string& value)
{
    return QString::fromUtf8(
        value.data(),
        static_cast<int>(value.size()));
}

QByteArray SerializeOpaqueJson(const json& value)
{
    return QByteArray::fromStdString(value.dump());
}

json ParseOpaqueJson(
    const QByteArray& serialized,
    const char* description)
{
    try
    {
        return json::parse(
            serialized.constData(),
            serialized.constData() + serialized.size());
    }
    catch(const std::exception&)
    {
        throw std::runtime_error(description);
    }
}

ClipId ParseClipId(
    const json& clip,
    int clip_number,
    std::set<quint64>& used_ids)
{
    if(!clip.contains("clipId"))
    {
        // Compatibility with early version-1 files.
        return {};
    }

    const json& serialized_id = clip["clipId"];
    quint64 id_value = 0;
    if(serialized_id.is_number_unsigned())
    {
        id_value = serialized_id.get<quint64>();
    }
    else if(serialized_id.is_number_integer())
    {
        const qint64 signed_id = serialized_id.get<qint64>();
        if(signed_id > 0)
        {
            id_value = static_cast<quint64>(signed_id);
        }
    }

    if(id_value == 0
        || !used_ids.insert(id_value).second)
    {
        throw std::runtime_error(
            ToUtf8(
                QString(
                    "Timeline clip %1 has an invalid or duplicate clipId")
                    .arg(clip_number)));
    }

    return ClipId(id_value);
}

LayoutSnapshot DecodeLayout(
    const json& layout,
    const QString& source_path)
{
    if(!layout.is_object()
        || !layout.contains("format")
        || !layout["format"].is_string()
        || layout["format"].get<std::string>()
            != LIGHTTRACK_LAYOUT_FORMAT)
    {
        throw std::runtime_error(
            "This is not a LightTrack layout file");
    }

    if(!layout.contains("version")
        || !layout["version"].is_number_integer())
    {
        throw std::runtime_error(
            "The layout version is missing");
    }

    const int version = layout["version"].get<int>();
    if(version != LIGHTTRACK_LAYOUT_VERSION)
    {
        throw std::runtime_error(
            "This LightTrack layout version is not supported");
    }

    LayoutSnapshot snapshot;
    if(layout.contains("music"))
    {
        if(!layout["music"].is_object())
        {
            throw std::runtime_error("Invalid music settings");
        }

        const json& music = layout["music"];
        if(music.contains("path"))
        {
            if(!music["path"].is_string())
            {
                throw std::runtime_error(
                    "Invalid music file path");
            }
            snapshot.music.path =
                FromUtf8(music["path"].get<std::string>());
        }
        if(music.contains("durationMs"))
        {
            if(!music["durationMs"].is_number_integer())
            {
                throw std::runtime_error(
                    "Invalid music duration");
            }
            snapshot.music.duration_ms = qMax<qint64>(
                0,
                music["durationMs"].get<qint64>());
        }
    }

    if(!snapshot.music.path.isEmpty()
        && QFileInfo(snapshot.music.path).isRelative())
    {
        snapshot.music.path = QFileInfo(
            QFileInfo(source_path).dir(),
            snapshot.music.path).absoluteFilePath();
    }

    // Lane settings are optional for compatibility with earlier version-1 files.
    if(layout.contains("lanes"))
    {
        if(!layout["lanes"].is_array())
        {
            throw std::runtime_error("Invalid lane settings list");
        }
        snapshot.lanes.reserve(static_cast<int>(layout["lanes"].size()));
        int lane_number = 0;
        for(const json& entry : layout["lanes"])
        {
            ++lane_number;
            if(!entry.is_object()
                || !entry.contains("lane")
                || !entry["lane"].is_object()
                || !entry.contains("name")
                || !entry["name"].is_string()
                || !entry.contains("highlighted")
                || !entry["highlighted"].is_boolean()
                || !entry.contains("disabled")
                || !entry["disabled"].is_boolean())
            {
                throw std::runtime_error(ToUtf8(
                    QString("Lane settings %1 are invalid").arg(lane_number)));
            }

            snapshot.lanes.push_back({
                SerializeOpaqueJson(entry["lane"]),
                FromUtf8(entry["name"].get<std::string>()),
                entry["highlighted"].get<bool>(),
                entry["disabled"].get<bool>()
            });
        }
    }

    if(!layout.contains("clips")
        || !layout["clips"].is_array())
    {
        throw std::runtime_error(
            "Invalid timeline clip list");
    }

    snapshot.clips.reserve(
        static_cast<int>(layout["clips"].size()));
    std::set<quint64> used_ids;
    int clip_number = 0;
    for(const json& clip : layout["clips"])
    {
        ++clip_number;
        if(!clip.is_object()
            || !clip.contains("effectId")
            || !clip["effectId"].is_string()
            || !clip.contains("lane")
            || !clip["lane"].is_object()
            || !clip.contains("startMs")
            || !clip["startMs"].is_number_integer()
            || !clip.contains("endMs")
            || !clip["endMs"].is_number_integer()
            || !clip.contains("settings")
            || !clip["settings"].is_object())
        {
            throw std::runtime_error(
                ToUtf8(
                    QString("Timeline clip %1 is invalid")
                        .arg(clip_number)));
        }

        snapshot.clips.push_back({
            ParseClipId(clip, clip_number, used_ids),
            FromUtf8(clip["effectId"].get<std::string>()),
            SerializeOpaqueJson(clip["lane"]),
            clip["startMs"].get<qint64>(),
            clip["endMs"].get<qint64>(),
            SerializeOpaqueJson(clip["settings"])
        });
    }

    return snapshot;
}

json EncodeLayout(const LayoutSnapshot& snapshot)
{
    json layout;
    layout["format"] = LIGHTTRACK_LAYOUT_FORMAT;
    layout["version"] = LIGHTTRACK_LAYOUT_VERSION;
    layout["music"] = {
        {"path", ToUtf8(snapshot.music.path)},
        {"durationMs", snapshot.music.duration_ms}
    };
    layout["clips"] = json::array();
    layout["lanes"] = json::array();
    for(const LayoutLaneSnapshot& entry : snapshot.lanes)
    {
        const json lane = ParseOpaqueJson(
            entry.serialized_lane,
            "Invalid serialized lane settings target");
        if(!lane.is_object())
        {
            throw std::runtime_error("Invalid serialized lane settings target");
        }

        layout["lanes"].push_back({
            {"lane", lane},
            {"name", ToUtf8(entry.name)},
            {"highlighted", entry.highlighted},
            {"disabled", entry.disabled}
        });
    }

    std::set<quint64> used_ids;
    int clip_number = 0;
    for(const LayoutClipSnapshot& clip : snapshot.clips)
    {
        ++clip_number;
        if(!clip.id.IsValid()
            || !used_ids.insert(clip.id.Value()).second)
        {
            throw std::runtime_error(
                ToUtf8(
                    QString(
                        "Timeline clip %1 has an invalid or duplicate clipId")
                        .arg(clip_number)));
        }
        if(clip.start_ms < 0 || clip.end_ms <= clip.start_ms)
        {
            throw std::runtime_error(
                ToUtf8(
                    QString(
                        "Timeline clip %1 has an invalid time range")
                        .arg(clip_number)));
        }

        const json lane = ParseOpaqueJson(
            clip.serialized_lane,
            "Invalid serialized timeline lane");
        const json settings = ParseOpaqueJson(
            clip.serialized_effect_settings,
            "Invalid serialized effect settings");
        if(!lane.is_object())
        {
            throw std::runtime_error(
                "Invalid serialized timeline lane");
        }
        if(!settings.is_object())
        {
            throw std::runtime_error(
                "Invalid serialized effect settings");
        }

        json serialized_clip;
        serialized_clip["clipId"] = clip.id.Value();
        serialized_clip["effectId"] = ToUtf8(clip.effect_id);
        serialized_clip["lane"] = lane;
        serialized_clip["startMs"] = clip.start_ms;
        serialized_clip["endMs"] = clip.end_ms;
        serialized_clip["settings"] = settings;
        layout["clips"].push_back(std::move(serialized_clip));
    }

    return layout;
}
}

namespace lighttrack::persistence
{
bool LayoutFileRepository::Save(
    const QString& path,
    const LayoutSnapshot& snapshot,
    QString& error) const
{
    QByteArray contents;
    try
    {
        contents = QByteArray::fromStdString(
            EncodeLayout(snapshot).dump(2));
    }
    catch(const std::exception& exception)
    {
        error = QString(
            "Could not serialize the layout:\n%1")
            .arg(QString::fromUtf8(exception.what()));
        return false;
    }

    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        error = QString(
            "Could not open the file for writing:\n%1")
            .arg(file.errorString());
        return false;
    }

    if(file.write(contents) != contents.size()
        || !file.commit())
    {
        error = QString(
            "Could not write the layout file:\n%1")
            .arg(file.errorString());
        return false;
    }

    return true;
}

bool LayoutFileRepository::Load(
    const QString& path,
    LayoutSnapshot& snapshot,
    QString& error) const
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        error = QString(
            "Could not open the layout file:\n%1")
            .arg(file.errorString());
        return false;
    }

    try
    {
        const QByteArray contents = file.readAll();
        const json layout = json::parse(
            contents.constData(),
            contents.constData() + contents.size());
        LayoutSnapshot loaded = DecodeLayout(layout, path);
        snapshot = std::move(loaded);
        return true;
    }
    catch(const std::exception& exception)
    {
        error = QString(
            "The layout file is invalid:\n%1")
            .arg(QString::fromUtf8(exception.what()));
    }

    return false;
}
}
