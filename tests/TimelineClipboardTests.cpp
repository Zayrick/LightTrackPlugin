#include "ui/timeline/internal/TimelineEditorInternal.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QGraphicsSceneMouseEvent>
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

    scene.SetMusicSpectrum({}, 5000);
    CHECK(scene.SetPixelsPerSecond(100.0));

    QVector<qint64> seek_positions;
    scene.SetMusicSeekCallback(
        [&seek_positions](qint64 position_ms)
        {
            seek_positions.push_back(position_ms);
        });

    const auto send_mouse_event =
        [&scene](
            QEvent::Type type,
            const QPointF& position,
            Qt::MouseButton button,
            Qt::MouseButtons buttons)
        {
            QGraphicsSceneMouseEvent event(type);
            event.setScenePos(position);
            event.setPos(position);
            event.setButton(button);
            event.setButtons(buttons);
            QCoreApplication::sendEvent(&scene, &event);
        };

    send_mouse_event(
        QEvent::GraphicsSceneMousePress,
        QPointF(250.0, ROW_HEIGHT / 2.0),
        Qt::LeftButton,
        Qt::LeftButton);
    send_mouse_event(
        QEvent::GraphicsSceneMouseRelease,
        QPointF(250.0, ROW_HEIGHT / 2.0),
        Qt::LeftButton,
        Qt::NoButton);
    CHECK(seek_positions.size() == 1);
    CHECK(seek_positions[0] == 2500);

    send_mouse_event(
        QEvent::GraphicsSceneMousePress,
        QPointF(800.0, ROW_HEIGHT / 2.0),
        Qt::LeftButton,
        Qt::LeftButton);
    send_mouse_event(
        QEvent::GraphicsSceneMouseRelease,
        QPointF(800.0, ROW_HEIGHT / 2.0),
        Qt::LeftButton,
        Qt::NoButton);
    CHECK(seek_positions.size() == 2);
    CHECK(seek_positions[1] == 5000);

    send_mouse_event(
        QEvent::GraphicsSceneMousePress,
        QPointF(800.0, ROW_HEIGHT + ROW_HEIGHT / 2.0),
        Qt::LeftButton,
        Qt::LeftButton);
    send_mouse_event(
        QEvent::GraphicsSceneMouseRelease,
        QPointF(800.0, ROW_HEIGHT + ROW_HEIGHT / 2.0),
        Qt::LeftButton,
        Qt::NoButton);
    CHECK(seek_positions.size() == 2);

    // Short clips keep their duration through zoom, restore and clipboard use.
    scene.ClearTimelineClips();
    scene.SetSnappingEnabled(false);
    CHECK(scene.RestoreClip(
        QStringLiteral("rainbow"), 0, 1000, 1001, ClipId(100)).IsValid());
    for(qreal zoom : {40.0, 240.0, 100.0})
    {
        CHECK(scene.SetPixelsPerSecond(zoom));
        const auto clips = scene.PersistentTimelineClips();
        CHECK(clips.size() == 1);
        CHECK(clips[0].start_ms == 1000);
        CHECK(clips[0].end_ms == 1001);
    }
    CHECK(scene.SelectClipAt(QPointF(103.0, 54.0)));
    CHECK(scene.CopySelectedClips());
    CHECK(scene.PasteCopiedClips());
    const auto short_paste = scene.PersistentTimelineClips();
    CHECK(short_paste.size() == 2);
    CHECK(short_paste[1].end_ms - short_paste[1].start_ms == 1);

    const auto drag_clip =
        [&send_mouse_event](qreal from_x, qreal to_x)
        {
            send_mouse_event(
                QEvent::GraphicsSceneMousePress,
                QPointF(from_x, 54.0),
                Qt::LeftButton,
                Qt::LeftButton);
            send_mouse_event(
                QEvent::GraphicsSceneMouseMove,
                QPointF(to_x, 54.0),
                Qt::NoButton,
                Qt::LeftButton);
            send_mouse_event(
                QEvent::GraphicsSceneMouseRelease,
                QPointF(to_x, 54.0),
                Qt::LeftButton,
                Qt::NoButton);
            QCoreApplication::processEvents();
        };

    scene.ClearTimelineClips();
    CHECK(scene.RestoreClip(
        QStringLiteral("rainbow"), 0, 1000, 3000, ClipId(101)).IsValid());
    drag_clip(299.0, 104.0);
    auto resized = scene.PersistentTimelineClips();
    CHECK(resized.size() == 1);
    CHECK(resized[0].start_ms == 1000);
    CHECK(resized[0].end_ms == 1050);

    // The center and both edges remain reachable on a narrow card.
    drag_clip(103.0, 153.0);
    resized = scene.PersistentTimelineClips();
    CHECK(resized[0].start_ms == 1500);
    CHECK(resized[0].end_ms == 1550);
    drag_clip(150.5, 154.5);
    resized = scene.PersistentTimelineClips();
    CHECK(resized[0].start_ms == 1540);
    CHECK(resized[0].end_ms == 1550);
    drag_clip(159.0, 149.0);
    resized = scene.PersistentTimelineClips();
    CHECK(resized[0].start_ms == 1540);
    CHECK(resized[0].end_ms == 1541);

    // Dragging beyond the old right boundary extends the timeline immediately.
    const qreal old_content_width = scene.ContentWidth();
    drag_clip(159.0, 1159.0);
    resized = scene.PersistentTimelineClips();
    CHECK(resized[0].start_ms == 1540);
    CHECK(resized[0].end_ms == 11541);
    CHECK(scene.ContentWidth() > old_content_width);
    CHECK(scene.ContentWidth() > 1154.1);
    // The playhead at 5s must not block the clip underneath it.
    drag_clip(500.0, 2000.0);
    resized = scene.PersistentTimelineClips();
    CHECK(resized[0].start_ms == 16540);
    CHECK(resized[0].end_ms == 26541);
    CHECK(scene.ContentWidth() > 2654.1);

    // Restoring a long clip into a smaller viewport must not move its start.
    scene.ClearTimelineClips();
    scene.SetViewportSize(QSize(700, 144));
    CHECK(scene.RestoreClip(
        QStringLiteral("rainbow"), 0, 40000, 70000, ClipId(102)).IsValid());
    CHECK(scene.SetPixelsPerSecond(40.0));
    CHECK(scene.SetPixelsPerSecond(100.0));
    scene.SetViewportSize(QSize(600, 144));
    const auto restored_long = scene.PersistentTimelineClips();
    CHECK(restored_long.size() == 1);
    CHECK(restored_long[0].start_ms == 40000);
    CHECK(restored_long[0].end_ms == 70000);
    CHECK(scene.ContentWidth() > 7000.0);

#undef CHECK
    return 0;
}
