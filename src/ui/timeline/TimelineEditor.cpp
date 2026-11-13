#include "TimelineEditor.h"

#include "internal/TimelineEditorInternal.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
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
        QVBoxLayout* editor_layout = new QVBoxLayout(editor);
        editor_layout->setContentsMargins(0, 0, 0, 0);
        editor_layout->setSpacing(0);

        // Effects menu strip: pick an effect, then click a lane to place it.
        QWidget* effects_strip = new QWidget(editor);
        QHBoxLayout* strip_layout = new QHBoxLayout(effects_strip);
        strip_layout->setContentsMargins(0, 0, 0, 2);
        strip_layout->setSpacing(4);
        QLabel* effects_label =
            HeaderLabel(QStringLiteral("Effects"), effects_strip);
        effects_menu_bar = new QMenuBar(effects_strip);
        effects_menu_bar->setObjectName(QStringLiteral("effectsMenuBar"));
        effects_menu_bar->setNativeMenuBar(false);
        effects_menu_bar->setToolTip(QStringLiteral(
            "Pick an effect, then click a lane to place it "
            "(Shift+click to place several, Esc to cancel). "
            "You can also right-click the timeline > Add effect."));
        strip_layout->addWidget(effects_label);
        strip_layout->addWidget(effects_menu_bar, 1);

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

        editor_layout->addWidget(effects_strip);
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
        ruler->SetHideDisabledLanesChangedCallback(
            [this](bool hide)
            {
                lane_list->SetHideDisabledLanes(hide);
            });
        ruler->SetExpandAllRequestedCallback(
            [this](bool expand)
            {
                lane_list->SetAllExpanded(expand);
            });
        lane_list->SetExpansionChangedCallback(
            [this](bool any_expanded)
            {
                ruler->SetDevicesExpanded(any_expanded);
            });
    }

    QMenuBar* effects_menu_bar;
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
    d->effects_menu_bar->clear();
    for(const EffectGroup& group : groups)
    {
        if(group.effects.isEmpty())
        {
            continue;
        }
        timeline_internal::AddEffectActions(
            d->effects_menu_bar->addMenu(group.name),
            group,
            [this](const QString& effect_id)
            {
                d->view->BeginPlacement(effect_id);
            });
    }
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

void TimelineEditor::SetMusicPeaks(const WaveformPeaks& peaks)
{
    d->view->SetMusicPeaks(peaks);
}

void TimelineEditor::SetWaveformStyle(WaveformStyle style)
{
    d->view->SetWaveformStyle(style);
}

void TimelineEditor::SetMusicPosition(
    qint64 position_ms,
    bool turn_page)
{
    d->view->SetMusicPosition(position_ms, turn_page);
}

void TimelineEditor::SetFollowPlayhead(bool follow)
{
    d->view->SetFollowPlayhead(follow);
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
    ClipId requested_id,
    quint64 group_id)
{
    return d->view->RestoreClip(
        effect_id,
        lane_index,
        start_ms,
        end_ms,
        requested_id,
        group_id);
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

void TimelineEditor::SetClipActivatedCallback(
    ClipActivatedCallback callback)
{
    d->view->SetClipActivatedCallback(std::move(callback));
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

void TimelineEditor::SetLaneHidden(int lane_index, bool hidden)
{
    d->lane_list->SetLaneHidden(lane_index, hidden);
}

void TimelineEditor::SetChooseDevicesCallback(
    std::function<void()> callback)
{
    d->ruler->SetChooseDevicesRequestedCallback(std::move(callback));
}
}
