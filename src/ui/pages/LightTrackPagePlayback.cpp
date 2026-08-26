#include "ui/pages/LightTrackPagePrivate.h"

#include "application/AudioService.h"
#include "application/TimelineBackend.h"
#include "ui/timeline/TimelineEditor.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QPushButton>
#include <QTimer>

namespace lighttrack::ui
{
using namespace page_detail;

void LightTrackPage::UpdateMinimumTimelineDuration()
{
    qint64 content_duration_ms = music_duration_ms;
    for(const TimelineClip& clip :
        timeline_editor->PersistentTimelineClips())
    {
        content_duration_ms =
            qMax(content_duration_ms, clip.end_ms);
    }
    timeline_editor->SetMinimumTimelineDuration(
        content_duration_ms);
}

void LightTrackPage::StartRuntime(qint64 position_ms)
{
    if(backend != nullptr)
    {
        backend->StartRuntime(
            position_ms,
            timeline_editor->TimelineClips());
    }
}

void LightTrackPage::StopRuntime()
{
    if(backend != nullptr)
    {
        backend->StopRuntime();
    }
}

void LightTrackPage::SyncRuntime(qint64 position_ms)
{
    if(backend != nullptr)
    {
        backend->SyncRuntime(
            position_ms,
            timeline_editor->TimelineClips());
    }
}

bool LightTrackPage::SetMusicFile(
    const QString& path,
    qint64 expected_duration_ms)
{
    CloseMusic();
    music_path = path;
    music_spectrum_preview.clear();
    timeline_editor->SetMusicPosition(0);

    if(path.isEmpty())
    {
        toolbar_music_button->setText(
            "Select Music File");
        toolbar_music_button->setToolTip(
            "Select Music File");
        timeline_editor->SetMusicSpectrum({}, 0);
        return true;
    }

    const QFileInfo file_info(path);
    toolbar_music_button->setText(
        file_info.fileName().isEmpty()
            ? path
            : file_info.fileName());
    toolbar_music_button->setToolTip(path);

    if(!OpenMusic(path))
    {
        music_duration_ms =
            qMax<qint64>(0, expected_duration_ms);
        toolbar_music_button->setToolTip(
            path + "\nCannot play this file");
        timeline_editor->SetMusicSpectrum(
            {},
            music_duration_ms);
        return false;
    }

    music_spectrum_preview =
        audio_service->AnalyzeWaveform(
            path,
            MUSIC_SPECTRUM_BARS);
    timeline_editor->SetMusicSpectrum(
        music_spectrum_preview,
        music_duration_ms);
    SetPlaybackControls(true, false);
    return true;
}

void LightTrackPage::ChooseMusic()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Select music",
        QString(),
        "Audio Files (*.wav *.mp3 *.flac *.ogg);;All Files (*.*)");
    if(path.isEmpty())
    {
        return;
    }

    qint64 content_duration_ms = 0;
    for(const TimelineClip& clip :
        timeline_editor->PersistentTimelineClips())
    {
        content_duration_ms =
            qMax(content_duration_ms, clip.end_ms);
    }
    timeline_editor->SetMinimumTimelineDuration(
        content_duration_ms);
    SetMusicFile(QFileInfo(path).absoluteFilePath(), 0);
    CommitHistorySnapshot();
}

bool LightTrackPage::OpenMusic(const QString& path)
{
    if(audio_service == nullptr
        || !audio_service->Open(path))
    {
        return false;
    }

    music_duration_ms = audio_service->DurationMs();
    music_loaded = true;
    return true;
}

void LightTrackPage::PlayMusic()
{
    if(!music_loaded || music_playing)
    {
        return;
    }

    if(music_duration_ms > 0
        && MusicPositionMs() >= music_duration_ms - 20)
    {
        audio_service->Seek(0);
        timeline_editor->SetMusicPosition(0);
    }

    if(!audio_service->Play())
    {
        MarkMusicError();
        return;
    }

    music_timer->start();
    music_playing = true;
    SetPlaybackControls(true, true);
    StartRuntime(MusicPositionMs());
}

void LightTrackPage::PauseMusic()
{
    if(!music_loaded || !music_playing)
    {
        return;
    }

    audio_service->Pause();
    music_timer->stop();
    music_playing = false;
    SetPlaybackControls(true, false);
    UpdateMusicPosition();
}

void LightTrackPage::StopMusic()
{
    if(!music_loaded)
    {
        return;
    }

    music_timer->stop();
    audio_service->Pause();
    if(!audio_service->Seek(0))
    {
        MarkMusicError();
        return;
    }

    music_playing = false;
    SetPlaybackControls(true, false);
    timeline_editor->SetMusicPosition(0);
    StopRuntime();
}

void LightTrackPage::SeekMusic(qint64 position_ms)
{
    if(!music_loaded)
    {
        return;
    }

    const qint64 clamped_position =
        music_duration_ms > 0
        ? qBound<qint64>(
            0,
            position_ms,
            music_duration_ms)
        : qMax<qint64>(0, position_ms);

    if(!audio_service->Seek(clamped_position))
    {
        MarkMusicError();
        return;
    }

    timeline_editor->SetMusicPosition(clamped_position);
    if(music_playing)
    {
        StartRuntime(clamped_position);
    }
}

void LightTrackPage::UpdateMusicPosition()
{
    if(!music_loaded)
    {
        return;
    }

    if(music_playing && audio_service->IsAtEnd())
    {
        FinishMusicPlayback();
        return;
    }

    const qint64 position = MusicPositionMs();
    if(music_duration_ms > 0
        && position >= music_duration_ms)
    {
        FinishMusicPlayback();
        return;
    }

    timeline_editor->SetMusicPosition(position, music_playing);
    if(music_playing)
    {
        SyncRuntime(position);
    }
    else
    {
        StopRuntime();
    }
}

qint64 LightTrackPage::MusicPositionMs() const
{
    return audio_service != nullptr
        ? audio_service->PositionMs()
        : 0;
}

void LightTrackPage::CloseMusic()
{
    StopRuntime();
    if(music_timer != nullptr)
    {
        music_timer->stop();
    }
    music_spectrum_preview.clear();

    if(audio_service != nullptr)
    {
        audio_service->Close();
    }

    music_loaded = false;
    music_playing = false;
    music_duration_ms = 0;
    SetPlaybackControls(false, false);
}

void LightTrackPage::FinishMusicPlayback()
{
    if(music_timer != nullptr)
    {
        music_timer->stop();
    }
    if(audio_service != nullptr)
    {
        audio_service->Pause();
    }
    music_playing = false;
    SetPlaybackControls(true, false);
    timeline_editor->SetMusicPosition(music_duration_ms);
    StopRuntime();
}

void LightTrackPage::MarkMusicError()
{
    if(!music_loaded)
    {
        return;
    }

    StopRuntime();
    if(music_timer != nullptr)
    {
        music_timer->stop();
    }

    const qint64 known_duration_ms = music_duration_ms;
    music_loaded = false;
    music_playing = false;
    music_spectrum_preview.clear();
    if(audio_service != nullptr)
    {
        audio_service->Close();
    }
    music_duration_ms = known_duration_ms;
    timeline_editor->SetMusicSpectrum(
        {},
        music_duration_ms);
    SetPlaybackControls(false, false);

    if(toolbar_music_button != nullptr)
    {
        toolbar_music_button->setToolTip(
            music_path + "\nCannot play this file");
    }
}

void LightTrackPage::SetPlaybackControls(
    bool enabled,
    bool playing)
{
    if(toolbar_play_button != nullptr)
    {
        toolbar_play_button->setEnabled(enabled && !playing);
    }
    if(toolbar_pause_button != nullptr)
    {
        toolbar_pause_button->setEnabled(enabled && playing);
    }
    if(toolbar_stop_button != nullptr)
    {
        toolbar_stop_button->setEnabled(enabled);
    }
}
}
