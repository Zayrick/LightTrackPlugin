#include "infrastructure/persistence/LayoutFileRepository.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include <iostream>

namespace
{
bool WriteFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }

    return file.write(contents) == contents.size()
        && file.flush();
}

QByteArray MakeClip(
    const QByteArray& serialized_id,
    const QByteArray& lane = QByteArrayLiteral(
        "{\"fallbackIndex\":0}"),
    const QByteArray& settings = QByteArrayLiteral(
        "{\"Speed\":7}"))
{
    return QByteArrayLiteral("{\"clipId\":")
        + serialized_id
        + QByteArrayLiteral(
            ",\"effectId\":\"test.effect\",\"lane\":")
        + lane
        + QByteArrayLiteral(
            ",\"startMs\":0,\"endMs\":1000,\"settings\":")
        + settings
        + QByteArrayLiteral("}");
}

QByteArray MakeLayout(const QByteArray& clips)
{
    return QByteArrayLiteral(
        "{\"format\":\"OpenRGB LightTrack Layout\","
        "\"version\":1,\"music\":{},\"clips\":")
        + clips
        + QByteArrayLiteral("}");
}

lighttrack::LayoutSnapshot SentinelSnapshot()
{
    lighttrack::LayoutSnapshot snapshot;
    snapshot.music.path = QStringLiteral("sentinel-music.wav");
    snapshot.music.duration_ms = 4242;
    snapshot.clips.push_back({
        lighttrack::ClipId(999),
        QStringLiteral("sentinel.effect"),
        QByteArrayLiteral("{\"fallbackIndex\":9}"),
        100,
        200,
        QByteArrayLiteral("{\"Speed\":99}")
    });
    return snapshot;
}
}

int main()
{
#define CHECK(expression)                                                        \
    do                                                                           \
    {                                                                            \
        if(!(expression))                                                        \
        {                                                                        \
            std::cerr << "Check failed at line " << __LINE__                    \
                      << ": " #expression << '\n';                              \
            return 1;                                                            \
        }                                                                        \
    } while(false)

    QTemporaryDir temporary_directory;
    CHECK(temporary_directory.isValid());

    lighttrack::persistence::LayoutFileRepository repository;

    lighttrack::LayoutSnapshot original;
    original.music.path =
        QDir(temporary_directory.path()).filePath(
            QStringLiteral("music.wav"));
    original.music.duration_ms = 123456;
    original.clips.push_back({
        lighttrack::ClipId(41),
        QStringLiteral("effect.one"),
        QByteArrayLiteral(
            "{\"fallbackIndex\":0,\"name\":\"Lane A\"}"),
        250,
        1750,
        QByteArrayLiteral(
            "{\"Brightness\":80,\"Enabled\":true}")
    });
    original.clips.push_back({
        lighttrack::ClipId(73),
        QStringLiteral("effect.two"),
        QByteArrayLiteral(
            "{\"fallbackIndex\":2,\"level\":1}"),
        1800,
        9200,
        QByteArrayLiteral(
            "{\"CustomName\":\"Evening\",\"Speed\":42}")
    });

    const QString roundtrip_path =
        QDir(temporary_directory.path()).filePath(
            QStringLiteral("roundtrip.lighttrack"));
    QString error;
    CHECK(repository.Save(roundtrip_path, original, &error));

    QFile saved_file(roundtrip_path);
    CHECK(saved_file.open(QIODevice::ReadOnly));
    const QByteArray saved_contents = saved_file.readAll();
    CHECK(saved_file.error() == QFile::NoError);
    CHECK(!saved_contents.isEmpty());
    CHECK(saved_contents.contains(
        QByteArrayLiteral(
            "\"format\": \"OpenRGB LightTrack Layout\"")));
    CHECK(saved_contents.contains(
        QByteArrayLiteral("\"clipId\": 41")));
    saved_file.close();

    lighttrack::LayoutSnapshot roundtrip;
    CHECK(repository.Load(roundtrip_path, &roundtrip, &error));
    CHECK(roundtrip == original);

    const QString legacy_path =
        QDir(temporary_directory.path()).filePath(
            QStringLiteral("legacy-v1.lighttrack"));
    const QByteArray legacy_contents = QByteArrayLiteral(
        "{"
        "\"format\":\"OpenRGB LightTrack Layout\","
        "\"version\":1,"
        "\"music\":{"
            "\"path\":\"audio/song.ogg\","
            "\"durationMs\":9000"
        "},"
        "\"clips\":[{"
            "\"effectId\":\"legacy.effect\","
            "\"lane\":{\"fallbackIndex\":0},"
            "\"startMs\":500,"
            "\"endMs\":2500,"
            "\"settings\":{\"Speed\":7}"
        "}]"
        "}");
    CHECK(WriteFile(legacy_path, legacy_contents));

    lighttrack::LayoutSnapshot legacy;
    CHECK(repository.Load(legacy_path, &legacy, &error));
    CHECK(legacy.clips.size() == 1);
    CHECK(!legacy.clips[0].id.IsValid());
    CHECK(legacy.clips[0].effect_id
        == QStringLiteral("legacy.effect"));
    CHECK(legacy.clips[0].serialized_lane
        == QByteArrayLiteral("{\"fallbackIndex\":0}"));
    CHECK(legacy.clips[0].serialized_effect_settings
        == QByteArrayLiteral("{\"Speed\":7}"));
    CHECK(legacy.music.duration_ms == 9000);

    const QString expected_music_path =
        QFileInfo(
            QFileInfo(legacy_path).dir(),
            QStringLiteral("audio/song.ogg"))
            .absoluteFilePath();
    CHECK(legacy.music.path == expected_music_path);
    CHECK(QFileInfo(legacy.music.path).isAbsolute());

    const QByteArray duplicate_id_layout =
        MakeLayout(
            QByteArrayLiteral("[")
            + MakeClip(QByteArrayLiteral("7"))
            + QByteArrayLiteral(",")
            + MakeClip(QByteArrayLiteral("7"))
            + QByteArrayLiteral("]"));

    const QVector<QByteArray> invalid_layouts = {
        duplicate_id_layout,
        MakeLayout(
            QByteArrayLiteral("[")
            + MakeClip(QByteArrayLiteral("0"))
            + QByteArrayLiteral("]")),
        MakeLayout(
            QByteArrayLiteral("[")
            + MakeClip(QByteArrayLiteral("-1"))
            + QByteArrayLiteral("]")),
        MakeLayout(
            QByteArrayLiteral("[")
            + MakeClip(
                QByteArrayLiteral("8"),
                QByteArrayLiteral("[]"))
            + QByteArrayLiteral("]")),
        MakeLayout(
            QByteArrayLiteral("[")
            + MakeClip(
                QByteArrayLiteral("9"),
                QByteArrayLiteral("{\"fallbackIndex\":0}"),
                QByteArrayLiteral("[]"))
            + QByteArrayLiteral("]"))
    };

    const lighttrack::LayoutSnapshot sentinel =
        SentinelSnapshot();
    for(int index = 0; index < invalid_layouts.size(); ++index)
    {
        const QString invalid_path =
            QDir(temporary_directory.path()).filePath(
                QStringLiteral("invalid-%1.lighttrack")
                    .arg(index));
        CHECK(WriteFile(invalid_path, invalid_layouts[index]));

        lighttrack::LayoutSnapshot output = sentinel;
        error.clear();
        CHECK(!repository.Load(invalid_path, &output, &error));
        CHECK(output == sentinel);
        CHECK(!error.isEmpty());
    }

    lighttrack::LayoutSnapshot missing_file_output = sentinel;
    error.clear();
    CHECK(!repository.Load(
        QDir(temporary_directory.path()).filePath(
            QStringLiteral("does-not-exist.lighttrack")),
        &missing_file_output,
        &error));
    CHECK(missing_file_output == sentinel);
    CHECK(!error.isEmpty());

#undef CHECK
    return 0;
}
