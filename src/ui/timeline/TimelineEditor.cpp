#include "TimelineEditor.h"

#include "internal/TimelineEditorInternal.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace lighttrack
{
using namespace timeline_internal;

class TimelineEditorPrivate
{
public:
    explicit TimelineEditorPrivate(TimelineEditor* editor)
    {
        QHBoxLayout* editor_layout = new QHBoxLayout(editor);
        editor_layout->setContentsMargins(0, 0, 0, 0);
        editor_layout->setSpacing(0);

        QWidget* effects_column = new QWidget(editor);
        QVBoxLayout* effects_layout =
            new QVBoxLayout(effects_column);
        effects_layout->setContentsMargins(0, 0, 0, 0);
        effects_layout->setSpacing(0);

        QLabel* effects_header =
            HeaderLabel(QStringLiteral("Effects"), effects_column);
        effects_header->setFixedHeight(
            static_cast<int>(RULER_HEIGHT));
        effects_list = new EffectsListWidget(effects_column);
        effects_layout->addWidget(effects_header);
        effects_layout->addWidget(effects_list);
        effects_column->setFixedWidth(
            static_cast<int>(EFFECTS_PANEL_WIDTH));

        QWidget* track_area = new QWidget(editor);
        QVBoxLayout* track_layout = new QVBoxLayout(track_area);
        track_layout->setContentsMargins(0, 0, 0, 0);
        track_layout->setSpacing(0);

        ruler = new TimelineRulerWidget(track_area);
        track_layout->addWidget(ruler);

        QWidget* timeline_body = new QWidget(track_area);
        QHBoxLayout* timeline_body_layout =
            new QHBoxLayout(timeline_body);
        timeline_body_layout->setContentsMargins(0, 0, 0, 0);
        timeline_body_layout->setSpacing(0);

        QSplitter* track_splitter =
            new QSplitter(Qt::Horizontal, timeline_body);
        track_splitter->setChildrenCollapsible(false);
        track_splitter->setHandleWidth(static_cast<int>(GAP));

        lane_list = new LaneListWidget(track_splitter);
        lane_list->setMinimumWidth(180);
        view = new LightTrackView(track_splitter);
        view->setMinimumWidth(320);
        view->SetRuler(ruler);

        track_splitter->addWidget(lane_list);
        track_splitter->addWidget(view);
        track_splitter->setStretchFactor(0, 0);
        track_splitter->setStretchFactor(1, 1);
        track_splitter->setSizes({
            static_cast<int>(LABEL_WIDTH),
            static_cast<int>(TIMELINE_MIN_WIDTH)
        });
        ruler->SetLabelWidth(
            LABEL_WIDTH + track_splitter->handleWidth());

        timeline_body_layout->addWidget(track_splitter);
        track_layout->addWidget(timeline_body, 1);

        editor_layout->addWidget(effects_column);
        editor_layout->addSpacing(static_cast<int>(GAP));
        editor_layout->addWidget(track_area, 1);

        QObject::connect(
            view->verticalScrollBar(),
            &QScrollBar::valueChanged,
            lane_list->verticalScrollBar(),
            &QScrollBar::setValue);
        QObject::connect(
            lane_list->verticalScrollBar(),
            &QScrollBar::valueChanged,
            view->verticalScrollBar(),
            &QScrollBar::setValue);
        lane_list->SetVisibilityChangedCallback(
            [this](const QVector<int>& lane_indices)
            {
                view->SetVisibleLanes(lane_indices);
            });
        lane_list->SetWidthChangedCallback(
            [this, track_splitter](int width)
            {
                ruler->SetLabelWidth(
                    width + track_splitter->handleWidth());
            });
        ruler->SetHideZeroLedZonesChangedCallback(
            [this](bool hide)
            {
                lane_list->SetHideZeroLedZones(hide);
            });
    }

    EffectsListWidget* effects_list;
    LaneListWidget* lane_list;
    TimelineRulerWidget* ruler;
    LightTrackView* view;
};

TimelineEditor::TimelineEditor(QWidget* parent) :
    QWidget(parent),
    d(std::make_unique<TimelineEditorPrivate>(this))
{
}

TimelineEditor::~TimelineEditor() = default;

void TimelineEditor::SetEffects(const QVector<EffectGroup>& groups)
{
    d->effects_list->SetEffects(groups);
    d->view->SetEffects(groups);
}

void TimelineEditor::SetLanes(
    const QVector<TimelineLane>& lanes,
    const QString& empty_text)
{
    d->lane_list->SetLanes(lanes);
    d->view->SetLanes(lanes, empty_text);
    d->view->SetVisibleLanes(
        d->lane_list->VisibleLaneIndices());
}

void TimelineEditor::SetMusicSpectrum(
    const QVector<qreal>& spectrum,
    qint64 duration_ms)
{
    d->view->SetMusicSpectrum(spectrum, duration_ms);
}

void TimelineEditor::SetMusicPosition(
    qint64 position_ms,
    bool turn_page)
{
    d->view->SetMusicPosition(position_ms, turn_page);
}

void TimelineEditor::SetMinimumTimelineDuration(qint64 duration_ms)
{
    d->view->SetMinimumTimelineDuration(duration_ms);
}

void TimelineEditor::SetHorizontalZoom(int pixels_per_second)
{
    d->view->SetHorizontalZoom(pixels_per_second);
}

void TimelineEditor::SetSnappingEnabled(bool enabled)
{
    d->view->SetSnappingEnabled(enabled);
}

QVector<TimelineClip> TimelineEditor::TimelineClips() const
{
    return d->view->TimelineClips();
}

QVector<TimelineClip> TimelineEditor::PersistentTimelineClips() const
{
    return d->view->PersistentTimelineClips();
}

std::optional<TimelineClip> TimelineEditor::FindClip(ClipId id) const
{
    if(!id.IsValid())
    {
        return std::nullopt;
    }

    const QVector<TimelineClip> clips =
        d->view->PersistentTimelineClips();
    const auto found = std::find_if(
        clips.cbegin(),
        clips.cend(),
        [id](const TimelineClip& clip)
        {
            return clip.id == id;
        });
    return found == clips.cend()
        ? std::nullopt
        : std::optional<TimelineClip>{*found};
}

void TimelineEditor::ClearTimelineClips()
{
    d->view->ClearTimelineClips();
}

ClipId TimelineEditor::RestoreClip(
    const QString& effect_id,
    int lane_index,
    qint64 start_ms,
    qint64 end_ms,
    ClipId requested_id)
{
    return d->view->RestoreClip(
        effect_id,
        lane_index,
        start_ms,
        end_ms,
        requested_id);
}

void TimelineEditor::SetMusicSeekCallback(MusicSeekCallback callback)
{
    d->view->SetMusicSeekCallback(std::move(callback));
}

void TimelineEditor::SetClipSelectedCallback(
    ClipSelectedCallback callback)
{
    d->view->SetClipSelectedCallback(std::move(callback));
}

void TimelineEditor::SetClipRemovedCallback(
    ClipRemovedCallback callback)
{
    d->view->SetClipRemovedCallback(std::move(callback));
}

void TimelineEditor::SetClipDuplicatedCallback(
    ClipDuplicatedCallback callback)
{
    d->view->SetClipDuplicatedCallback(std::move(callback));
}

void TimelineEditor::SetTimelineChangedCallback(
    TimelineChangedCallback callback)
{
    d->view->SetTimelineChangedCallback(std::move(callback));
}

void TimelineEditor::SetLaneRenamedCallback(
    LaneRenamedCallback callback)
{
    d->lane_list->SetLaneRenamedCallback(std::move(callback));
}

void TimelineEditor::SetLaneHighlightedCallback(
    LaneStateChangedCallback callback)
{
    d->lane_list->SetLaneHighlightedCallback(
        std::move(callback));
}

void TimelineEditor::SetLaneDisabledCallback(
    LaneStateChangedCallback callback)
{
    d->lane_list->SetLaneDisabledCallback(
        std::move(callback));
}
}
