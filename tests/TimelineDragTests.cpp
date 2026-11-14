#include "ui/timeline/internal/TimelineEditorInternal.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>

#include <algorithm>
#include <iostream>
#include <set>

namespace
{
using namespace lighttrack;
using namespace lighttrack::timeline_internal;

bool SameClip(const TimelineClip& left, const TimelineClip& right)
{
    return left.id == right.id && left.effect_id == right.effect_id
        && left.lane_index == right.lane_index
        && left.start_ms == right.start_ms && left.end_ms == right.end_ms;
}

bool SameClips(const QVector<TimelineClip>& left, const QVector<TimelineClip>& right)
{
    if(left.size() != right.size())
    {
        return false;
    }
    for(int i = 0; i < left.size(); ++i)
    {
        if(!SameClip(left[i], right[i]))
        {
            return false;
        }
    }
    return true;
}
}

int RunTimelineDragTests()
{
#define CHECK(expression)                                                        \
    do                                                                           \
    {                                                                            \
        if(!(expression))                                                        \
        {                                                                        \
            std::cerr << "Drag check failed at line " << __LINE__                 \
                      << ": " #expression << '\n';                               \
            return 1;                                                            \
        }                                                                        \
    } while(false)

    LightTrackView view;
    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.resize(720, 280);
    view.SetEffects({{QStringLiteral("Basic"), {{QStringLiteral("rainbow"),
        QStringLiteral("Rainbow"), QStringLiteral("Basic"), QColor(20, 120, 220)}}}});
    view.SetLanes({
        {QStringLiteral("a"), QStringLiteral("A"), 0},
        {QStringLiteral("b"), QStringLiteral("B"), 0},
        {QStringLiteral("c"), QStringLiteral("C"), 0},
        {QStringLiteral("d"), QStringLiteral("D"), 0},
        {QStringLiteral("e"), QStringLiteral("E"), 0}
    }, {});
    view.show();
    QApplication::processEvents();
    auto* scene = static_cast<LightTrackScene*>(view.scene());
    int changes = 0;
    view.SetTimelineChangedCallback([&changes]() { ++changes; });

    const auto mouse = [&view](QEvent::Type type, const QPointF& scene_pos,
        Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        const QPoint local = view.mapFromScene(scene_pos);
        const bool moving = type == QEvent::MouseMove;
        const bool releasing = type == QEvent::MouseButtonRelease;
        QMouseEvent event(type, QPointF(local), QPointF(local),
            QPointF(view.viewport()->mapToGlobal(local)),
            moving ? Qt::NoButton : Qt::LeftButton,
            releasing ? Qt::NoButton : Qt::LeftButton, modifiers);
        QApplication::sendEvent(view.viewport(), &event);
    };
    const auto click = [&mouse](const QPointF& pos,
        Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        mouse(QEvent::MouseButtonPress, pos, modifiers);
        mouse(QEvent::MouseButtonRelease, pos, modifiers);
    };
    const auto drag = [&mouse](const QPointF& from, const QPointF& to,
        Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        mouse(QEvent::MouseButtonPress, from, modifiers);
        mouse(QEvent::MouseMove, to, modifiers);
        mouse(QEvent::MouseButtonRelease, to, modifiers);
    };
    const auto key = [&view](int value, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QKeyEvent event(QEvent::KeyPress, value, modifiers);
        QApplication::sendEvent(&view, &event);
    };
    const auto reset = [&]()
    {
        view.ClearTimelineClips();
        view.SetVisibleLanes({0, 1, 2, 3, 4});
        view.SetHorizontalZoom(100);
        view.SetSnappingEnabled(false);
        view.horizontalScrollBar()->setValue(0);
        changes = 0;
        return view.RestoreClip(QStringLiteral("rainbow"), 0, 1000, 2000, ClipId(401)).IsValid()
            && view.RestoreClip(QStringLiteral("rainbow"), 1, 3000, 4500, ClipId(402)).IsValid()
            && view.RestoreClip(QStringLiteral("rainbow"), 4, 7000, 8000, ClipId(403)).IsValid();
    };
    const auto select_pair = [&]()
    {
        click(QPointF(150, 90));
        click(QPointF(350, 126), Qt::ControlModifier);
    };
    const auto pair_moved = [&view](const QVector<TimelineClip>& before,
        qint64 delta_ms, int lane_delta)
    {
        const auto after = view.PersistentTimelineClips();
        if(after.size() != before.size() || !SameClip(after[2], before[2]))
        {
            return false;
        }
        for(int i = 0; i < 2; ++i)
        {
            TimelineClip expected = before[i];
            expected.start_ms += delta_ms;
            expected.end_ms += delta_ms;
            expected.lane_index += lane_delta;
            if(!SameClip(after[i], expected))
            {
                return false;
            }
        }
        return true;
    };

    // A rectangle-selected group moves live from either member, then commits once.
    CHECK(reset());
    drag(QPointF(80, 73), QPointF(460, 143));
    CHECK(scene->selectedItems().size() == 2);
    const auto original = view.PersistentTimelineClips();
    mouse(QEvent::MouseButtonPress, QPointF(350, 126));
    CHECK(scene->selectedItems().size() == 2);
    CHECK(SameClips(original, view.PersistentTimelineClips()));
    mouse(QEvent::MouseMove, QPointF(410, 126));
    CHECK(pair_moved(original, 600, 0));
    mouse(QEvent::MouseMove, QPointF(450, 126));
    CHECK(pair_moved(original, 1000, 0));
    CHECK(changes == 0);
    mouse(QEvent::MouseButtonRelease, QPointF(450, 126));
    CHECK(changes == 1);
    CHECK(scene->selectedItems().size() == 2);
    key(Qt::Key_C, Qt::ControlModifier);
    key(Qt::Key_V, Qt::ControlModifier);
    const auto pasted = view.PersistentTimelineClips();
    CHECK(pasted.size() == 5);
    CHECK(pasted[4].start_ms - pasted[3].start_ms == original[1].start_ms - original[0].start_ms);

    // Ctrl selection and Ctrl dragging coexist; only a click toggles an item off.
    CHECK(reset());
    select_pair();
    drag(QPointF(350, 126), QPointF(450, 126), Qt::ControlModifier);
    CHECK(pair_moved(original, 1000, 0));
    CHECK(scene->selectedItems().size() == 2);
    CHECK(changes == 1);
    click(QPointF(450, 126), Qt::ControlModifier);
    CHECK(scene->selectedItems().size() == 1);
    CHECK(pair_moved(original, 1000, 0));

    // Small pointer jitter is a click, not an edit or a snapped movement.
    CHECK(reset());
    select_pair();
    view.SetSnappingEnabled(true);
    drag(QPointF(150, 90), QPointF(152, 91));
    CHECK(SameClips(original, view.PersistentTimelineClips()));
    CHECK(scene->selectedItems().size() == 1);
    CHECK(changes == 0);

    // Vertical shifts clamp the whole group at both track boundaries.
    CHECK(reset());
    select_pair();
    drag(QPointF(150, 90), QPointF(250, 126));
    CHECK(pair_moved(original, 1000, 1));
    drag(QPointF(250, 126), QPointF(250, 234));
    CHECK(pair_moved(original, 1000, 3));
    drag(QPointF(450, 234), QPointF(450, -30));
    CHECK(pair_moved(original, 1000, 0));
    CHECK(scene->selectedItems().size() == 2);
    CHECK(changes == 3);

    // Reaching zero from a later member preserves offsets, even beyond the view.
    CHECK(reset());
    select_pair();
    drag(QPointF(350, 126), QPointF(-200, 126));
    CHECK(pair_moved(original, -1000, 0));
    const qreal old_width = scene->ContentWidth();
    drag(QPointF(50, 90), QPointF(old_width + 300, 90));
    CHECK(pair_moved(original, qRound64((old_width + 150) * 10), 0));
    CHECK(scene->ContentWidth() >= (old_width + 600));

    // Snap using any member's edges, with a common correction for all members.
    CHECK(reset());
    view.ClearTimelineClips();
    CHECK(view.RestoreClip(QStringLiteral("rainbow"), 0, 1000, 2000, ClipId(401)).IsValid());
    CHECK(view.RestoreClip(QStringLiteral("rainbow"), 1, 3000, 4500, ClipId(402)).IsValid());
    CHECK(view.RestoreClip(QStringLiteral("rainbow"), 4, 4700, 5700, ClipId(403)).IsValid());
    select_pair();
    view.SetSnappingEnabled(true);
    const auto before_snap = view.PersistentTimelineClips();
    drag(QPointF(150, 90), QPointF(168, 90));
    CHECK(pair_moved(before_snap, 200, 0));

    // Selected members cannot attract each other using their previous positions.
    CHECK(reset());
    view.ClearTimelineClips();
    CHECK(view.RestoreClip(QStringLiteral("rainbow"), 0, 1230, 1670, ClipId(401)).IsValid());
    CHECK(view.RestoreClip(QStringLiteral("rainbow"), 1, 1830, 2310, ClipId(402)).IsValid());
    CHECK(view.RestoreClip(QStringLiteral("rainbow"), 4, 7000, 8000, ClipId(403)).IsValid());
    click(QPointF(145, 90));
    click(QPointF(207, 126), Qt::ControlModifier);
    view.SetSnappingEnabled(true);
    const auto before_self_snap = view.PersistentTimelineClips();
    drag(QPointF(145, 90), QPointF(156, 90));
    CHECK(pair_moved(before_self_snap, 170, 0));

    // Escape and a zoom change restore every member without recording an edit.
    CHECK(reset());
    select_pair();
    mouse(QEvent::MouseButtonPress, QPointF(150, 90));
    mouse(QEvent::MouseMove, QPointF(250, 126));
    CHECK(pair_moved(original, 1000, 1));
    key(Qt::Key_Escape);
    CHECK(SameClips(original, view.PersistentTimelineClips()));
    mouse(QEvent::MouseButtonRelease, QPointF(250, 126));
    CHECK(changes == 0);
    CHECK(scene->selectedItems().size() == 2);
    mouse(QEvent::MouseButtonPress, QPointF(150, 90));
    mouse(QEvent::MouseMove, QPointF(250, 126));
    view.SetHorizontalZoom(200);
    CHECK(SameClips(original, view.PersistentTimelineClips()));
    mouse(QEvent::MouseButtonRelease, QPointF(250, 126));
    CHECK(changes == 0);

    // Collapsed rows are skipped; the visible row spacing stays intact.
    CHECK(reset());
    CHECK(view.RestoreClip(QStringLiteral("rainbow"), 2, 9000, 9001, ClipId(404)).IsValid());
    view.SetVisibleLanes({0, 1, 4});
    select_pair();
    drag(QPointF(150, 90), QPointF(250, 126));
    const auto collapsed = view.PersistentTimelineClips();
    CHECK(collapsed[0].lane_index == 1 && collapsed[1].lane_index == 4);
    CHECK(collapsed[0].start_ms == 2000 && collapsed[1].start_ms == 4000);
    CHECK(SameClip(collapsed[2], original[2]));
    CHECK(collapsed[3].lane_index == 2 && collapsed[3].start_ms == 9000 && collapsed[3].end_ms == 9001);

    // Moving an unselected card edits only that card; resizing remains individual.
    CHECK(reset());
    select_pair();
    drag(QPointF(750, 234), QPointF(800, 234));
    const auto single = view.PersistentTimelineClips();
    CHECK(SameClip(single[0], original[0]) && SameClip(single[1], original[1]));
    CHECK(single[2].start_ms == 7500 && single[2].end_ms == 8500);
    CHECK(scene->selectedItems().size() == 1);
    select_pair();
    drag(QPointF(199, 90), QPointF(249, 90));
    const auto resized = view.PersistentTimelineClips();
    CHECK(resized[0].start_ms == 1000 && resized[0].end_ms == 2500);
    CHECK(SameClip(resized[1], original[1]));

    // Deleting during a group drag leaves no dangling gesture state.
    CHECK(reset());
    select_pair();
    mouse(QEvent::MouseButtonPress, QPointF(150, 90));
    mouse(QEvent::MouseMove, QPointF(250, 126));
    key(Qt::Key_Delete);
    CHECK(changes == 1);
    CHECK(view.PersistentTimelineClips().size() == 1);
    CHECK(SameClip(view.PersistentTimelineClips()[0], original[2]));
    mouse(QEvent::MouseButtonRelease, QPointF(250, 126));
    CHECK(changes == 1);

    // Effects menu placement: pick an effect, click a lane to place it.
    const QPointF lane_d(400, 198);
    const int clips_before = view.PersistentTimelineClips().size();
    view.BeginPlacement(QStringLiteral("rainbow"));
    CHECK(view.IsPlacing());
    mouse(QEvent::MouseMove, lane_d);
    mouse(QEvent::MouseButtonPress, lane_d);
    mouse(QEvent::MouseButtonRelease, lane_d);
    CHECK(!view.IsPlacing());
    const auto placed = view.PersistentTimelineClips();
    CHECK(placed.size() == clips_before + 1);
    CHECK(std::any_of(placed.begin(), placed.end(), [](const TimelineClip& clip)
    {
        return clip.lane_index == 3 && clip.effect_id == QStringLiteral("rainbow");
    }));

    // Esc cancels placement without adding anything.
    view.BeginPlacement(QStringLiteral("rainbow"));
    key(Qt::Key_Escape);
    CHECK(!view.IsPlacing());
    mouse(QEvent::MouseButtonPress, QPointF(650, 198));
    mouse(QEvent::MouseButtonRelease, QPointF(650, 198));
    CHECK(view.PersistentTimelineClips().size() == clips_before + 1);

    // Double-clicking a clip reports it (opens its settings window).
    ClipId activated;
    view.SetClipActivatedCallback([&activated](ClipId id) { activated = id; });
    mouse(QEvent::MouseButtonPress, lane_d);
    mouse(QEvent::MouseButtonRelease, lane_d);
    mouse(QEvent::MouseButtonDblClick, lane_d);
    mouse(QEvent::MouseButtonRelease, lane_d);
    CHECK(activated.IsValid());
    bool activated_is_placed = false;
    for(const TimelineClip& clip : view.PersistentTimelineClips())
    {
        activated_is_placed = activated_is_placed
            || (clip.id == activated && clip.lane_index == 3);
    }
    CHECK(activated_is_placed);

    // Grouping: grouped clips select (and so move) together.
    view.ClearTimelineClips();
    CHECK(view.RestoreClip(QStringLiteral("rainbow"), 0, 1000, 2000, ClipId(501)).IsValid());
    CHECK(view.RestoreClip(QStringLiteral("rainbow"), 1, 3000, 4500, ClipId(502)).IsValid());
    CHECK(view.RestoreClip(QStringLiteral("rainbow"), 2, 6000, 7000, ClipId(503)).IsValid());
    const auto selected_ids = [scene]()
    {
        std::set<quint64> ids;
        for(const TimelineClip& clip : scene->PersistentTimelineClips())
        {
            for(QGraphicsItem* item : scene->selectedItems())
            {
                auto* clip_item = dynamic_cast<TimelineClipItem*>(item);
                if(clip_item != nullptr && clip_item->Id() == clip.id)
                {
                    ids.insert(clip.id.Value());
                }
            }
        }
        return ids;
    };
    // Clip centers: A at 1.5 s, B at 3.75 s, C at 6.5 s.
    view.SetHorizontalZoom(80);
    view.horizontalScrollBar()->setValue(0);
    QApplication::processEvents();
    const qreal pps = scene->PixelsPerSecond();
    const QPointF clip_a(1.5 * pps, 90), clip_b(3.75 * pps, 126), clip_c(6.5 * pps, 162);
    click(clip_a);
    click(clip_b, Qt::ControlModifier);
    CHECK(scene->CanGroupSelection());
    key(Qt::Key_G, Qt::ControlModifier);
    for(const TimelineClip& clip : view.PersistentTimelineClips())
    {
        CHECK((clip.id.Value() == 503) == (clip.group_id == 0));
    }
    click(clip_c);
    CHECK(selected_ids() == std::set<quint64>({503}));
    click(clip_a);
    CHECK(selected_ids() == std::set<quint64>({501, 502}));
    click(clip_b, Qt::ControlModifier);
    CHECK(selected_ids().empty());
    drag(QPointF(2.5 * pps, 112), QPointF(3.25 * pps, 140)); // box over part of the group
    CHECK(selected_ids() == std::set<quint64>({501, 502}));
    // Copy/paste keeps the pasted clips grouped, as a new group.
    CHECK(scene->CopySelectedClips());
    CHECK(scene->PasteCopiedClips());
    std::set<quint64> pasted_groups;
    for(const TimelineClip& clip : view.PersistentTimelineClips())
    {
        if(clip.id.Value() > 503)
        {
            pasted_groups.insert(clip.group_id);
        }
    }
    CHECK(pasted_groups.size() == 1 && *pasted_groups.begin() != 0);
    // Ungroup.
    click(clip_a);
    key(Qt::Key_G, Qt::ControlModifier | Qt::ShiftModifier);
    click(clip_a);
    CHECK(selected_ids() == std::set<quint64>({501}));

    // Follow playhead off: playback and Stop never scroll the view.
    view.SetMusicSpectrum({0.5, 0.5, 0.5, 0.5}, 600000);
    QApplication::processEvents();
    QScrollBar* scroll = view.horizontalScrollBar();
    CHECK(scroll->maximum() > 1000);
    view.SetFollowPlayhead(false);
    scroll->setValue(800);
    view.SetMusicPosition(300000, true);
    view.SetMusicPosition(0, true);
    QApplication::processEvents();
    CHECK(scroll->value() == 800);
    // Following again: Stop returns the view to the start.
    view.SetFollowPlayhead(true);
    view.SetMusicPosition(0, true);
    CHECK(scroll->value() == scroll->minimum());

#undef CHECK
    return 0;
}
