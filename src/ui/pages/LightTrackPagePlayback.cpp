#include "ui/pages/LightTrackPagePrivate.h"

#include "integrations/openrgb/PerformanceStats.h"
#include "ui/timeline/TimelineEditor.h"

#include <QAction>
#include <QFileDialog>
#include <QMenu>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

#include <limits>

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

// The lights are live from Play (or the first seek) until Stop, the end of
// the music, or an error. While paused they keep showing the playhead
// position, so seeking previews the timeline.
void LightTrackPage::StartRuntime(qint64 position_ms)
{
    // Keep the separate Effects plugin off the controllers while
    // LightTrack is driving them.
    external_effects.Suspend();
    output_active = true;
    backend.StartRuntime(
        position_ms,
        timeline_editor->TimelineClips(),
        !music_playing,
        playback_rate);
    ScheduleRuntimeBoundary(position_ms);
}

void LightTrackPage::StopRuntime()
{
    runtime_boundary_timer->stop();
    backend.StopRuntime();
    output_active = false;
    external_effects.Resume();
}

void LightTrackPage::SyncRuntime(qint64 position_ms)
{
    backend.SyncRuntime(
        position_ms,
        timeline_editor->TimelineClips(),
        !music_playing,
        playback_rate);
    ScheduleRuntimeBoundary(position_ms);
}

void LightTrackPage::ScheduleRuntimeBoundary(qint64 position_ms)
{
    if(!music_playing)
    {
        runtime_boundary_timer->stop();
        return;
    }

    qint64 next_boundary_ms = std::numeric_limits<qint64>::max();
    for(const TimelineClip& clip : timeline_editor->TimelineClips())
    {
        if(clip.start_ms > position_ms)
        {
            next_boundary_ms = qMin(next_boundary_ms, clip.start_ms);
        }
        if(clip.end_ms > position_ms)
        {
            next_boundary_ms = qMin(next_boundary_ms, clip.end_ms);
        }
    }

    if(next_boundary_ms == std::numeric_limits<qint64>::max())
    {
        runtime_boundary_timer->stop();
        return;
    }

    // Song time runs slower in slow play.
    const qint64 delay_ms = qBound<qint64>(
        qint64{1},
        static_cast<qint64>(
            (next_boundary_ms - position_ms) / playback_rate),
        static_cast<qint64>(std::numeric_limits<int>::max()));
    runtime_boundary_timer->start(static_cast<int>(delay_ms));
}

void LightTrackPage::UpdateRuntimeBoundary()
{
    if(music_playing)
    {
        SyncRuntime(MusicPositionMs());
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
        audio_service.AnalyzeWaveform(
            path,
            MUSIC_SPECTRUM_BARS);
    // Detailed left/right peaks for the music track.
    timeline_editor->SetMusicPeaks(
        audio_service.AnalyzeWaveformPeaks(path));
    UpdateTimeDisplay(0);
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
    if(!audio_service.Open(path))
    {
        return false;
    }

    music_duration_ms = audio_service.DurationMs();
    music_loaded = true;
    scrubber.Load(path);
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
        audio_service.Seek(0);
        timeline_editor->SetMusicPosition(0);
    }

    scrub_stop_timer->stop();
    scrubber.Stop();
    if(!audio_service.Play())
    {
        MarkMusicError();
        return;
    }

    music_timer->start();
    music_playing = true;
    openrgb::performance::Reset();
    diagnostics->Start();
    SetPlaybackControls(true, true);
    StartRuntime(MusicPositionMs());
}

void LightTrackPage::PauseMusic()
{
    if(!music_loaded || !music_playing)
    {
        return;
    }

    audio_service.Pause();
    music_timer->stop();
    music_playing = false;
    SetPlaybackControls(true, false);
    UpdateMusicPosition();
}

void LightTrackPage::PauseForProfileLoad()
{
    PauseMusic();
    StopRuntime();
}

void LightTrackPage::StopMusic()
{
    if(!music_loaded)
    {
        return;
    }

    music_timer->stop();
    audio_service.Pause();
    if(!audio_service.Seek(0))
    {
        MarkMusicError();
        return;
    }

    music_playing = false;
    SetPlaybackControls(true, false);
    timeline_editor->SetMusicPosition(0);
    UpdateTimeDisplay(0);
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

    if(!audio_service.Seek(clamped_position))
    {
        MarkMusicError();
        return;
    }

    timeline_editor->SetMusicPosition(clamped_position);
    UpdateTimeDisplay(clamped_position);
    if(!music_playing)
    {
        // Dragging the playhead while paused: vinyl-style scrub sound.
        scrubber.ScrubTo(clamped_position);
        scrub_stop_timer->start();
    }
    StartRuntime(clamped_position);
}

void LightTrackPage::UpdateMusicPosition()
{
    if(!music_loaded)
    {
        return;
    }

    if(music_playing && audio_service.IsAtEnd())
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
    UpdateTimeDisplay(position);
    if(music_playing)
    {
        SyncRuntime(position);
    }
    else if(output_active)
    {
        // Paused: hold the lights at the playhead.
        StartRuntime(position);
    }
}

qint64 LightTrackPage::MusicPositionMs() const
{
    return audio_service.PositionMs();
}

void LightTrackPage::CloseMusic()
{
    timeline_editor->SetMusicPeaks({});
    scrub_stop_timer->stop();
    scrubber.Unload();
    StopRuntime();
    music_timer->stop();
    music_spectrum_preview.clear();

    audio_service.Close();

    music_loaded = false;
    music_playing = false;
    music_duration_ms = 0;
    SetPlaybackControls(false, false);
}

void LightTrackPage::FinishMusicPlayback()
{
    music_timer->stop();
    audio_service.Pause();
    music_playing = false;
    SetPlaybackControls(true, false);
    timeline_editor->SetMusicPosition(music_duration_ms);
    UpdateTimeDisplay(music_duration_ms);
    StopRuntime();
}

void LightTrackPage::MarkMusicError()
{
    if(!music_loaded)
    {
        return;
    }

    StopRuntime();
    music_timer->stop();

    const qint64 known_duration_ms = music_duration_ms;
    music_loaded = false;
    music_playing = false;
    music_spectrum_preview.clear();
    audio_service.Close();
    music_duration_ms = known_duration_ms;
    timeline_editor->SetMusicSpectrum(
        {},
        music_duration_ms);
    SetPlaybackControls(false, false);

    toolbar_music_button->setToolTip(
        music_path + "\nCannot play this file");
}

void LightTrackPage::SetWaveformStyle(
    WaveformStyle style,
    bool record_history)
{
    const bool changed = waveform_style != style;
    waveform_style = style;
    timeline_editor->SetWaveformStyle(style);
    for(QAction* action : waveform_menu->actions())
    {
        action->setChecked(
            action->data().toInt() == static_cast<int>(style));
    }
    if(changed && record_history)
    {
        CommitHistorySnapshot();
    }
}

void LightTrackPage::UpdateTimeDisplay(qint64 position_ms)
{
    if(transport_time_label == nullptr)
    {
        return;
    }
    const qint64 clamped = qMax<qint64>(0, position_ms);
    const QChar zero('0');
    transport_time_label->setText(QString("%1:%2:%3.%4")
        .arg(clamped / 3600000, 2, 10, zero)
        .arg((clamped / 60000) % 60, 2, 10, zero)
        .arg((clamped / 1000) % 60, 2, 10, zero)
        .arg(clamped % 1000, 3, 10, zero));
}

void LightTrackPage::SetPlaybackRate(double rate)
{
    playback_rate = rate > 0.0 ? rate : 1.0;
    audio_service.SetPlaybackRate(playback_rate);
    toolbar_speed_button->setText(QString("%1x").arg(playback_rate));
    for(QAction* action : speed_menu->actions())
    {
        action->setChecked(action->data().toDouble() == playback_rate);
    }
    if(output_active)
    {
        // Re-time the running clips at the new speed.
        StartRuntime(MusicPositionMs());
    }
}

void LightTrackPage::SetPlaybackControls(
    bool enabled,
    bool playing)
{
    if(!playing)
    {
        diagnostics->Stop();
        openrgb::performance::WriteReport();
    }
    toolbar_play_button->setEnabled(enabled && !playing);
    toolbar_pause_button->setEnabled(enabled && playing);
    toolbar_stop_button->setEnabled(enabled);
}
}
