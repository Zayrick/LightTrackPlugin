#include "ui/pages/LightTrackPagePrivate.h"

#include "core/effects/EffectDescriptor.h"
#include "ui/timeline/TimelineEditor.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>

#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using lighttrack::ClipId;
using lighttrack::EffectDescriptor;
using lighttrack::openrgb::OpenRgbTimelineBackend;

std::string ToUtf8String(const QString& value)
{
    const QByteArray utf8 = value.toUtf8();
    return std::string(
        utf8.constData(),
        static_cast<std::size_t>(utf8.size()));
}

struct PreparedClipState
{
    ClipId id;
    EffectDescriptor definition;
    int lane = -1;
    qint64 start_ms = 0;
    qint64 end_ms = 0;
    OpenRgbTimelineBackend::EffectPtr effect;
};
}

namespace lighttrack::ui
{
void LightTrackPage::PrepareForDeviceReload()
{
    backend.PrepareForDeviceReload();
}

void LightTrackPage::ReloadDevices()
{
    LayoutSnapshot preserved_layout;
    bool restore_layout = false;
    if(history.IsInitialized() && !applying_snapshot)
    {
        FlushPendingHistory();
        preserved_layout = history.Current();
        restore_layout = true;
    }

    const OpenRgbTimelineBackend::DeviceSnapshot devices =
        backend.ReloadDevices();
    timeline_editor->SetLanes(
        devices.lanes,
        devices.empty_message);

    if(!restore_layout)
    {
        return;
    }

    QString error;
    QStringList warnings;
    if(ApplyLayoutState(
        preserved_layout,
        error,
        warnings))
    {
        history.ReplaceCurrent(std::move(preserved_layout));
        UpdateHistoryButtons();
    }
    else
    {
        InitializeHistory();
    }
}

void LightTrackPage::InitializeHistory()
{
    QString error;
    LayoutSnapshot snapshot;
    if(TryCaptureLayoutState(snapshot, error))
    {
        history.Reset(std::move(snapshot));
    }
    else
    {
        history.Clear();
    }
    UpdateHistoryButtons();
}

void LightTrackPage::FlushPendingHistory()
{
    if(history_commit_timer->isActive())
    {
        history_commit_timer->stop();
    }
    CommitHistorySnapshot();
}

void LightTrackPage::CommitHistorySnapshot()
{
    if(applying_snapshot || !history.IsInitialized())
    {
        return;
    }

    QString error;
    LayoutSnapshot snapshot;
    if(!TryCaptureLayoutState(snapshot, error))
    {
        return;
    }

    if(history.Commit(std::move(snapshot)))
    {
        UpdateHistoryButtons();
    }
}

void LightTrackPage::UpdateHistoryButtons()
{
    toolbar_undo_button->setEnabled(history.CanUndo());
    toolbar_redo_button->setEnabled(history.CanRedo());
}

void LightTrackPage::Undo()
{
    FlushPendingHistory();
    const LayoutSnapshot* target = history.UndoTarget();
    if(target == nullptr)
    {
        return;
    }

    QString error;
    QStringList warnings;
    if(!ApplyLayoutState(*target, error, warnings))
    {
        QMessageBox::critical(this, "Cannot undo", error);
        return;
    }

    history.AcceptUndo();
    UpdateHistoryButtons();
    if(!warnings.empty())
    {
        QMessageBox::warning(
            this,
            "Undo completed with warnings",
            warnings.join("\n"));
    }
}

void LightTrackPage::Redo()
{
    FlushPendingHistory();
    const LayoutSnapshot* target = history.RedoTarget();
    if(target == nullptr)
    {
        return;
    }

    QString error;
    QStringList warnings;
    if(!ApplyLayoutState(*target, error, warnings))
    {
        QMessageBox::critical(this, "Cannot redo", error);
        return;
    }

    history.AcceptRedo();
    UpdateHistoryButtons();
    if(!warnings.empty())
    {
        QMessageBox::warning(
            this,
            "Redo completed with warnings",
            warnings.join("\n"));
    }
}

LayoutSnapshot LightTrackPage::CaptureLayoutState()
{
    LayoutSnapshot layout;
    layout.music.path = music_path;
    layout.music.duration_ms = music_duration_ms;
    layout.lanes = backend.CaptureLaneStates();

    const QVector<TimelineClip> clips =
        timeline_editor->PersistentTimelineClips();
    layout.clips.reserve(clips.size());
    for(const TimelineClip& clip : clips)
    {
        QString backend_error;
        if(!backend.EnsureEffect(
            clip.id,
            clip.effect_id,
            backend_error))
        {
            throw std::runtime_error(
                ToUtf8String(
                    backend_error.isEmpty()
                    ? QString("Cannot serialize effect \"%1\"")
                        .arg(clip.effect_id)
                    : backend_error));
        }

        layout.clips.push_back({
            clip.id,
            clip.effect_id,
            backend.SerializeLane(clip.lane_index),
            clip.start_ms,
            clip.end_ms,
            backend.ExportClipSettings(clip.id)
        });
    }
    return layout;
}

bool LightTrackPage::TryCaptureLayoutState(
    LayoutSnapshot& snapshot,
    QString& error)
{
    try
    {
        snapshot = CaptureLayoutState();
        return true;
    }
    catch(const std::exception& exception)
    {
        error = QString(
            "Could not serialize the current layout: %1")
            .arg(QString::fromUtf8(exception.what()));
    }
    return false;
}

bool LightTrackPage::ApplyLayoutState(
    const LayoutSnapshot& layout,
    QString& error,
    QStringList& warnings)
{
    bool commit_started = false;
    const auto clear_partial_commit =
        [this, &commit_started]()
        {
            if(!commit_started)
            {
                return;
            }

            backend.ClearClips();
            replacing_layout_effects = true;
            timeline_editor->ClearTimelineClips();
            replacing_layout_effects = false;
            ClearAllClipSettings();
            settings_stack->setCurrentWidget(
                settings_placeholder);
        };

    warnings.clear();

    try
    {
        const QString saved_music_path = layout.music.path;
        const qint64 saved_music_duration =
            qMax<qint64>(0, layout.music.duration_ms);
        qint64 restored_timeline_duration_ms =
            saved_music_duration;

        std::vector<PreparedClipState> prepared_clips;
        prepared_clips.reserve(
            static_cast<std::size_t>(layout.clips.size()));
        std::set<quint64> serialized_clip_ids;
        int clip_number = 0;

        for(const LayoutClipSnapshot& clip : layout.clips)
        {
            ++clip_number;
            if(clip.id.IsValid()
                && !serialized_clip_ids
                    .insert(clip.id.Value()).second)
            {
                throw std::runtime_error(
                    ToUtf8String(
                        QString(
                            "Timeline clip %1 has a duplicate clipId")
                            .arg(clip_number)));
            }

            EffectDescriptor definition;
            if(!backend.FindEffect(
                clip.effect_id,
                definition))
            {
                warnings.push_back(
                    QString(
                        "Skipped clip %1: effect \"%2\" is unavailable.")
                        .arg(clip_number)
                        .arg(clip.effect_id));
                continue;
            }

            const int lane =
                backend.ResolveLane(clip.serialized_lane);
            if(lane < 0)
            {
                warnings.push_back(
                    QString(
                        "Skipped clip %1 (%2): its device or zone is unavailable.")
                        .arg(clip_number)
                        .arg(definition.name));
                continue;
            }

            if(clip.start_ms < 0
                || clip.end_ms <= clip.start_ms)
            {
                throw std::runtime_error(
                    ToUtf8String(
                        QString(
                            "Timeline clip %1 has an invalid time range")
                            .arg(clip_number)));
            }
            restored_timeline_duration_ms = qMax(
                restored_timeline_duration_ms,
                clip.end_ms);

            QString backend_error;
            OpenRgbTimelineBackend::EffectPtr runtime_effect =
                backend.CreateEffect(
                    clip.effect_id,
                    backend_error);
            if(runtime_effect == nullptr)
            {
                throw std::runtime_error(
                    ToUtf8String(
                        backend_error.isEmpty()
                        ? QString("Could not create effect \"%1\"")
                            .arg(definition.name)
                        : backend_error));
            }

            if(!backend.ImportEffectSettings(
                *runtime_effect,
                clip.serialized_effect_settings,
                backend_error))
            {
                throw std::runtime_error(
                    ToUtf8String(
                        QString(
                            "Could not restore clip %1 (%2): %3")
                            .arg(clip_number)
                            .arg(definition.name)
                            .arg(
                                backend_error.isEmpty()
                                ? QStringLiteral(
                                    "Invalid effect settings")
                                : backend_error)));
            }

            prepared_clips.push_back({
                clip.id,
                definition,
                lane,
                clip.start_ms,
                clip.end_ms,
                std::move(runtime_effect)
            });
        }

        // Legacy version-1 snapshots did not persist clip IDs.
        quint64 generated_id = 1;
        for(PreparedClipState& prepared : prepared_clips)
        {
            if(prepared.id.IsValid())
            {
                continue;
            }

            while(generated_id == 0
                || serialized_clip_ids.find(generated_id)
                    != serialized_clip_ids.end())
            {
                ++generated_id;
            }
            prepared.id = ClipId(generated_id);
            serialized_clip_ids.insert(generated_id);
            ++generated_id;
        }

        history_commit_timer->stop();

        std::vector<OpenRgbTimelineBackend::ClipEffect>
            replacement_effects;
        replacement_effects.reserve(prepared_clips.size());
        for(PreparedClipState& prepared : prepared_clips)
        {
            replacement_effects.push_back({
                prepared.id,
                std::move(prepared.effect)
            });
        }

        // Validation and effect construction are complete before commit.
        applying_snapshot = true;
        QString replacement_error;
        if(!backend.ReplaceEffects(
            std::move(replacement_effects),
            replacement_error))
        {
            throw std::runtime_error(
                ToUtf8String(
                    replacement_error.isEmpty()
                    ? QStringLiteral(
                        "Could not replace timeline effects")
                    : replacement_error));
        }

        commit_started = true;
        const OpenRgbTimelineBackend::DeviceSnapshot devices =
            backend.RestoreLaneStates(layout.lanes, warnings);
        replacing_layout_effects = true;
        timeline_editor->SetLanes(devices.lanes, devices.empty_message);
        replacing_layout_effects = false;
        timeline_editor->SetMinimumTimelineDuration(
            restored_timeline_duration_ms);
        const bool music_available = SetMusicFile(
            saved_music_path,
            saved_music_duration);

        if(!saved_music_path.isEmpty()
            && !music_available)
        {
            warnings.push_back(
                QString(
                    "The music file could not be opened: %1")
                    .arg(saved_music_path));
        }

        for(const PreparedClipState& prepared : prepared_clips)
        {
            const ClipId clip_id = timeline_editor->RestoreClip(
                prepared.definition.id,
                prepared.lane,
                prepared.start_ms,
                prepared.end_ms,
                prepared.id);
            if(clip_id != prepared.id)
            {
                throw std::runtime_error(
                    "Could not restore a timeline clip");
            }
        }

        for(const PreparedClipState& prepared : prepared_clips)
        {
            CreateClipSettings(prepared.id);
        }

        settings_stack->setCurrentWidget(
            settings_placeholder);
        applying_snapshot = false;
        return true;
    }
    catch(const std::exception& exception)
    {
        clear_partial_commit();
        applying_snapshot = false;
        replacing_layout_effects = false;
        error = QString::fromUtf8(exception.what());
    }
    return false;
}

void LightTrackPage::NewLayout()
{
    QString error;
    LayoutSnapshot current_layout;
    const bool saved_current_layout = persisted_layout.has_value()
        && TryCaptureLayoutState(current_layout, error)
        && current_layout == *persisted_layout;
    if(!saved_current_layout)
    {
        QMessageBox prompt(
            QMessageBox::Warning,
            "New layout",
            current_layout_path.isEmpty()
                ? "The current layout has not been saved."
                : "The current layout has unsaved changes.",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            this);
        prompt.setInformativeText(
            "Save the current layout before creating a new one?");
        prompt.setDefaultButton(QMessageBox::Save);
        prompt.setEscapeButton(QMessageBox::Cancel);
        const int choice = prompt.exec();
        if(choice == QMessageBox::Save)
        {
            if(!SaveLayout())
            {
                return;
            }
        }
        else if(choice != QMessageBox::Discard)
        {
            return;
        }
    }

    QStringList warnings;
    if(!ApplyLayoutState({}, error, warnings))
    {
        QMessageBox::critical(this, "Cannot create layout", error);
        return;
    }

    const OpenRgbTimelineBackend::DeviceSnapshot devices =
        backend.ResetLaneStates();
    timeline_editor->SetLanes(devices.lanes, devices.empty_message);
    settings_placeholder->setText(
        "Select an effect card on the timeline to configure it.");
    current_layout_path.clear();
    persisted_layout.reset();
    toolbar_save_button->setToolTip("Save layout (Ctrl+S)");
    InitializeHistory();
}

bool LightTrackPage::SaveLayout()
{
    FlushPendingHistory();

    QString error;
    LayoutSnapshot layout;
    if(!TryCaptureLayoutState(layout, error))
    {
        QMessageBox::critical(
            this,
            "Cannot save layout",
            error);
        return false;
    }

    const QString suggested_path =
        current_layout_path.isEmpty()
        ? QStringLiteral("lighttrack-layout.lighttrack")
        : current_layout_path;
    QString path = QFileDialog::getSaveFileName(
        this,
        "Save LightTrack layout",
        suggested_path,
        "LightTrack Layout (*.lighttrack);;JSON Files (*.json);;All Files (*.*)");
    if(path.isEmpty())
    {
        return false;
    }
    if(QFileInfo(path).suffix().isEmpty())
    {
        path += ".lighttrack";
    }

    if(!layout_repository.Save(path, layout, error))
    {
        QMessageBox::critical(
            this,
            "Cannot save layout",
            error);
        return false;
    }

    current_layout_path =
        QFileInfo(path).absoluteFilePath();
    persisted_layout = std::move(layout);
    toolbar_save_button->setToolTip(
        QString("Save layout (Ctrl+S)\nLast saved: %1")
            .arg(current_layout_path));
    return true;
}

void LightTrackPage::LoadLayout()
{
    const QString start_path =
        current_layout_path.isEmpty()
        ? QString()
        : QFileInfo(current_layout_path).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Load LightTrack layout",
        start_path,
        "LightTrack Layout (*.lighttrack *.json);;All Files (*.*)");
    if(path.isEmpty())
    {
        return;
    }

    LayoutSnapshot layout;
    QString file_error;
    if(!layout_repository.Load(
            path,
            layout,
            file_error))
    {
        QMessageBox::critical(
            this,
            "Cannot load layout",
            file_error);
        return;
    }

    FlushPendingHistory();
    QString error;
    QStringList warnings;
    if(!ApplyLayoutState(layout, error, warnings))
    {
        QMessageBox::critical(
            this,
            "Cannot load layout",
            error);
        return;
    }

    current_layout_path =
        QFileInfo(path).absoluteFilePath();
    CommitHistorySnapshot();
    // Capture the restored state so legacy IDs and device ordering do not
    // make a freshly loaded layout appear modified.
    LayoutSnapshot restored_layout;
    if(TryCaptureLayoutState(restored_layout, error))
    {
        persisted_layout = std::move(restored_layout);
    }
    else
    {
        persisted_layout.reset();
    }
    toolbar_save_button->setToolTip(
        QString("Save layout (Ctrl+S)\nCurrent file: %1")
            .arg(current_layout_path));
    if(!warnings.empty())
    {
        QMessageBox::warning(
            this,
            "Layout loaded with warnings",
            warnings.join("\n"));
    }
}
}
