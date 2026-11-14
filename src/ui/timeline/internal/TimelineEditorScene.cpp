#include "TimelineEditorInternal.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsSceneMouseEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QResizeEvent>
#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <optional>
#include <utility>

namespace lighttrack::timeline_internal
{
namespace
{
constexpr int CLIPBOARD_FORMAT_VERSION = 1;
constexpr int MAX_CLIPBOARD_CLIPS = 1000;

struct ClipboardClip
{
    ClipId source_id;
    QString effect_id;
    int lane_index = -1;
    qint64 start_ms = 0;
    qint64 end_ms = 0;
    quint64 group_id = 0;
};

struct ClipboardPayload
{
    QString source_editor_id;
    QVector<ClipboardClip> clips;
};

QByteArray EncodeClipboardPayload(
    const QString& source_editor_id,
    const QVector<TimelineClip>& clips)
{
    QJsonArray clip_values;
    for(const TimelineClip& clip : clips)
    {
        QJsonObject clip_value;
        clip_value.insert(
            QStringLiteral("sourceClipId"),
            QString::number(clip.id.Value()));
        clip_value.insert(
            QStringLiteral("effectId"),
            clip.effect_id);
        clip_value.insert(
            QStringLiteral("laneIndex"),
            clip.lane_index);
        clip_value.insert(
            QStringLiteral("startMs"),
            QString::number(clip.start_ms));
        clip_value.insert(
            QStringLiteral("endMs"),
            QString::number(clip.end_ms));
        if(clip.group_id != 0)
        {
            clip_value.insert(
                QStringLiteral("group"),
                QString::number(clip.group_id));
        }
        clip_values.push_back(clip_value);
    }

    QJsonObject root;
    root.insert(
        QStringLiteral("version"),
        CLIPBOARD_FORMAT_VERSION);
    root.insert(
        QStringLiteral("sourceEditorId"),
        source_editor_id);
    root.insert(QStringLiteral("clips"), clip_values);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

std::optional<ClipboardPayload> DecodeClipboardPayload(
    const QByteArray& data)
{
    QJsonParseError parse_error;
    const QJsonDocument document =
        QJsonDocument::fromJson(data, &parse_error);
    if(parse_error.error != QJsonParseError::NoError
        || !document.isObject())
    {
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    if(root.value(QStringLiteral("version")).toInt(-1)
            != CLIPBOARD_FORMAT_VERSION
        || !root.value(QStringLiteral("sourceEditorId")).isString()
        || !root.value(QStringLiteral("clips")).isArray())
    {
        return std::nullopt;
    }

    ClipboardPayload payload;
    payload.source_editor_id =
        root.value(QStringLiteral("sourceEditorId")).toString();
    const QJsonArray clip_values =
        root.value(QStringLiteral("clips")).toArray();
    if(payload.source_editor_id.isEmpty()
        || clip_values.empty()
        || clip_values.size() > MAX_CLIPBOARD_CLIPS)
    {
        return std::nullopt;
    }

    payload.clips.reserve(clip_values.size());
    for(const QJsonValue& value : clip_values)
    {
        if(!value.isObject())
        {
            return std::nullopt;
        }

        const QJsonObject clip_value = value.toObject();
        bool source_id_valid = false;
        bool start_valid = false;
        bool end_valid = false;
        const quint64 source_id = clip_value
            .value(QStringLiteral("sourceClipId"))
            .toString()
            .toULongLong(&source_id_valid);
        const qint64 start_ms = clip_value
            .value(QStringLiteral("startMs"))
            .toString()
            .toLongLong(&start_valid);
        const qint64 end_ms = clip_value
            .value(QStringLiteral("endMs"))
            .toString()
            .toLongLong(&end_valid);
        const QString effect_id = clip_value
            .value(QStringLiteral("effectId"))
            .toString();
        const int lane_index = clip_value
            .value(QStringLiteral("laneIndex"))
            .toInt(-1);

        if(!source_id_valid
            || source_id == 0
            || !start_valid
            || !end_valid
            || start_ms < 0
            || end_ms <= start_ms
            || effect_id.isEmpty()
            || lane_index < 0)
        {
            return std::nullopt;
        }

        payload.clips.push_back({
            ClipId(source_id),
            effect_id,
            lane_index,
            start_ms,
            end_ms,
            clip_value.value(QStringLiteral("group"))
                .toString()
                .toULongLong()
        });
    }
    return payload;
}
}

LightTrackScene::LightTrackScene(QObject* parent) :
    QGraphicsScene(parent),
    clipboard_source_id(
        QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

void LightTrackScene::SetEffects(const QVector<EffectGroup>& groups)
{
    effects.clear();
    for(const EffectGroup& group : groups)
    {
        for(EffectDescriptor effect : group.effects)
        {
            if(effect.category.isEmpty())
            {
                effect.category = group.name;
            }
            effects.insert(effect.id, std::move(effect));
        }
    }
}

void LightTrackScene::SetPalette(const QPalette& new_palette)
{
    palette = new_palette;
    RebuildLayout();
}

void LightTrackScene::SetViewportSize(const QSize& size)
{
    if(viewport_size == size)
    {
        return;
    }

    viewport_size = size;
    RebuildLayout();
}

void LightTrackScene::SetLanes(
    const QVector<TimelineLane>& entries,
    const QString& empty_text)
{
    CancelDrag();
    ClearClips();
    lane_entries = entries;
    visible_lane_indices.clear();
    visible_lane_indices.reserve(lane_entries.size());
    for(int i = 0; i < lane_entries.size(); i++)
    {
        visible_lane_indices.push_back(i);
    }
    empty_message = empty_text;
    RebuildLayout();
}

void LightTrackScene::SetVisibleLanes(const QVector<int>& indices)
{
    QVector<int> filtered_indices;
    filtered_indices.reserve(indices.size());

    for(int index : indices)
    {
        if(index >= 0
            && index < lane_entries.size()
            && !filtered_indices.contains(index))
        {
            filtered_indices.push_back(index);
        }
    }

    if(filtered_indices.size() == visible_lane_indices.size()
        && std::equal(
            filtered_indices.cbegin(),
            filtered_indices.cend(),
            visible_lane_indices.cbegin()))
    {
        return;
    }

    CancelDrag();
    visible_lane_indices = filtered_indices;
    RebuildLayout();
}

void LightTrackScene::SetMusicSpectrum(
    const QVector<qreal>& spectrum,
    qint64 duration_ms)
{
    music_spectrum = spectrum;
    music_duration_ms = qMax<qint64>(0, duration_ms);
    music_position_ms = 0;
    RebuildLayout();
}

void LightTrackScene::SetMusicPosition(qint64 position_ms)
{
    music_position_ms = qMax<qint64>(0, position_ms);
    if(music_duration_ms > 0)
    {
        music_position_ms =
            qMin(music_position_ms, music_duration_ms);
    }
    UpdatePlayhead();
}

void LightTrackScene::SetMusicPeaks(const WaveformPeaks& peaks)
{
    music_peaks = peaks;
    if(music_item != nullptr)
    {
        music_item->SetPeaks(music_peaks);
    }
}

void LightTrackScene::SetWaveformStyle(WaveformStyle style)
{
    waveform_style = style;
    if(music_item != nullptr)
    {
        music_item->SetStyle(style);
    }
}

void LightTrackScene::SetMinimumContentDuration(qint64 duration_ms)
{
    const qint64 clamped_duration_ms = qMax<qint64>(0, duration_ms);
    if(minimum_content_duration_ms == clamped_duration_ms)
    {
        return;
    }

    minimum_content_duration_ms = clamped_duration_ms;
    RebuildLayout();
}

void LightTrackScene::SetMusicSeekCallback(TimelineEditor::MusicSeekCallback callback)
{
    music_seek_callback = std::move(callback);
}

void LightTrackScene::SetClipSelectedCallback(
    TimelineEditor::ClipSelectedCallback callback)
{
    clip_selected_callback = std::move(callback);
}

void LightTrackScene::SetClipRemovedCallback(
    TimelineEditor::ClipRemovedCallback callback)
{
    clip_removed_callback = std::move(callback);
}

void LightTrackScene::SetClipDuplicatedCallback(
    TimelineEditor::ClipDuplicatedCallback callback)
{
    clip_duplicated_callback = std::move(callback);
}

void LightTrackScene::SetTimelineChangedCallback(
    TimelineEditor::TimelineChangedCallback callback)
{
    timeline_changed_callback = std::move(callback);
}

void LightTrackScene::ClearTimelineClips()
{
    CancelDrag();
    ClearClips();
    RefreshInheritedClips();
}

ClipId LightTrackScene::RestoreClip(
    const QString& effect_id,
    int lane,
    qint64 start_ms,
    qint64 end_ms,
    ClipId requested_id,
    quint64 group_id)
{
    const EffectDescriptor* effect = FindEffect(effect_id);
    if(effect == nullptr
        || lane < 0
        || lane >= lanes.size()
        || end_ms <= start_ms
        || pixels_per_second <= 0.0)
    {
        return {};
    }

    const qreal x = start_ms * pixels_per_second / 1000.0;
    const qreal width =
        (end_ms - start_ms) * pixels_per_second / 1000.0;
    TimelineClipItem* clip =
        AddClip(*effect, lane, x, width, false, false, requested_id);
    if(clip != nullptr)
    {
        clip->SetGroupId(group_id);
    }
    return clip == nullptr ? ClipId{} : clip->Id();
}

void LightTrackScene::SetSnappingEnabled(bool enabled)
{
    snapping_enabled = enabled;
}

bool LightTrackScene::SetPixelsPerSecond(qreal value)
{
    value = qBound(
        TIMELINE_ZOOM_MIN,
        value,
        TIMELINE_ZOOM_MAX);
    if(qFuzzyCompare(pixels_per_second, value))
    {
        return false;
    }

    CancelDrag();
    const qreal ratio = value / pixels_per_second;
    for(TimelineClipItem* clip : clips)
    {
        clip->SetClipWidth(clip->ClipWidth() * ratio);
        clip->setPos(clip->pos().x() * ratio, clip->pos().y());
    }

    pixels_per_second = value;
    RebuildLayout();
    return true;
}

qreal LightTrackScene::PixelsPerSecond() const
{
    return pixels_per_second;
}

qreal LightTrackScene::ContentWidth() const
{
    return sceneRect().width();
}

QVector<TimelineClip> LightTrackScene::TimelineClips() const
{
    return BuildClipSnapshots(false);
}

QVector<TimelineClip> LightTrackScene::PersistentTimelineClips() const
{
    return BuildClipSnapshots(true);
}

bool LightTrackScene::PreviewEffectAt(const QString& effect_id, const QPointF& pos)
{
    const EffectDescriptor* effect = FindEffect(effect_id);
    if(effect == nullptr)
    {
        HidePreview();
        return false;
    }

    const int lane = LaneAt(pos);
    if(lane < 0)
    {
        HidePreview();
        return false;
    }

    const qreal clip_width = DefaultClipWidth();
    const qreal x =
        qMax<qreal>(
            0.0,
            SnapClipX(
                pos.x() - clip_width / 2.0,
                clip_width,
                nullptr));
    ShowPreview(lane, x, clip_width, *effect);
    return true;
}

bool LightTrackScene::AddEffectAt(const QString& effect_id, const QPointF& pos)
{
    const EffectDescriptor* effect = FindEffect(effect_id);
    if(effect == nullptr)
    {
        return false;
    }

    const int lane = LaneAt(pos);
    if(lane < 0)
    {
        return false;
    }

    const qreal clip_width = DefaultClipWidth();
    const qreal x = SnapClipX(
        pos.x() - clip_width / 2.0,
        clip_width,
        nullptr);
    return AddClip(
               *effect,
               lane,
               x,
               clip_width,
               true,
               true,
               {})
        != nullptr;
}

void LightTrackScene::ClearPreview()
{
    HidePreview();
}

bool LightTrackScene::HasSelectedClips() const
{
    return std::any_of(
        clips.cbegin(),
        clips.cend(),
        [](const TimelineClipItem* clip)
        {
            return clip->isSelected();
        });
}

bool LightTrackScene::SelectClipAt(const QPointF& pos)
{
    TimelineClipItem* clip = ClipAt(pos);
    if(clip == nullptr)
    {
        return false;
    }

    if(!clip->isSelected())
    {
        clearSelection();
        clip->setSelected(true);
        NotifyClipSelected(clip);
    }
    return true;
}

bool LightTrackScene::CopySelectedClips()
{
    const QVector<TimelineClip> snapshots =
        BuildClipSnapshots(true);
    QVector<TimelineClip> selected_clips;
    selected_clips.reserve(clips.size());
    for(int i = 0; i < clips.size() && i < snapshots.size(); i++)
    {
        if(clips[i]->isSelected())
        {
            selected_clips.push_back(snapshots[i]);
        }
    }

    if(selected_clips.empty())
    {
        return false;
    }

    const QByteArray data = EncodeClipboardPayload(
        clipboard_source_id,
        selected_clips);
    QMimeData* mime_data = new QMimeData();
    mime_data->setData(CLIP_MIME, data);
    QGuiApplication::clipboard()->setMimeData(mime_data);
    last_pasted_clip_data.clear();
    paste_sequence = 0;
    return true;
}

bool LightTrackScene::CanPasteCopiedClips() const
{
    const QMimeData* mime_data =
        QGuiApplication::clipboard()->mimeData();
    return mime_data != nullptr
        && mime_data->hasFormat(CLIP_MIME);
}

bool LightTrackScene::PasteCopiedClips()
{
    const QMimeData* mime_data =
        QGuiApplication::clipboard()->mimeData();
    if(mime_data == nullptr
        || !mime_data->hasFormat(CLIP_MIME)
        || pixels_per_second <= 0.0
        || lanes.empty())
    {
        return false;
    }

    const QByteArray data = mime_data->data(CLIP_MIME);
    const std::optional<ClipboardPayload> payload =
        DecodeClipboardPayload(data);
    if(!payload.has_value())
    {
        return false;
    }

    if(last_pasted_clip_data != data)
    {
        last_pasted_clip_data = data;
        paste_sequence = 0;
    }

    const int next_sequence =
        paste_sequence == std::numeric_limits<int>::max()
        ? paste_sequence
        : paste_sequence + 1;
    const qint64 paste_interval_ms =
        qMax<qint64>(
            1,
            TimelineTickIntervalMs(pixels_per_second));
    qint64 latest_end_ms = 0;
    for(const ClipboardClip& copied_clip : payload->clips)
    {
        latest_end_ms = qMax(latest_end_ms, copied_clip.end_ms);
    }
    const qint64 maximum_offset =
        std::numeric_limits<qint64>::max() - latest_end_ms;
    const qint64 paste_offset_ms =
        next_sequence > maximum_offset / paste_interval_ms
        ? maximum_offset
        : next_sequence * paste_interval_ms;

    struct PendingPaste
    {
        ClipId source_id;
        EffectDescriptor effect;
        int lane_index = -1;
        qreal x = 0.0;
        qreal width = CLIP_DEFAULT_WIDTH;
        quint64 group_id = 0;
    };

    QVector<PendingPaste> pending_clips;
    pending_clips.reserve(payload->clips.size());
    for(const ClipboardClip& copied_clip : payload->clips)
    {
        const EffectDescriptor* effect =
            FindEffect(copied_clip.effect_id);
        const qint64 duration_ms =
            copied_clip.end_ms - copied_clip.start_ms;
        if(effect == nullptr
            || copied_clip.lane_index >= lanes.size()
            || duration_ms <= 0)
        {
            continue;
        }

        // Move the whole selection by the same amount. AddClip extends the
        // timeline, so clips near its end keep their spacing and duration.
        const qint64 start_ms = copied_clip.start_ms + paste_offset_ms;
        pending_clips.push_back({
            copied_clip.source_id,
            *effect,
            copied_clip.lane_index,
            start_ms * pixels_per_second / 1000.0,
            duration_ms * pixels_per_second / 1000.0,
            copied_clip.group_id
        });
    }

    if(pending_clips.empty())
    {
        return false;
    }

    CancelDrag();
    clearSelection();
    const bool same_editor =
        payload->source_editor_id == clipboard_source_id;
    TimelineClipItem* last_added_clip = nullptr;
    // Copied groups become new groups of the pasted clips.
    std::map<quint64, quint64> pasted_groups;
    quint64 next_group = NextGroupId();
    for(const PendingPaste& pending : pending_clips)
    {
        TimelineClipItem* clip = AddClip(
            pending.effect,
            pending.lane_index,
            pending.x,
            pending.width,
            false,
            false,
            {});
        if(clip == nullptr)
        {
            continue;
        }

        if(pending.group_id != 0)
        {
            auto group = pasted_groups.find(pending.group_id);
            if(group == pasted_groups.end())
            {
                group = pasted_groups.emplace(
                    pending.group_id, next_group++).first;
            }
            clip->SetGroupId(group->second);
        }
        clip->setSelected(true);
        last_added_clip = clip;
        if(same_editor && clip_duplicated_callback)
        {
            clip_duplicated_callback(
                pending.source_id,
                clip->Id());
        }
    }

    if(last_added_clip == nullptr)
    {
        return false;
    }

    paste_sequence = next_sequence;
    NotifyClipSelected(last_added_clip);
    NotifyTimelineChanged();
    return true;
}

bool LightTrackScene::DeleteSelectedClips()
{
    CancelDrag();
    QVector<TimelineClipItem*> selected_clips;
    for(TimelineClipItem* clip : clips)
    {
        if(clip->isSelected())
        {
            selected_clips.push_back(clip);
        }
    }

    for(TimelineClipItem* clip : selected_clips)
    {
        RemoveClip(clip);
    }

    if(!selected_clips.empty())
    {
        NotifyTimelineChanged();
    }

    return !selected_clips.empty();
}

void LightTrackScene::drawBackground(QPainter* painter, const QRectF& rect)
{
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->fillRect(rect, palette.color(QPalette::Base));

    QColor music_row_color = palette.color(QPalette::Window);
    if(!music_row_color.isValid())
    {
        music_row_color = palette.color(QPalette::Base);
    }

    PaintRow(
        painter,
        QRectF(0.0, 0.0, sceneRect().width(), MUSIC_ROW_HEIGHT),
        music_row_color,
        TextLineColor(palette, 52, 82),
        TextLineColor(palette, 24, 42),
        rect);

    if(lane_entries.empty())
    {
        if(!empty_message.isEmpty())
        {
            QFont font;
            font.setPointSize(10);
            painter->setRenderHint(QPainter::TextAntialiasing, true);
            painter->setFont(font);
            painter->setPen(TextLineColor(palette, 160, 170));
            painter->drawText(
                QRectF(
                    12.0,
                    MUSIC_ROW_HEIGHT + 14.0,
                    sceneRect().width() - 24.0,
                    ROW_HEIGHT),
                Qt::AlignLeft | Qt::AlignTop,
                empty_message);
        }
        return;
    }

    for(int lane_index : visible_lane_indices)
    {
        PaintRow(
            painter,
            lanes[lane_index].rect,
            palette.color(QPalette::Base),
            TextLineColor(palette, 38, 72),
            TextLineColor(palette, 20, 38),
            rect);
    }

    PaintInheritedClips(painter, rect);
}

void LightTrackScene::drawForeground(QPainter* painter, const QRectF&)
{
    if(drag_mode != BoxSelection || !selection_box_visible)
    {
        return;
    }

    painter->save();
    const QColor highlight = palette.color(QPalette::Highlight);
    QPen pen(highlight, 1.0);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(WithAlpha(highlight, 45));
    painter->drawRect(selection_rect);
    painter->restore();
}

void LightTrackScene::keyPressEvent(QKeyEvent* event)
{
    if(event->key() == Qt::Key_Escape && drag_mode != NoDrag)
    {
        CancelDrag();
        event->accept();
        return;
    }
    QGraphicsScene::keyPressEvent(event);
}

quint64 LightTrackScene::NextGroupId() const
{
    quint64 highest = 0;
    for(const TimelineClipItem* clip : clips)
    {
        highest = qMax(highest, clip->GroupId());
    }
    return highest + 1;
}

void LightTrackScene::SetGroupSelected(
    const TimelineClipItem* clip,
    bool selected)
{
    if(clip == nullptr)
    {
        return;
    }
    for(TimelineClipItem* candidate : clips)
    {
        if(candidate == clip
            || (clip->GroupId() != 0
                && candidate->GroupId() == clip->GroupId()))
        {
            candidate->setSelected(selected);
        }
    }
}

void LightTrackScene::ExpandSelectionToGroups()
{
    std::set<quint64> groups;
    for(const TimelineClipItem* clip : clips)
    {
        if(clip->isSelected() && clip->GroupId() != 0)
        {
            groups.insert(clip->GroupId());
        }
    }
    for(TimelineClipItem* clip : clips)
    {
        if(groups.count(clip->GroupId()) != 0)
        {
            clip->setSelected(true);
        }
    }
}

bool LightTrackScene::CanGroupSelection() const
{
    // At least two selected clips that are not already one group.
    int selected = 0;
    std::set<quint64> groups;
    for(const TimelineClipItem* clip : clips)
    {
        if(clip->isSelected())
        {
            ++selected;
            groups.insert(clip->GroupId());
        }
    }
    return selected >= 2
        && !(groups.size() == 1 && *groups.begin() != 0);
}

bool LightTrackScene::CanUngroupSelection() const
{
    for(const TimelineClipItem* clip : clips)
    {
        if(clip->isSelected() && clip->GroupId() != 0)
        {
            return true;
        }
    }
    return false;
}

bool LightTrackScene::GroupSelectedClips()
{
    if(!CanGroupSelection())
    {
        return false;
    }
    CancelDrag();
    const quint64 group = NextGroupId();
    for(TimelineClipItem* clip : clips)
    {
        if(clip->isSelected())
        {
            clip->SetGroupId(group);
        }
    }
    NotifyTimelineChanged();
    return true;
}

bool LightTrackScene::UngroupSelectedClips()
{
    if(!CanUngroupSelection())
    {
        return false;
    }
    CancelDrag();
    for(TimelineClipItem* clip : clips)
    {
        if(clip->isSelected())
        {
            clip->SetGroupId(0);
        }
    }
    NotifyTimelineChanged();
    return true;
}

void LightTrackScene::SetClipActivatedCallback(
    TimelineEditor::ClipActivatedCallback callback)
{
    clip_activated_callback = std::move(callback);
}

void LightTrackScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if(event->button() == Qt::LeftButton)
    {
        if(TimelineClipItem* clip = ClipAt(event->scenePos()))
        {
            CancelDrag();
            if(clip_activated_callback)
            {
                clip_activated_callback(clip->Id());
            }
            event->accept();
            return;
        }
    }
    // Elsewhere a double click behaves like a normal press.
    mousePressEvent(event);
}

void LightTrackScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if(event->button() == Qt::RightButton)
    {
        if(drag_mode == NoDrag)
        {
            SelectClipAt(event->scenePos());
        }
        event->accept();
        return;
    }

    if(event->button() != Qt::LeftButton)
    {
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    CancelDrag();
    const QPointF scene_pos = event->scenePos();
    const bool control_pressed = event->modifiers() & Qt::ControlModifier;
    const bool is_music_row =
        music_duration_ms > 0
        && scene_pos.y() >= 0.0
        && scene_pos.y() < MUSIC_ROW_HEIGHT;
    if(IsPlayheadHandle(scene_pos) || is_music_row)
    {
        clearSelection();
        NotifyClipSelected(nullptr);
        drag_mode = PlayheadSeek;
        SeekMusicAt(scene_pos.x());
        event->accept();
        return;
    }

    TimelineClipItem* clip = ClipAt(scene_pos);
    if(clip == nullptr)
    {
        if(control_pressed)
        {
            for(TimelineClipItem* selected_clip : clips)
            {
                if(selected_clip->isSelected())
                {
                    selection_before_drag.insert(selected_clip);
                }
            }
        }
        else
        {
            clearSelection();
            NotifyClipSelected(nullptr);
        }
        drag_mode = BoxSelection;
        drag_scene_start = scene_pos;
        drag_screen_start = event->screenPos();
        event->accept();
        return;
    }

    if(control_pressed)
    {
        // Defer deselection until release so Ctrl+drag can move the group too.
        pending_clip_click = clip->isSelected()
            ? DeselectClickedClip : KeepClipSelection;
        SetGroupSelected(clip, true);
    }
    else
    {
        pending_clip_click = SelectOnlyClickedClip;
        if(!clip->isSelected())
        {
            clearSelection();
            SetGroupSelected(clip, true);
        }
    }

    const QPointF local_pos =
        clip->mapFromScene(scene_pos);
    active_clip = clip;
    if(!control_pressed && clip->IsLeftResizeHandle(local_pos))
    {
        drag_mode = ClipResizeLeft;
    }
    else if(!control_pressed && clip->IsRightResizeHandle(local_pos))
    {
        drag_mode = ClipResizeRight;
    }
    else
    {
        drag_mode = ClipMove;
    }

    drag_scene_start = scene_pos;
    drag_screen_start = event->screenPos();
    clip_start_x = clip->pos().x();
    clip_start_width = clip->ClipWidth();
    clip_start_lane = clip->LaneIndex();
    if(drag_mode == ClipMove)
    {
        for(TimelineClipItem* selected_clip : clips)
        {
            const int row = visible_lane_indices.indexOf(selected_clip->LaneIndex());
            if(selected_clip->isSelected() && selected_clip->isVisible() && row >= 0)
            {
                moving_clips.push_back({selected_clip, selected_clip->pos().x(),
                    selected_clip->LaneIndex(), row});
                moving_clip_items.insert(selected_clip);
            }
        }
    }
    else
    {
        // Edge handles continue to resize just the clicked clip.
        clearSelection();
        clip->setSelected(true);
        active_clip->setOpacity(0.88);
        active_clip->setZValue(80.0);
        UpdateClipResize(scene_pos);
    }
    NotifyClipSelected(clip);

    event->accept();
}

void LightTrackScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if(drag_mode == BoxSelection)
    {
        UpdateBoxSelection(event->scenePos(), event->screenPos());
        event->accept();
        return;
    }

    if(drag_mode == ClipMove)
    {
        UpdateClipMove(event->scenePos(), event->screenPos());
        event->accept();
        return;
    }

    if(drag_mode == ClipResizeLeft
        || drag_mode == ClipResizeRight)
    {
        UpdateClipResize(event->scenePos());
        event->accept();
        return;
    }

    if(drag_mode == PlayheadSeek)
    {
        SeekMusicAt(event->scenePos().x());
        event->accept();
        return;
    }

    QGraphicsScene::mouseMoveEvent(event);
}

void LightTrackScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if(event->button() != Qt::LeftButton)
    {
        QGraphicsScene::mouseReleaseEvent(event);
        return;
    }

    if(drag_mode == BoxSelection)
    {
        UpdateBoxSelection(event->scenePos(), event->screenPos());
        CancelDrag();
        // A box touching part of a group selects the whole group.
        ExpandSelectionToGroups();
        event->accept();
        return;
    }

    if(drag_mode == PlayheadSeek)
    {
        drag_mode = NoDrag;
        event->accept();
        return;
    }

    if(drag_mode == ClipMove)
    {
        UpdateClipMove(event->scenePos(), event->screenPos());
        bool changed = false;
        for(const ClipMoveStart& start : moving_clips)
        {
            changed = changed || start.clip->LaneIndex() != start.lane
                || !qFuzzyCompare(start.clip->pos().x() + 1.0, start.x + 1.0);
        }

        if(!clip_move_started && active_clip != nullptr)
        {
            if(pending_clip_click == SelectOnlyClickedClip)
            {
                clearSelection();
                SetGroupSelected(active_clip, true);
                NotifyClipSelected(active_clip);
            }
            else if(pending_clip_click == DeselectClickedClip)
            {
                SetGroupSelected(active_clip, false);
                TimelineClipItem* selected_clip = nullptr;
                for(TimelineClipItem* candidate : clips)
                {
                    if(candidate->isSelected())
                    {
                        selected_clip = candidate;
                    }
                }
                NotifyClipSelected(selected_clip);
            }
        }

        CancelDrag(false);
        if(changed)
        {
            NotifyTimelineChanged();
        }
        event->accept();
        return;
    }

    if(drag_mode == ClipResizeLeft
        || drag_mode == ClipResizeRight)
    {
        bool changed = false;
        if(active_clip != nullptr)
        {
            changed =
                active_clip->LaneIndex() != clip_start_lane
                || !qFuzzyCompare(
                    active_clip->pos().x() + 1.0,
                    clip_start_x + 1.0)
                || !qFuzzyCompare(
                    active_clip->ClipWidth() + 1.0,
                    clip_start_width + 1.0);
        }

        CancelDrag(false);
        if(changed)
        {
            NotifyTimelineChanged();
        }
        event->accept();
        return;
    }

    QGraphicsScene::mouseReleaseEvent(event);
}

template<typename ItemType>
ItemType* LightTrackScene::Track(ItemType* item)
{
    item->setAcceptedMouseButtons(Qt::NoButton);
    layout_items.push_back(item);
    return item;
}

const EffectDescriptor* LightTrackScene::FindEffect(const QString& id) const
{
    const auto found = effects.constFind(id);
    return found == effects.cend() ? nullptr : &found.value();
}

bool LightTrackScene::ContainsClipId(ClipId id) const
{
    return std::any_of(
        clips.cbegin(),
        clips.cend(),
        [id](const TimelineClipItem* clip)
        {
            return clip->Id() == id;
        });
}

ClipId LightTrackScene::AllocateClipId(ClipId requested)
{
    if(requested.IsValid())
    {
        if(ContainsClipId(requested))
        {
            return {};
        }

        next_clip_id =
            qMax(next_clip_id, requested.Value());
        return requested;
    }

    do
    {
        ++next_clip_id;
        if(next_clip_id == 0)
        {
            ++next_clip_id;
        }
    }
    while(ContainsClipId(ClipId(next_clip_id)));

    return ClipId(next_clip_id);
}

QVector<TimelineClip> LightTrackScene::BuildClipSnapshots(bool persistent) const
{
    QVector<TimelineClip> states;
    if(pixels_per_second <= 0.0)
    {
        return states;
    }

    states.reserve(clips.size());
    for(TimelineClipItem* clip : clips)
    {
        const qreal start_x = clip->pos().x();
        const qreal start_value =
            start_x * 1000.0 / pixels_per_second;
        const qreal end_value =
            (start_x + clip->ClipWidth())
            * 1000.0 / pixels_per_second;

        states.push_back({
            clip->Id(),
            clip->Effect().id,
            clip->LaneIndex(),
            qMax<qint64>(
                0,
                persistent
                    ? qRound64(start_value)
                    : static_cast<qint64>(std::floor(start_value))),
            qMax<qint64>(
                0,
                persistent
                    ? qRound64(end_value)
                    : static_cast<qint64>(std::ceil(end_value))),
            clip->GroupId()
        });
    }

    return states;
}

void LightTrackScene::ClearLayoutItems()
{
    for(QGraphicsItem* item : layout_items)
    {
        removeItem(item);
        delete item;
    }
    layout_items.clear();

    if(preview != nullptr)
    {
        removeItem(preview);
        delete preview;
        preview = nullptr;
    }

    music_item = nullptr;
    playhead_item = nullptr;
    playhead_handle = nullptr;
}

void LightTrackScene::ClearClips()
{
    for(TimelineClipItem* clip : clips)
    {
        if(clip_removed_callback)
        {
            clip_removed_callback(clip->Id());
        }
        removeItem(clip);
        delete clip;
    }
    clips.clear();
    active_clip = nullptr;
    NotifyClipSelected(nullptr);
}

void LightTrackScene::RebuildLayout()
{
    ClearLayoutItems();
    lanes.clear();

    const int lane_count = qMax(1, visible_lane_indices.size());
    const qreal content_height =
        MUSIC_ROW_HEIGHT + lane_count * ROW_HEIGHT;
    const qreal minimum_content_width =
        minimum_content_duration_ms * pixels_per_second / 1000.0;
    qreal scene_width = qMax(
        qMax(
            qMax<qreal>(TIMELINE_MIN_WIDTH, viewport_size.width()),
            MusicPixelWidth()),
        minimum_content_width);
    for(const TimelineClipItem* clip : clips)
    {
        scene_width = qMax(
            scene_width,
            clip->pos().x() + clip->boundingRect().width()
                + pixels_per_second);
    }
    const qreal scene_height =
        qMax<qreal>(content_height, viewport_size.height());

    setSceneRect(0.0, 0.0, scene_width, scene_height);

    for(const TimelineLane& lane : lane_entries)
    {
        lanes.push_back({lane.name, QRectF(), lane.level});
    }

    for(
        int visible_row = 0;
        visible_row < visible_lane_indices.size();
        visible_row++)
    {
        const int lane_index = visible_lane_indices[visible_row];
        lanes[lane_index].rect = QRectF(
            0.0,
            MUSIC_ROW_HEIGHT + visible_row * ROW_HEIGHT,
            scene_width,
            ROW_HEIGHT);
    }

    invalidate(sceneRect(), QGraphicsScene::BackgroundLayer);
    AddMusicItems();
    RelayoutClips();
}

void LightTrackScene::EnsureContentWidth(qreal right)
{
    const qreal width = right + pixels_per_second;
    if(width <= sceneRect().width())
    {
        return;
    }

    for(int lane : visible_lane_indices)
    {
        lanes[lane].rect.setRight(width);
    }
    setSceneRect(0.0, 0.0, width, sceneRect().height());
    RefreshInheritedClips();
}

void LightTrackScene::RefreshInheritedClips()
{
    invalidate(sceneRect(), QGraphicsScene::BackgroundLayer);
}

void LightTrackScene::PaintRow(
    QPainter* painter,
    const QRectF& row_rect,
    const QColor& fill,
    const QColor& border,
    const QColor& grid,
    const QRectF& exposed) const
{
    if(!row_rect.intersects(exposed))
    {
        return;
    }

    painter->fillRect(row_rect.intersected(exposed), fill);
    painter->setPen(QPen(border));
    painter->drawRect(
        row_rect.adjusted(0.0, 0.0, -0.5, -0.5));

    if(pixels_per_second <= 0.0)
    {
        return;
    }

    const qint64 tick_interval_ms =
        TimelineTickIntervalMs(pixels_per_second);
    const qreal tick_width =
        tick_interval_ms * pixels_per_second / 1000.0;
    painter->setPen(QPen(grid));
    const qint64 first_tick = static_cast<qint64>(
        std::ceil(qMax(tick_width, exposed.left()) / tick_width));

    for(qint64 tick = first_tick;; tick++)
    {
        const qreal x = tick * tick_width;
        if(x >= row_rect.right() || x > exposed.right())
        {
            break;
        }
        painter->drawLine(
            QPointF(x, row_rect.top() + 1.0),
            QPointF(x, row_rect.bottom() - 1.0));
    }
}

void LightTrackScene::RelayoutClips()
{
    if(lanes.empty())
    {
        return;
    }

    for(TimelineClipItem* clip : clips)
    {
        if(clip->scene() == nullptr)
        {
            addItem(clip);
        }

        const int lane =
            qBound(0, clip->LaneIndex(), lanes.size() - 1);
        clip->SetLaneIndex(lane);
        if(!lanes[lane].rect.isValid()
            || lanes[lane].rect.isEmpty())
        {
            clip->setVisible(false);
            continue;
        }

        clip->setVisible(true);
        const qreal x = qMax<qreal>(0.0, clip->pos().x());
        clip->setPos(x, ClipY(lane));
    }

    RefreshInheritedClips();
}

bool LightTrackScene::IsDescendantLane(int lane, int ancestor_lane) const
{
    if(lane <= ancestor_lane
        || lane >= lanes.size()
        || ancestor_lane >= lanes.size())
    {
        return false;
    }

    const int ancestor_level = lanes[ancestor_lane].level;
    for(int i = ancestor_lane + 1; i <= lane; i++)
    {
        if(lanes[i].level <= ancestor_level)
        {
            return false;
        }
    }

    return true;
}

void LightTrackScene::PaintGhostClip(
    QPainter* painter,
    const QRectF& exposed,
    int lane,
    qreal x,
    qreal width,
    const EffectDescriptor& effect,
    bool preview_ghost) const
{
    const QRectF ghost_rect(
        x, ClipY(lane), qMax(CLIP_MIN_DISPLAY_WIDTH, width), CLIP_HEIGHT);
    if(!ghost_rect.intersects(exposed))
    {
        return;
    }

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(
        WithAlpha(effect.color, preview_ghost ? 36 : 44));
    painter->setPen(QPen(
        WithAlpha(
            effect.color.darker(115),
            preview_ghost ? 100 : 125),
        1.0,
        preview_ghost ? Qt::DashLine : Qt::SolidLine));
    painter->drawRoundedRect(ghost_rect, 5.0, 5.0);

    QFont font = painter->font();
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(
        WithAlpha(Qt::white, preview_ghost ? 88 : 110));
    const int text_width =
        static_cast<int>(ghost_rect.width() - 18.0);
    if(text_width > 0)
    {
        painter->drawText(
            ghost_rect.adjusted(9.0, 0.0, -9.0, 0.0),
            Qt::AlignVCenter | Qt::AlignLeft,
            QFontMetrics(font).elidedText(
                effect.name, Qt::ElideRight, text_width));
    }
}

void LightTrackScene::PaintInheritedClips(
    QPainter* painter,
    const QRectF& exposed) const
{
    for(int lane = 0; lane < lanes.size(); lane++)
    {
        if(!lanes[lane].rect.intersects(exposed))
        {
            continue;
        }

        for(
            int ancestor_lane = 0;
            ancestor_lane < lane;
            ancestor_lane++)
        {
            if(!IsDescendantLane(lane, ancestor_lane))
            {
                continue;
            }

            for(TimelineClipItem* clip : clips)
            {
                if(clip->LaneIndex() == ancestor_lane)
                {
                    PaintGhostClip(
                        painter,
                        exposed,
                        lane,
                        clip->pos().x(),
                        clip->ClipWidth(),
                        clip->Effect(),
                        false);
                }
            }

            if(active_clip == nullptr
                && last_preview_lane == ancestor_lane)
            {
                PaintGhostClip(
                    painter,
                    exposed,
                    lane,
                    last_preview_x,
                    last_preview_width,
                    last_preview_effect,
                    true);
            }
        }
    }
}

TimelineClipItem* LightTrackScene::ClipAt(const QPointF& pos) const
{
    for(QGraphicsItem* item : items(pos))
    {
        if(auto* clip = dynamic_cast<TimelineClipItem*>(item))
        {
            return clip;
        }
    }
    return nullptr;
}

int LightTrackScene::LaneAt(const QPointF& pos) const
{
    for(int i = 0; i < lanes.size(); i++)
    {
        const QRectF& rect = lanes[i].rect;
        if(rect.isValid()
            && pos.y() >= rect.top()
            && pos.y() < rect.bottom())
        {
            return i;
        }
    }
    return -1;
}

qreal LightTrackScene::ClipY(int lane) const
{
    return lanes[lane].rect.top()
        + (lanes[lane].rect.height() - CLIP_HEIGHT) / 2.0;
}

qreal LightTrackScene::SnapClipX(
    qreal x,
    qreal width,
    const TimelineClipItem* ignored_clip) const
{
    return x + SnapDelta(
        {x, x + width},
        {ignored_clip});
}

qreal LightTrackScene::SnapEdgeX(
    qreal x,
    const TimelineClipItem* ignored_clip) const
{
    return x + SnapDelta({x}, {ignored_clip});
}

qreal LightTrackScene::SnapDelta(
    const QVector<qreal>& moving_edges,
    const QSet<const TimelineClipItem*>& ignored_clips) const
{
    if(!snapping_enabled || moving_edges.empty())
    {
        return 0.0;
    }

    qreal best_delta = 0.0;
    qreal best_distance = SNAP_DISTANCE + 1.0;
    const auto consider_target =
        [&best_delta, &best_distance](
            qreal moving_edge,
            qreal target)
        {
            const qreal delta = target - moving_edge;
            const qreal distance = qAbs(delta);
            if(distance <= SNAP_DISTANCE
                && distance < best_distance)
            {
                best_delta = delta;
                best_distance = distance;
            }
        };

    for(const TimelineClipItem* clip : clips)
    {
        if(ignored_clips.contains(clip) || !clip->isVisible())
        {
            continue;
        }

        const qreal clip_left = clip->pos().x();
        const qreal clip_right = clip_left + clip->ClipWidth();
        for(qreal moving_edge : moving_edges)
        {
            consider_target(moving_edge, clip_left);
            consider_target(moving_edge, clip_right);
        }
    }

    if(best_distance <= SNAP_DISTANCE)
    {
        return best_delta;
    }

    const qint64 interval_ms =
        TimelineTickIntervalMs(pixels_per_second);
    const qreal interval_width =
        interval_ms * pixels_per_second / 1000.0;
    if(interval_width <= 0.0)
    {
        return 0.0;
    }

    for(qreal moving_edge : moving_edges)
    {
        const qreal target =
            qRound64(moving_edge / interval_width)
            * interval_width;
        consider_target(moving_edge, target);
    }
    return best_distance <= SNAP_DISTANCE
        ? best_delta
        : 0.0;
}

qreal LightTrackScene::MusicPixelWidth() const
{
    if(music_duration_ms <= 0)
    {
        return 0.0;
    }
    return music_duration_ms * pixels_per_second / 1000.0;
}

void LightTrackScene::AddMusicItems()
{
    const qreal music_width = MusicPixelWidth();
    if(!music_spectrum.empty() && music_width > 0.0)
    {
        MusicSpectrumItem* item = new MusicSpectrumItem();
        addItem(item);
        Track(item);
        item->SetSpectrum(
            music_spectrum,
            music_width,
            MUSIC_ROW_HEIGHT);
        item->SetPeaks(music_peaks);
        item->SetStyle(waveform_style);
        music_item = item;
    }

    if(music_duration_ms > 0)
    {
        playhead_item = Track(addLine(
            0.0,
            0.0,
            0.0,
            sceneRect().height(),
            QPen(QColor(255, 255, 255, 220), 2.0)));
        playhead_item->setZValue(95.0);
        playhead_handle = Track(addEllipse(
            -PLAYHEAD_HANDLE_RADIUS,
            MUSIC_ROW_HEIGHT / 2.0 - PLAYHEAD_HANDLE_RADIUS,
            PLAYHEAD_HANDLE_RADIUS * 2.0,
            PLAYHEAD_HANDLE_RADIUS * 2.0,
            QPen(QColor(255, 255, 255, 235), 2.0),
            QBrush(QColor(64, 188, 255))));
        playhead_handle->setZValue(100.0);
        UpdatePlayhead();
    }
}

void LightTrackScene::UpdatePlayhead()
{
    if(playhead_item == nullptr)
    {
        return;
    }

    const qreal x = qBound<qreal>(
        0.0,
        music_position_ms * pixels_per_second / 1000.0,
        sceneRect().right());
    playhead_item->setLine(
        x,
        0.0,
        x,
        sceneRect().height());
    if(music_item != nullptr)
    {
        music_item->SetPlayedWidth(x - music_item->pos().x());
    }

    if(playhead_handle != nullptr)
    {
        playhead_handle->setRect(
            x - PLAYHEAD_HANDLE_RADIUS,
            MUSIC_ROW_HEIGHT / 2.0 - PLAYHEAD_HANDLE_RADIUS,
            PLAYHEAD_HANDLE_RADIUS * 2.0,
            PLAYHEAD_HANDLE_RADIUS * 2.0);
    }
}

bool LightTrackScene::IsPlayheadHandle(const QPointF& pos) const
{
    return music_duration_ms > 0
        && playhead_handle != nullptr
        && playhead_handle->rect()
            .adjusted(-4.0, -4.0, 4.0, 4.0)
            .contains(pos);
}

void LightTrackScene::SeekMusicAt(qreal x)
{
    if(music_duration_ms <= 0)
    {
        return;
    }

    const qint64 position_ms = qBound<qint64>(
        0,
        static_cast<qint64>(
            x * 1000.0 / pixels_per_second),
        music_duration_ms);
    SetMusicPosition(position_ms);
    if(music_seek_callback)
    {
        music_seek_callback(position_ms);
    }
}

void LightTrackScene::UpdateBoxSelection(
    const QPointF& pos,
    const QPoint& screen_pos)
{
    if(!selection_box_visible
        && (screen_pos - drag_screen_start).manhattanLength()
            < QApplication::startDragDistance())
    {
        return;
    }

    const QRectF previous_rect = selection_rect;
    selection_rect = QRectF(drag_scene_start, pos).normalized()
        .intersected(sceneRect());
    selection_box_visible = true;
    update(previous_rect.united(selection_rect).adjusted(-2.0, -2.0, 2.0, 2.0));

    bool selection_changed = false;
    TimelineClipItem* selected_clip = nullptr;
    for(TimelineClipItem* clip : clips)
    {
        const bool selected = clip->isVisible()
            && (selection_before_drag.contains(clip)
                || selection_rect.intersects(clip->sceneBoundingRect()));
        if(clip->isSelected() != selected)
        {
            clip->setSelected(selected);
            selection_changed = true;
        }
        if(selected)
        {
            selected_clip = clip;
        }
    }

    if(selection_changed)
    {
        NotifyClipSelected(selected_clip);
    }
}

void LightTrackScene::UpdateClipMove(const QPointF& pos, const QPoint& screen_pos)
{
    if(active_clip == nullptr || moving_clips.empty())
    {
        return;
    }

    if(!clip_move_started)
    {
        if((screen_pos - drag_screen_start).manhattanLength()
            < QApplication::startDragDistance())
        {
            return;
        }
        clip_move_started = true;
        for(const ClipMoveStart& start : moving_clips)
        {
            start.clip->setOpacity(0.88);
            start.clip->setZValue(80.0);
        }
        active_clip->setZValue(81.0);
        active_clip->setCursor(Qt::ClosedHandCursor);
    }

    qreal leftmost_x = moving_clips.front().x;
    int first_row = moving_clips.front().visible_row;
    int last_row = first_row;
    for(const ClipMoveStart& start : moving_clips)
    {
        leftmost_x = qMin(leftmost_x, start.x);
        first_row = qMin(first_row, start.visible_row);
        last_row = qMax(last_row, start.visible_row);
    }

    // Clamp and snap one common delta to preserve the entire arrangement.
    qreal delta_x = qMax(-leftmost_x, pos.x() - drag_scene_start.x());
    QVector<qreal> moving_edges;
    moving_edges.reserve(moving_clips.size() * 2);
    for(const ClipMoveStart& start : moving_clips)
    {
        moving_edges.push_back(start.x + delta_x);
        moving_edges.push_back(start.x + delta_x + start.clip->ClipWidth());
    }
    delta_x = qMax(-leftmost_x,
        delta_x + SnapDelta(moving_edges, moving_clip_items));

    const int target_lane = LaneAt(pos);
    const int target_row = target_lane >= 0
        ? static_cast<int>(visible_lane_indices.indexOf(target_lane))
        : (pos.y() < MUSIC_ROW_HEIGHT ? 0 : static_cast<int>(visible_lane_indices.size()) - 1);
    const int active_start_row =
        static_cast<int>(visible_lane_indices.indexOf(clip_start_lane));
    const int row_delta = qBound(-first_row,
        target_row - active_start_row,
        static_cast<int>(visible_lane_indices.size()) - 1 - last_row);

    qreal rightmost_x = 0.0;
    for(const ClipMoveStart& start : moving_clips)
    {
        const int lane = visible_lane_indices[start.visible_row + row_delta];
        const qreal x = start.x + delta_x;
        start.clip->SetLaneIndex(lane);
        start.clip->setPos(x, ClipY(lane));
        rightmost_x = qMax(rightmost_x, x + start.clip->boundingRect().width());
    }
    EnsureContentWidth(rightmost_x);
    ShowPreview(
        active_clip->LaneIndex(),
        active_clip->pos().x(),
        active_clip->ClipWidth(),
        active_clip->Effect());
}

void LightTrackScene::UpdateClipResize(const QPointF& pos)
{
    if(active_clip == nullptr)
    {
        return;
    }

    const int lane = active_clip->LaneIndex();
    const qreal minimum_width =
        CLIP_MIN_DURATION_MS * pixels_per_second / 1000.0;
    qreal x = clip_start_x;
    qreal width = clip_start_width;

    if(drag_mode == ClipResizeLeft)
    {
        const qreal right = clip_start_x + clip_start_width;
        const qreal unsnapped_x = qBound(
            lanes[lane].rect.left(),
            clip_start_x + pos.x() - drag_scene_start.x(),
            right - minimum_width);
        x = qBound(
            lanes[lane].rect.left(),
            SnapEdgeX(unsnapped_x, active_clip),
            right - minimum_width);
        width = right - x;
    }
    else
    {
        const qreal minimum_right =
            clip_start_x + minimum_width;
        const qreal unsnapped_right = qMax(
            minimum_right,
            clip_start_x
                + clip_start_width
                + pos.x()
                - drag_scene_start.x());
        const qreal right = qMax(
            minimum_right,
            SnapEdgeX(unsnapped_right, active_clip));
        width = right - clip_start_x;
    }

    active_clip->SetClipWidth(width);
    active_clip->setPos(x, ClipY(lane));
    ShowPreview(lane, x, width, active_clip->Effect());
}

void LightTrackScene::ShowPreview(
    int lane,
    qreal x,
    qreal width,
    const EffectDescriptor& effect)
{
    if(preview == nullptr)
    {
        preview = new ClipPreviewItem();
        addItem(preview);
    }

    preview->SetPreview(width, effect);
    preview->setPos(x, ClipY(lane));
    preview->setVisible(true);
    last_preview_lane = lane;
    last_preview_x = x;
    last_preview_width = width;
    last_preview_effect = effect;
    EnsureContentWidth(x + qMax(CLIP_MIN_DISPLAY_WIDTH, width));
    RefreshInheritedClips();
}

void LightTrackScene::HidePreview()
{
    if(preview != nullptr)
    {
        preview->setVisible(false);
    }

    last_preview_lane = -1;
    last_preview_x = 0.0;
    last_preview_width = CLIP_DEFAULT_WIDTH;
    RefreshInheritedClips();
}

TimelineClipItem* LightTrackScene::AddClip(
    const EffectDescriptor& effect,
    int lane,
    qreal x,
    qreal width,
    bool select,
    bool notify_change,
    ClipId requested_id)
{
    if(lane < 0 || lane >= lanes.size())
    {
        return nullptr;
    }

    const ClipId id = AllocateClipId(requested_id);
    if(!id.IsValid())
    {
        return nullptr;
    }

    TimelineClipItem* clip =
        new TimelineClipItem(id, effect);
    clip->SetLaneIndex(lane);
    clip->SetClipWidth(width);
    if(lanes[lane].rect.isValid()
        && !lanes[lane].rect.isEmpty())
    {
        clip->setPos(
            qMax<qreal>(0.0, x),
            ClipY(lane));
    }
    else
    {
        clip->setPos(qMax<qreal>(0.0, x), 0.0);
        clip->setVisible(false);
    }
    addItem(clip);
    clips.push_back(clip);
    EnsureContentWidth(clip->pos().x() + clip->boundingRect().width());

    if(select)
    {
        clearSelection();
        clip->setSelected(true);
        NotifyClipSelected(clip);
    }

    RefreshInheritedClips();
    if(notify_change)
    {
        NotifyTimelineChanged();
    }
    return clip;
}

qreal LightTrackScene::DefaultClipWidth() const
{
    return CLIP_DEFAULT_WIDTH * pixels_per_second / GRID_WIDTH;
}

void LightTrackScene::RemoveClip(TimelineClipItem* clip)
{
    if(active_clip == clip || moving_clip_items.contains(clip))
    {
        CancelDrag();
    }

    clips.removeOne(clip);
    if(clip_removed_callback)
    {
        clip_removed_callback(clip->Id());
    }

    removeItem(clip);
    delete clip;
    NotifyClipSelected(nullptr);
    RefreshInheritedClips();
}

void LightTrackScene::NotifyClipSelected(TimelineClipItem* clip)
{
    if(clip_selected_callback)
    {
        clip_selected_callback(
            clip == nullptr
                ? std::optional<ClipId>{}
                : std::optional<ClipId>{clip->Id()});
    }
}

void LightTrackScene::NotifyTimelineChanged()
{
    if(timeline_changed_callback)
    {
        timeline_changed_callback();
    }
}

void LightTrackScene::CancelDrag(bool restore_positions)
{
    if(selection_box_visible)
    {
        update(selection_rect.adjusted(-2.0, -2.0, 2.0, 2.0));
    }
    selection_rect = QRectF();
    selection_before_drag.clear();
    selection_box_visible = false;

    for(const ClipMoveStart& start : moving_clips)
    {
        if(restore_positions)
        {
            start.clip->SetLaneIndex(start.lane);
            start.clip->setPos(start.x, ClipY(start.lane));
        }
        start.clip->setOpacity(1.0);
        start.clip->setZValue(35.0);
    }
    moving_clips.clear();
    moving_clip_items.clear();
    pending_clip_click = KeepClipSelection;
    clip_move_started = false;

    if(active_clip != nullptr)
    {
        if(restore_positions
            && (drag_mode == ClipResizeLeft || drag_mode == ClipResizeRight))
        {
            active_clip->SetClipWidth(clip_start_width);
            active_clip->setPos(clip_start_x, ClipY(clip_start_lane));
        }
        active_clip->setOpacity(1.0);
        active_clip->setZValue(35.0);
        active_clip->setCursor(Qt::OpenHandCursor);
        active_clip = nullptr;
    }

    HidePreview();
    drag_mode = NoDrag;
}

namespace
{
constexpr ushort HIDE_ZERO_LED_ICON = 0xE0BB;  // eye-off
constexpr ushort COLLAPSE_ALL_ICON = 0xE228;   // chevrons-down-up
constexpr ushort EXPAND_ALL_ICON = 0xE211;     // chevrons-up-down
constexpr ushort CHOOSE_DEVICES_ICON = 0xE460; // list-filter

QPushButton* CreateDeviceHeaderButton(
    QWidget* parent,
    const QString& object_name,
    ushort icon,
    const QString& tool_tip)
{
    QPushButton* button =
        new QPushButton(QString(QChar(icon)), parent);
    button->setObjectName(object_name);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(tool_tip);
    button->setStyleSheet(QStringLiteral(R"(
        QPushButton#%1 {
            background-color: transparent;
            border: 1px solid transparent;
            border-radius: 5px;
            padding: 0;
        }

        QPushButton#%1:hover {
            background-color: rgba(127, 127, 127, 45);
        }

        QPushButton#%1:pressed {
            background-color: rgba(127, 127, 127, 80);
        }

        QPushButton#%1:checked {
            background-color: rgba(64, 188, 255, 70);
            border-color: rgba(64, 188, 255, 180);
        }

        QPushButton#%1:checked:hover {
            background-color: rgba(64, 188, 255, 95);
        }
    )").arg(object_name));
    QFont icon_font(lighttrack::ui::LucideFontFamily());
    icon_font.setPixelSize(15);
    button->setFont(icon_font);
    return button;
}
}

TimelineRulerWidget::TimelineRulerWidget(QWidget* parent) :
    QWidget(parent)
{
    setFixedHeight(static_cast<int>(RULER_HEIGHT));

    choose_devices_button = CreateDeviceHeaderButton(
        this,
        QStringLiteral("chooseDevicesButton"),
        CHOOSE_DEVICES_ICON,
        QStringLiteral("Choose which devices to show"));
    connect(
        choose_devices_button,
        &QPushButton::clicked,
        this,
        [this]()
        {
            if(choose_devices_requested_callback)
            {
                choose_devices_requested_callback();
            }
        });

    expand_all_button = CreateDeviceHeaderButton(
        this,
        QStringLiteral("expandAllDevicesButton"),
        COLLAPSE_ALL_ICON,
        QString());
    SetDevicesExpanded(true);
    connect(
        expand_all_button,
        &QPushButton::clicked,
        this,
        [this]()
        {
            if(expand_all_requested_callback)
            {
                expand_all_requested_callback(!devices_expanded);
            }
        });

    // Lane filters: a menu of checkable options; the button stays
    // highlighted while any filter is on.
    hide_zero_led_zones_button = CreateDeviceHeaderButton(
        this,
        QStringLiteral("hideZeroLedZonesButton"),
        HIDE_ZERO_LED_ICON,
        QStringLiteral("Hide zones"));
    hide_zero_led_zones_button->setCheckable(true);
    lane_filter_menu = new QMenu(hide_zero_led_zones_button);
    hide_zero_led_action = lane_filter_menu->addAction(
        QStringLiteral("Hide zones with zero LEDs"));
    hide_zero_led_action->setCheckable(true);
    hide_disabled_action = lane_filter_menu->addAction(
        QStringLiteral("Hide disabled zones"));
    hide_disabled_action->setCheckable(true);

    connect(
        hide_zero_led_action,
        &QAction::toggled,
        this,
        [this](bool checked)
        {
            UpdateLaneFilterButton();
            if(hide_zero_led_zones_changed_callback)
            {
                hide_zero_led_zones_changed_callback(checked);
            }
        });
    connect(
        hide_disabled_action,
        &QAction::toggled,
        this,
        [this](bool checked)
        {
            UpdateLaneFilterButton();
            if(hide_disabled_lanes_changed_callback)
            {
                hide_disabled_lanes_changed_callback(checked);
            }
        });
    connect(
        hide_zero_led_zones_button,
        &QPushButton::clicked,
        this,
        [this]()
        {
            // The highlight reflects the filters, not the click.
            UpdateLaneFilterButton();
            lane_filter_menu->popup(
                hide_zero_led_zones_button->mapToGlobal(
                    QPoint(0, hide_zero_led_zones_button->height())));
        });
    UpdateDeviceButtonGeometry();
}

void TimelineRulerWidget::SetLabelWidth(qreal width)
{
    const qreal bounded_width = qMax<qreal>(0.0, width);
    if(qFuzzyCompare(label_width, bounded_width))
    {
        return;
    }
    label_width = bounded_width;
    UpdateDeviceButtonGeometry();
    update();
}

void TimelineRulerWidget::SetContentWidth(qreal width)
{
    if(qFuzzyCompare(content_width, width))
    {
        return;
    }
    content_width = width;
    update();
}

void TimelineRulerWidget::SetPixelsPerSecond(qreal value)
{
    if(qFuzzyCompare(pixels_per_second, value))
    {
        return;
    }
    pixels_per_second = qBound(
        TIMELINE_ZOOM_MIN,
        value,
        TIMELINE_ZOOM_MAX);
    update();
}

void TimelineRulerWidget::SetScrollOffset(int offset)
{
    if(horizontal_offset == offset)
    {
        return;
    }
    horizontal_offset = offset;
    update();
}

void TimelineRulerWidget::SetHideZeroLedZonesChangedCallback(
    std::function<void(bool)> callback)
{
    hide_zero_led_zones_changed_callback = std::move(callback);
}

void TimelineRulerWidget::SetHideDisabledLanesChangedCallback(
    std::function<void(bool)> callback)
{
    hide_disabled_lanes_changed_callback = std::move(callback);
}

void TimelineRulerWidget::UpdateLaneFilterButton()
{
    const bool any = hide_zero_led_action->isChecked()
        || hide_disabled_action->isChecked();
    hide_zero_led_zones_button->setChecked(any);
    QStringList active;
    if(hide_zero_led_action->isChecked())
    {
        active << QStringLiteral("zero-LED zones");
    }
    if(hide_disabled_action->isChecked())
    {
        active << QStringLiteral("disabled zones");
    }
    hide_zero_led_zones_button->setToolTip(
        active.isEmpty()
            ? QStringLiteral("Hide zones")
            : QStringLiteral("Hiding: %1").arg(active.join(", ")));
}

void TimelineRulerWidget::SetChooseDevicesRequestedCallback(
    std::function<void()> callback)
{
    choose_devices_requested_callback = std::move(callback);
}

void TimelineRulerWidget::SetExpandAllRequestedCallback(
    std::function<void(bool)> callback)
{
    expand_all_requested_callback = std::move(callback);
}

void TimelineRulerWidget::SetDevicesExpanded(bool expanded)
{
    devices_expanded = expanded;
    expand_all_button->setText(
        QString(QChar(
            expanded ? COLLAPSE_ALL_ICON : EXPAND_ALL_ICON)));
    expand_all_button->setToolTip(
        expanded
            ? QStringLiteral("Collapse all devices")
            : QStringLiteral("Expand all devices"));
}

void TimelineRulerWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    UpdateDeviceButtonGeometry();
}

void TimelineRulerWidget::UpdateDeviceButtonGeometry()
{
    if(hide_zero_led_zones_button == nullptr
        || expand_all_button == nullptr
        || choose_devices_button == nullptr)
    {
        return;
    }

    constexpr int button_size = 24;
    constexpr int button_spacing = 2;
    constexpr int right_margin = 5;
    const int visible_label_width = qRound(
        qMin<qreal>(label_width, width()));
    const int top = (height() - button_size) / 2;
    const int hide_x = qMax(
        right_margin,
        visible_label_width
            - button_size
            - right_margin);
    hide_zero_led_zones_button->setGeometry(
        hide_x,
        top,
        button_size,
        button_size);
    const int expand_x = qMax(
        right_margin,
        hide_x - button_spacing - button_size);
    expand_all_button->setGeometry(
        expand_x,
        top,
        button_size,
        button_size);
    choose_devices_button->setGeometry(
        qMax(
            right_margin,
            expand_x - button_spacing - button_size),
        top,
        button_size,
        button_size);
}

void TimelineRulerWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QColor line_color =
        TextLineColor(palette(), 90, 120);
    const QColor text_color =
        TextLineColor(palette(), 160, 180);
    const qreal visible_label_width =
        qMin<qreal>(label_width, width());
    const qreal timeline_width =
        qMax<qreal>(0.0, width() - visible_label_width);
    const QRectF frame_rect(
        0.5,
        0.5,
        qMax(0, width() - 1),
        qMax(0, height() - 1));

    painter.setPen(QPen(line_color));
    painter.drawRect(frame_rect);
    painter.drawLine(
        QPointF(visible_label_width - 0.5, 0.0),
        QPointF(visible_label_width - 0.5, height()));

    QFont label_font = painter.font();
    label_font.setBold(true);
    painter.setFont(label_font);
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(
        QRectF(
            8.0,
            0.0,
            qMax<qreal>(
                0.0,
                visible_label_width - 100.0),
            height()),
        Qt::AlignVCenter | Qt::AlignLeft,
        QStringLiteral("Device"));

    painter.save();
    painter.setClipRect(
        QRectF(
            visible_label_width,
            0.0,
            timeline_width,
            height()));
    painter.setFont(font());

    const qint64 tick_interval_ms =
        TimelineTickIntervalMs(pixels_per_second);
    const qreal tick_width =
        tick_interval_ms * pixels_per_second / 1000.0;
    const qint64 first_tick = qMax<qint64>(
        0,
        static_cast<qint64>(
            std::floor(
                static_cast<qreal>(horizontal_offset)
                / tick_width)));
    const qint64 last_tick = qMin(
        static_cast<qint64>(
            std::floor(content_width / tick_width)),
        static_cast<qint64>(
            std::ceil(
                (horizontal_offset + timeline_width)
                / tick_width)));

    for(qint64 tick = first_tick; tick <= last_tick; tick++)
    {
        const qreal x = tick * tick_width;
        const qreal view_x =
            visible_label_width + x - horizontal_offset;

        painter.setPen(QPen(line_color));
        painter.drawLine(
            QPointF(view_x, 0.0),
            QPointF(view_x, height()));
        painter.setPen(text_color);
        painter.drawText(
            QRectF(
                view_x + 6.0,
                0.0,
                qMax<qreal>(0.0, tick_width - 12.0),
                height()),
            Qt::AlignVCenter | Qt::AlignLeft,
            FormatTimelineTime(
                tick * tick_interval_ms,
                tick_interval_ms));
    }

    painter.restore();
}
}
