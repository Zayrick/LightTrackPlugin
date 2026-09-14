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
        "{\"fallbackIndex\":0}"))
{
    return QByteArrayLiteral("{\"clipId\":")
        + serialized_id
        + QByteArrayLiteral(
            ",\"effectId\":\"test.effect\",\"lane\":")
        + lane
        + QByteArrayLiteral(
            ",\"startMs\":0,\"endMs\":1000,"
            "\"settings\":{\"Speed\":7}}");
}

QByteArray MakeLayout(
    const QByteArray& clips,
    const QByteArray& lanes = {})
{
    return QByteArrayLiteral(
        "{\"format\":\"OpenRGB LightTrack Layout\","
        "\"version\":1,\"music\":{},\"clips\":")
        + clips
        + (lanes.isEmpty() ? QByteArray()
            : QByteArrayLiteral(",\"lanes\":") + lanes)
        + QByteArrayLiteral("}");
}

lighttrack::LayoutSnapshot SentinelSnapshot()
{
    lighttrack::LayoutSnapshot snapshot;
    snapshot.music.path = QStringLiteral("sentinel-music.wav");
    snapshot.music.duration_ms = 4242;
    snapshot.lanes.push_back({
        QByteArrayLiteral("{\"fallbackIndex\":9}"),
        QStringLiteral("Sentinel lane"),
        true,
        true
    });
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
    original.lanes = {
        {QByteArrayLiteral("{\"fallbackIndex\":0}"),
            QString::fromUtf8(u8"Desk \u706f"), true, false},
        {QByteArrayLiteral("{\"fallbackIndex\":1}"),
            QStringLiteral("Disabled zone without clips"), false, true},
        {QByteArrayLiteral("{\"fallbackIndex\":2}"),
            QStringLiteral("Segment"), true, true},
        {QByteArrayLiteral("{\"fallbackIndex\":3}"),
            QStringLiteral("Default zone"), false, false}
    };
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
    CHECK(repository.Save(roundtrip_path, original, error));

    lighttrack::LayoutSnapshot roundtrip;
    CHECK(repository.Load(roundtrip_path, roundtrip, error));
    CHECK(roundtrip == original);

    lighttrack::LayoutSnapshot lanes_only;
    lanes_only.lanes = original.lanes;
    CHECK(repository.Save(roundtrip_path, lanes_only, error));
    CHECK(repository.Load(roundtrip_path, roundtrip, error));
    CHECK(roundtrip == lanes_only);

    lighttrack::LayoutSnapshot renamed = lanes_only;
    renamed.lanes[0].name = QStringLiteral("Renamed");
    CHECK(!(renamed == lanes_only));
    lighttrack::LayoutSnapshot highlighted = lanes_only;
    highlighted.lanes[1].highlighted = true;
    CHECK(!(highlighted == lanes_only));
    lighttrack::LayoutSnapshot disabled = lanes_only;
    disabled.lanes[1].disabled = false;
    CHECK(!(disabled == lanes_only));

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
    CHECK(repository.Load(legacy_path, legacy, error));
    CHECK(legacy.clips.size() == 1);
    CHECK(legacy.lanes.isEmpty());
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
            + MakeClip(
                QByteArrayLiteral("8"),
                QByteArrayLiteral("[]"))
            + QByteArrayLiteral("]")),
        MakeLayout(QByteArrayLiteral("[]"), QByteArrayLiteral("{}")),
        MakeLayout(QByteArrayLiteral("[]"), QByteArrayLiteral("[null]")),
        MakeLayout(QByteArrayLiteral("[]"), QByteArrayLiteral(
            "[{\"lane\":[],\"name\":\"Zone\",\"highlighted\":true,\"disabled\":false}]")),
        MakeLayout(QByteArrayLiteral("[]"), QByteArrayLiteral(
            "[{\"lane\":{},\"name\":12,\"highlighted\":true,\"disabled\":false}]")),
        MakeLayout(QByteArrayLiteral("[]"), QByteArrayLiteral(
            "[{\"lane\":{},\"name\":\"Zone\",\"highlighted\":\"true\",\"disabled\":false}]")),
        MakeLayout(QByteArrayLiteral("[]"), QByteArrayLiteral(
            "[{\"lane\":{},\"name\":\"Zone\",\"highlighted\":true,\"disabled\":1}]")),
        MakeLayout(QByteArrayLiteral("[]"), QByteArrayLiteral(
            "[{\"lane\":{},\"name\":\"Zone\",\"highlighted\":true}]"))
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
        CHECK(!repository.Load(invalid_path, output, error));
        CHECK(output == sentinel);
        CHECK(!error.isEmpty());
    }

    lighttrack::LayoutSnapshot missing_file_output = sentinel;
    error.clear();
    CHECK(!repository.Load(
        QDir(temporary_directory.path()).filePath(
            QStringLiteral("does-not-exist.lighttrack")),
        missing_file_output,
        error));
    CHECK(missing_file_output == sentinel);
    CHECK(!error.isEmpty());

    // A bad target must not replace an existing valid file.
    lighttrack::LayoutSnapshot invalid_target = lanes_only;
    invalid_target.lanes[0].serialized_lane = QByteArrayLiteral("[]");
    CHECK(!repository.Save(roundtrip_path, invalid_target, error));
    CHECK(repository.Load(roundtrip_path, roundtrip, error));
    CHECK(roundtrip == lanes_only);

#undef CHECK
    return 0;
}
