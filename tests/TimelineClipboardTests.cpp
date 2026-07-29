#include "ui/timeline/internal/TimelineEditorInternal.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

#include <iostream>
#include <utility>

int main(int argc, char** argv)
{
    QApplication application(argc, argv);

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

    using namespace lighttrack;
    using namespace lighttrack::timeline_internal;

    QGuiApplication::clipboard()->clear();

    LightTrackScene scene;
    scene.SetViewportSize(QSize(800, 144));
    scene.SetEffects({
        {
            QStringLiteral("Basic"),
            {
                {
                    QStringLiteral("rainbow"),
                    QStringLiteral("Rainbow"),
                    QStringLiteral("Basic"),
                    QColor(20, 120, 220)
                }
            }
        }
    });
    scene.SetLanes({
        {
            QStringLiteral("lane-1"),
            QStringLiteral("Lane 1"),
            0,
            false,
            false,
            10
        }
    }, {});

    int timeline_change_count = 0;
    scene.SetTimelineChangedCallback(
        [&timeline_change_count]()
        {
            ++timeline_change_count;
        });

    QVector<std::pair<ClipId, ClipId>> duplicated_ids;
    scene.SetClipDuplicatedCallback(
        [&duplicated_ids](ClipId source, ClipId duplicated)
        {
            duplicated_ids.push_back({source, duplicated});
        });

    CHECK(scene.AddEffectAt(
        QStringLiteral("rainbow"),
        QPointF(220.0, ROW_HEIGHT + ROW_HEIGHT / 2.0)));
    CHECK(timeline_change_count == 1);

    const QVector<TimelineClip> original_clips =
        scene.PersistentTimelineClips();
    CHECK(original_clips.size() == 1);
    CHECK(scene.HasSelectedClips());
    CHECK(scene.CopySelectedClips());
    CHECK(scene.CanPasteCopiedClips());
    CHECK(
        QGuiApplication::clipboard()
            ->mimeData()
            ->hasFormat(CLIP_MIME));
    CHECK(timeline_change_count == 1);

    CHECK(scene.PasteCopiedClips());
    CHECK(timeline_change_count == 2);
    const QVector<TimelineClip> first_paste =
        scene.PersistentTimelineClips();
    CHECK(first_paste.size() == 2);
    CHECK(first_paste[1].id != first_paste[0].id);
    CHECK(first_paste[1].effect_id == first_paste[0].effect_id);
    CHECK(first_paste[1].lane_index == first_paste[0].lane_index);
    CHECK(
        first_paste[1].end_ms - first_paste[1].start_ms
        == first_paste[0].end_ms - first_paste[0].start_ms);
    CHECK(first_paste[1].start_ms > first_paste[0].start_ms);
    CHECK(duplicated_ids.size() == 1);
    CHECK(duplicated_ids[0].first == first_paste[0].id);
    CHECK(duplicated_ids[0].second == first_paste[1].id);

    CHECK(scene.PasteCopiedClips());
    CHECK(timeline_change_count == 3);
    const QVector<TimelineClip> second_paste =
        scene.PersistentTimelineClips();
    CHECK(second_paste.size() == 3);
    CHECK(second_paste[2].id != second_paste[1].id);
    CHECK(second_paste[2].start_ms > second_paste[1].start_ms);
    CHECK(duplicated_ids.size() == 2);
    CHECK(duplicated_ids[1].first == second_paste[0].id);
    CHECK(duplicated_ids[1].second == second_paste[2].id);

#undef CHECK
    return 0;
}
