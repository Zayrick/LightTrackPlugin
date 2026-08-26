#include "ui/pages/LightTrackPagePrivate.h"

#include "application/LayoutRepository.h"
#include "application/TimelineBackend.h"
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
using lighttrack::TimelineBackend;

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
    TimelineBackend::EffectPtr effect;
};
}

namespace lighttrack::ui
{
void LightTrackPage::PrepareForDeviceReload()
{
    if(backend != nullptr)
    {
        backend->PrepareForDeviceReload();
    }
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

    const TimelineBackend::DeviceSnapshot devices =
        backend != nullptr
        ? backend->ReloadDevices()
        : TimelineBackend::DeviceSnapshot{
            {},
            QStringLiteral("Timeline backend unavailable")
        };
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
        &error,
        &warnings))
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
    if(TryCaptureLayoutState(&snapshot, &error))
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
    if(history_commit_timer != nullptr
        && history_commit_timer->isActive())
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
    if(!TryCaptureLayoutState(&snapshot, &error))
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
    if(toolbar_undo_button != nullptr)
    {
        toolbar_undo_button->setEnabled(history.CanUndo());
    }
    if(toolbar_redo_button != nullptr)
    {
        toolbar_redo_button->setEnabled(history.CanRedo());
    }
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
    if(!ApplyLayoutState(*target, &error, &warnings))
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
    if(!ApplyLayoutState(*target, &error, &warnings))
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

    const QVector<TimelineClip> clips =
        timeline_editor->PersistentTimelineClips();
    layout.clips.reserve(clips.size());
    for(const TimelineClip& clip : clips)
    {
        QString backend_error;
        if(backend == nullptr
            || !backend->EnsureEffect(
                clip.id,
                clip.effect_id,
                &backend_error))
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
            backend->SerializeLane(clip.lane_index),
            clip.start_ms,
            clip.end_ms,
            backend->ExportClipSettings(clip.id)
        });
    }
    return layout;
}

bool LightTrackPage::TryCaptureLayoutState(
    LayoutSnapshot* snapshot,
    QString* error)
{
    try
    {
        if(snapshot != nullptr)
        {
            *snapshot = CaptureLayoutState();
        }
        return true;
    }
    catch(const std::exception& exception)
    {
        if(error != nullptr)
        {
            *error = QString(
                "Could not serialize the current layout: %1")
                .arg(QString::fromUtf8(exception.what()));
        }
    }
    catch(...)
    {
        if(error != nullptr)
        {
            *error =
                "Could not serialize the current layout.";
        }
    }
    return false;
}

bool LightTrackPage::ApplyLayoutState(
    const LayoutSnapshot& layout,
    QString* error,
    QStringList* warnings)
{
    bool commit_started = false;
    const auto clear_partial_commit =
        [this, &commit_started]()
        {
            if(!commit_started)
            {
                return;
            }

            backend->ClearClips();
            replacing_layout_effects = true;
            timeline_editor->ClearTimelineClips();
            replacing_layout_effects = false;
            ClearAllClipSettings();
            settings_stack->setCurrentWidget(
                settings_placeholder);
        };

    if(warnings != nullptr)
    {
        warnings->clear();
    }

    try
    {
        const QString saved_music_path = layout.music.path;
        const qint64 saved_music_duration =
            qMax<qint64>(0, layout.music.duration_ms);
        qint64 restored_timeline_duration_ms =
            saved_music_duration;

        if(backend == nullptr)
        {
            throw std::runtime_error(
                "Timeline backend is unavailable");
        }

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
            if(!backend->FindEffect(
                clip.effect_id,
                &definition))
            {
                if(warnings != nullptr)
                {
                    warnings->push_back(
                        QString(
                            "Skipped clip %1: effect \"%2\" is unavailable.")
                            .arg(clip_number)
                            .arg(clip.effect_id));
                }
                continue;
            }

            const int lane =
                backend->ResolveLane(clip.serialized_lane);
            if(lane < 0)
            {
                if(warnings != nullptr)
                {
                    warnings->push_back(
                        QString(
                            "Skipped clip %1 (%2): its device or zone is unavailable.")
                            .arg(clip_number)
                            .arg(definition.name));
                }
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
            TimelineBackend::EffectPtr runtime_effect =
                backend->CreateEffect(
                    clip.effect_id,
                    &backend_error);
            if(runtime_effect == nullptr)
            {
                throw std::runtime_error(
                    ToUtf8String(
                        backend_error.isEmpty()
                        ? QString("Could not create effect \"%1\"")
                            .arg(definition.name)
                        : backend_error));
            }

            if(!backend->ImportEffectSettings(
                *runtime_effect,
                clip.serialized_effect_settings,
                &backend_error))
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

        if(history_commit_timer != nullptr)
        {
            history_commit_timer->stop();
        }

        std::vector<TimelineBackend::ClipEffect>
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
        if(!backend->ReplaceEffects(
            std::move(replacement_effects),
            &replacement_error))
        {
            throw std::runtime_error(
                ToUtf8String(
                    replacement_error.isEmpty()
                    ? QStringLiteral(
                        "Could not replace timeline effects")
                    : replacement_error));
        }

        commit_started = true;
        replacing_layout_effects = true;
        timeline_editor->ClearTimelineClips();
        replacing_layout_effects = false;
        timeline_editor->SetMinimumTimelineDuration(
            restored_timeline_duration_ms);
        const bool music_available = SetMusicFile(
            saved_music_path,
            saved_music_duration);

        if(!saved_music_path.isEmpty()
            && !music_available
            && warnings != nullptr)
        {
            warnings->push_back(
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
        if(error != nullptr)
        {
            *error = QString::fromUtf8(exception.what());
        }
    }
    catch(...)
    {
        clear_partial_commit();
        applying_snapshot = false;
        replacing_layout_effects = false;
        if(error != nullptr)
        {
            *error = "The layout could not be restored.";
        }
    }
    return false;
}

void LightTrackPage::SaveLayout()
{
    FlushPendingHistory();

    QString error;
    LayoutSnapshot layout;
    if(!TryCaptureLayoutState(&layout, &error))
    {
        QMessageBox::critical(
            this,
            "Cannot save layout",
            error);
        return;
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
        return;
    }
    if(QFileInfo(path).suffix().isEmpty())
    {
        path += ".lighttrack";
    }

    if(layout_repository == nullptr
        || !layout_repository->Save(path, layout, &error))
    {
        if(error.isEmpty())
        {
            error =
                "Layout repository is unavailable.";
        }
        QMessageBox::critical(
            this,
            "Cannot save layout",
            error);
        return;
    }

    current_layout_path =
        QFileInfo(path).absoluteFilePath();
    toolbar_save_button->setToolTip(
        QString("Save layout (Ctrl+S)\nLast saved: %1")
            .arg(current_layout_path));
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
    if(layout_repository == nullptr
        || !layout_repository->Load(
            path,
            &layout,
            &file_error))
    {
        if(file_error.isEmpty())
        {
            file_error =
                "Layout repository is unavailable.";
        }
        QMessageBox::critical(
            this,
            "Cannot load layout",
            file_error);
        return;
    }

    FlushPendingHistory();
    QString error;
    QStringList warnings;
    if(!ApplyLayoutState(layout, &error, &warnings))
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
