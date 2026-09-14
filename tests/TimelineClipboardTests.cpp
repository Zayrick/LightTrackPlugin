#include "ui/timeline/internal/TimelineEditorInternal.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QGraphicsSceneMouseEvent>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>

#include <algorithm>
#include <iostream>
#include <utility>

int RunTimelineDragTests();

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
            Qt::MouseButtons buttons,
            Qt::KeyboardModifiers modifiers = Qt::NoModifier)
        {
            QGraphicsSceneMouseEvent event(type);
            event.setScenePos(position);
            event.setPos(position);
            event.setScreenPos(position.toPoint());
            event.setButton(button);
            event.setButtons(buttons);
            event.setModifiers(modifiers);
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

    // Selection gestures never edit the clips or add history entries.
    scene.ClearTimelineClips();
    scene.SetLanes({
        {QStringLiteral("device"), QStringLiteral("Device"), 0},
        {QStringLiteral("zone"), QStringLiteral("Zone"), 1},
        {QStringLiteral("segment"), QStringLiteral("Segment"), 2}
    }, {});
    CHECK(scene.RestoreClip(
        QStringLiteral("rainbow"), 0, 1000, 2000, ClipId(201)).IsValid());
    CHECK(scene.RestoreClip(
        QStringLiteral("rainbow"), 1, 3000, 4500, ClipId(202)).IsValid());
    CHECK(scene.RestoreClip(
        QStringLiteral("rainbow"), 0, 5000, 6000, ClipId(203)).IsValid());
    CHECK(scene.RestoreClip(
        QStringLiteral("rainbow"), 2, 1000, 1001, ClipId(204)).IsValid());
    const auto before_selection = scene.PersistentTimelineClips();
    const int changes_before_selection = timeline_change_count;
    std::optional<ClipId> inspected_clip;
    scene.SetClipSelectedCallback(
        [&inspected_clip](std::optional<ClipId> id) { inspected_clip = id; });

    const auto selected_ids = [](const QGraphicsScene& target)
    {
        QVector<ClipId> ids;
        for(QGraphicsItem* item : target.selectedItems())
        {
            if(const auto* clip = dynamic_cast<TimelineClipItem*>(item))
            {
                ids.push_back(clip->Id());
            }
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    };
    const QVector<ClipId> first_two{ClipId(201), ClipId(202)};
    const QVector<ClipId> first_three{ClipId(201), ClipId(202), ClipId(203)};
    const auto click_scene =
        [&send_mouse_event](
            const QPointF& pos,
            Qt::KeyboardModifiers modifiers = Qt::NoModifier,
            Qt::MouseButton button = Qt::LeftButton)
        {
            send_mouse_event(QEvent::GraphicsSceneMousePress, pos,
                button, button, modifiers);
            send_mouse_event(QEvent::GraphicsSceneMouseRelease, pos,
                button, Qt::NoButton, modifiers);
        };
    const auto box_select =
        [&send_mouse_event](
            const QPointF& from,
            const QPointF& to,
            Qt::KeyboardModifiers modifiers = Qt::NoModifier)
        {
            send_mouse_event(QEvent::GraphicsSceneMousePress, from,
                Qt::LeftButton, Qt::LeftButton, modifiers);
            send_mouse_event(QEvent::GraphicsSceneMouseMove, to,
                Qt::NoButton, Qt::LeftButton, modifiers);
            send_mouse_event(QEvent::GraphicsSceneMouseRelease, to,
                Qt::LeftButton, Qt::NoButton, modifiers);
        };

    click_scene(QPointF(150.0, 54.0));
    click_scene(QPointF(350.0, 90.0), Qt::ControlModifier);
    CHECK(selected_ids(scene) == first_two);
    click_scene(QPointF(150.0, 54.0), Qt::ControlModifier);
    CHECK(selected_ids(scene) == QVector<ClipId>{ClipId(202)});
    CHECK(inspected_clip == ClipId(202));
    click_scene(QPointF(350.0, 90.0), Qt::ControlModifier);
    CHECK(!scene.HasSelectedClips());
    CHECK(!inspected_clip.has_value());
    click_scene(QPointF(150.0, 54.0), Qt::ControlModifier);
    click_scene(QPointF(350.0, 90.0), Qt::ControlModifier);
    click_scene(QPointF(700.0, 90.0), Qt::ControlModifier);
    CHECK(selected_ids(scene) == first_two);

    // Right-clicking a selected card or empty space retains the whole group.
    click_scene(QPointF(150.0, 54.0), Qt::NoModifier, Qt::RightButton);
    CHECK(selected_ids(scene) == first_two);
    click_scene(QPointF(700.0, 90.0), Qt::NoModifier, Qt::RightButton);
    CHECK(selected_ids(scene) == first_two);
    click_scene(QPointF(550.0, 54.0), Qt::NoModifier, Qt::RightButton);
    CHECK(selected_ids(scene) == QVector<ClipId>{ClipId(203)});

    // Selection updates as the rectangle grows, shrinks, and is released.
    send_mouse_event(QEvent::GraphicsSceneMousePress, QPointF(80.0, 37.0),
        Qt::LeftButton, Qt::LeftButton);
    CHECK(!scene.HasSelectedClips());
    send_mouse_event(QEvent::GraphicsSceneMouseMove, QPointF(400.0, 107.0),
        Qt::NoButton, Qt::LeftButton);
    CHECK(selected_ids(scene) == first_two);
    send_mouse_event(QEvent::GraphicsSceneMouseMove, QPointF(220.0, 71.0),
        Qt::NoButton, Qt::LeftButton);
    CHECK(selected_ids(scene) == QVector<ClipId>{ClipId(201)});
    send_mouse_event(QEvent::GraphicsSceneMouseRelease, QPointF(400.0, 107.0),
        Qt::LeftButton, Qt::NoButton);
    CHECK(selected_ids(scene) == first_two);

    // Reverse drags and partial intersections work on multiple lanes.
    box_select(QPointF(470.0, 110.0), QPointF(130.0, 40.0));
    CHECK(selected_ids(scene) == first_two);
    box_select(QPointF(490.0, 37.0), QPointF(610.0, 71.0));
    CHECK(selected_ids(scene) == QVector<ClipId>{ClipId(203)});
    box_select(QPointF(80.0, 37.0), QPointF(400.0, 107.0), Qt::ControlModifier);
    CHECK(selected_ids(scene) == first_three);
    click_scene(QPointF(550.0, 54.0));
    CHECK(selected_ids(scene) == QVector<ClipId>{ClipId(203)});
    send_mouse_event(QEvent::GraphicsSceneMousePress, QPointF(80.0, 37.0),
        Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    send_mouse_event(QEvent::GraphicsSceneMouseMove, QPointF(400.0, 107.0),
        Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
    CHECK(selected_ids(scene) == first_three);
    send_mouse_event(QEvent::GraphicsSceneMouseRelease, QPointF(80.0, 110.0),
        Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    CHECK(selected_ids(scene) == QVector<ClipId>{ClipId(203)});
    click_scene(QPointF(700.0, 90.0));
    CHECK(!scene.HasSelectedClips());
    CHECK(!scene.CopySelectedClips());

    // A click with small pointer jitter does not accidentally select a card.
    box_select(QPointF(99.0, 37.0), QPointF(101.0, 39.0));
    CHECK(!scene.HasSelectedClips());
    // Select even the visible tail of a 1 ms clip, but never hidden lanes.
    box_select(QPointF(104.0, 109.0), QPointF(110.0, 140.0));
    CHECK(selected_ids(scene) == QVector<ClipId>{ClipId(204)});
    scene.SetVisibleLanes({0, 1});
    box_select(QPointF(80.0, 37.0), QPointF(610.0, 144.0));
    CHECK(selected_ids(scene) == first_three);
    CHECK(timeline_change_count == changes_before_selection);
    const auto after_selection = scene.PersistentTimelineClips();
    CHECK(after_selection.size() == before_selection.size());
    for(int i = 0; i < after_selection.size(); ++i)
    {
        CHECK(after_selection[i].id == before_selection[i].id);
        CHECK(after_selection[i].lane_index == before_selection[i].lane_index);
        CHECK(after_selection[i].start_ms == before_selection[i].start_ms);
        CHECK(after_selection[i].end_ms == before_selection[i].end_ms);
    }

    // Exercise real viewport mouse dispatch and the existing keyboard actions,
    // including a scrolled timeline and repeated group pastes at its right edge.
    LightTrackView view;
    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.resize(640, 200);
    view.SetEffects({{QStringLiteral("Basic"), {{QStringLiteral("rainbow"),
        QStringLiteral("Rainbow"), QStringLiteral("Basic"), QColor(20, 120, 220)}}}});
    view.SetLanes({{QStringLiteral("a"), QStringLiteral("A"), 0},
        {QStringLiteral("b"), QStringLiteral("B"), 0}}, {});
    view.SetSnappingEnabled(false);
    view.show();
    QCoreApplication::processEvents();
    view.SetHorizontalZoom(100);
    CHECK(view.RestoreClip(
        QStringLiteral("rainbow"), 0, 10000, 11000, ClipId(301)).IsValid());
    CHECK(view.RestoreClip(
        QStringLiteral("rainbow"), 1, 12500, 14000, ClipId(302)).IsValid());
    auto* view_scene = static_cast<LightTrackScene*>(view.scene());
    const auto originals = view.PersistentTimelineClips();
    const qreal width_before_paste = view_scene->ContentWidth();
    view.horizontalScrollBar()->setValue(900);
    CHECK(view.horizontalScrollBar()->value() > 0);
    const auto send_view_mouse =
        [&view](QEvent::Type type, const QPointF& scene_pos,
            Qt::MouseButton button, Qt::MouseButtons buttons,
            Qt::KeyboardModifiers modifiers = Qt::NoModifier)
        {
            const QPoint local = view.mapFromScene(scene_pos);
            QMouseEvent event(type, QPointF(local), QPointF(local),
                QPointF(view.viewport()->mapToGlobal(local)), button, buttons, modifiers);
            QCoreApplication::sendEvent(view.viewport(), &event);
        };
    send_view_mouse(QEvent::MouseButtonPress, QPointF(980.0, 37.0),
        Qt::LeftButton, Qt::LeftButton);
    send_view_mouse(QEvent::MouseMove, QPointF(1420.0, 107.0),
        Qt::NoButton, Qt::LeftButton);
    CHECK(view_scene->selectedItems().size() == 2);
    send_view_mouse(QEvent::MouseButtonRelease, QPointF(1420.0, 107.0),
        Qt::LeftButton, Qt::NoButton);
    CHECK(view_scene->selectedItems().size() == 2);
    send_view_mouse(QEvent::MouseButtonPress, QPointF(1050.0, 54.0),
        Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    send_view_mouse(QEvent::MouseButtonRelease, QPointF(1050.0, 54.0),
        Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    CHECK(selected_ids(*view_scene) == QVector<ClipId>{ClipId(302)});
    send_view_mouse(QEvent::MouseButtonPress, QPointF(1050.0, 54.0),
        Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    send_view_mouse(QEvent::MouseButtonRelease, QPointF(1050.0, 54.0),
        Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    CHECK(view_scene->selectedItems().size() == 2);

    int group_changes = 0;
    QVector<std::pair<ClipId, ClipId>> group_duplicates;
    view.SetTimelineChangedCallback([&group_changes]() { ++group_changes; });
    view.SetClipDuplicatedCallback([&group_duplicates](ClipId source, ClipId copied)
        { group_duplicates.push_back({source, copied}); });
    const auto send_key = [&view](int key, Qt::KeyboardModifiers modifiers)
    {
        QKeyEvent event(QEvent::KeyPress, key, modifiers);
        QCoreApplication::sendEvent(&view, &event);
    };
    send_key(Qt::Key_C, Qt::ControlModifier);
    CHECK(group_changes == 0);
    const auto copied_data = QJsonDocument::fromJson(
        QGuiApplication::clipboard()->mimeData()->data(CLIP_MIME));
    CHECK(copied_data.object().value(QStringLiteral("clips")).toArray().size() == 2);
    for(int sequence = 1; sequence <= 3; ++sequence)
    {
        send_key(Qt::Key_V, Qt::ControlModifier);
        const auto pasted = view.PersistentTimelineClips();
        CHECK(pasted.size() == 2 + sequence * 2);
        CHECK(group_changes == sequence);
        CHECK(group_duplicates.size() == sequence * 2);
        CHECK(view_scene->selectedItems().size() == 2);
        const qint64 offset = pasted[sequence * 2].start_ms - originals[0].start_ms;
        CHECK(offset > 0);
        for(int i = 0; i < 2; ++i)
        {
            const int index = sequence * 2 + i;
            CHECK(pasted[index].id != originals[i].id);
            CHECK(pasted[index].effect_id == originals[i].effect_id);
            CHECK(pasted[index].lane_index == originals[i].lane_index);
            CHECK(pasted[index].start_ms == originals[i].start_ms + offset);
            CHECK(pasted[index].end_ms == originals[i].end_ms + offset);
            CHECK(group_duplicates[index - 2].first == originals[i].id);
            CHECK(group_duplicates[index - 2].second == pasted[index].id);
            CHECK(selected_ids(*view_scene).contains(pasted[index].id));
            if(sequence > 1)
            {
                CHECK(pasted[index].start_ms > pasted[index - 2].start_ms);
            }
        }
    }
    CHECK(view_scene->ContentWidth() > width_before_paste);
    send_key(Qt::Key_Delete, Qt::NoModifier);
    CHECK(view.PersistentTimelineClips().size() == 6);
    CHECK(group_changes == 4);
    CHECK(!view_scene->HasSelectedClips());

    // Rebuilding the layout during a gesture clears its transient state.
    send_mouse_event(QEvent::GraphicsSceneMousePress, QPointF(80.0, 37.0),
        Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    send_mouse_event(QEvent::GraphicsSceneMouseMove, QPointF(400.0, 107.0),
        Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
    scene.ClearTimelineClips();
    send_mouse_event(QEvent::GraphicsSceneMouseRelease, QPointF(400.0, 107.0),
        Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
    CHECK(!scene.HasSelectedClips());
    CHECK(scene.PersistentTimelineClips().isEmpty());

#undef CHECK
    return RunTimelineDragTests();
}
