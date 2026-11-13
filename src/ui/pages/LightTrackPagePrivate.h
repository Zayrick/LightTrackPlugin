#pragma once

#include "core/LayoutSnapshot.h"
#include "core/SnapshotHistory.h"
#include "infrastructure/persistence/LayoutFileRepository.h"
#include "integrations/audio/AudioScrubber.h"
#include "integrations/audio/MiniaudioAudioService.h"
#include "integrations/openrgb/ExternalEffectsSuspender.h"
#include "integrations/openrgb/OpenRgbTimelineBackend.h"
#include "integrations/openrgb/PlaybackDiagnostics.h"
#include "ui/timeline/TimelineMetrics.h"

#include <QVector>
#include <QWidget>
#include <QStringList>

#include <cmath>
#include <map>
#include <optional>

class QDialog;
class QLabel;
class QMenu;
class QEvent;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace lighttrack
{
class TimelineEditor;
}

class OpenRGBPluginAPIInterface;

namespace lighttrack::ui::page_detail
{
inline constexpr qreal PAGE_MARGIN = 10.0;
inline constexpr qreal TOOLBAR_HEIGHT = 32.0;
inline constexpr qreal TOOLBAR_BUTTON_SIZE = 28.0;
inline constexpr qreal TOOLBAR_ICON_SIZE = 17.0;
inline constexpr qreal TOOLBAR_BUTTON_SPACING = 6.0;
inline constexpr qreal TOOLBAR_SEPARATOR_GAP = 6.0;
inline constexpr qreal SETTINGS_PANEL_MIN_WIDTH = 300.0;
inline constexpr qreal SETTINGS_PANEL_PREFERRED_WIDTH = 480.0;
inline constexpr int MUSIC_SPECTRUM_BARS = 1200;
inline constexpr int HISTORY_LIMIT = 100;
inline constexpr int ZOOM_SLIDER_STEPS = 1000;

using timeline_metrics::EFFECTS_PANEL_WIDTH;
using timeline_metrics::GAP;
using timeline_metrics::GRID_WIDTH;
using timeline_metrics::LABEL_WIDTH;
using timeline_metrics::RULER_HEIGHT;
using timeline_metrics::TIMELINE_MIN_WIDTH;
using timeline_metrics::TIMELINE_ZOOM_MAX;
using timeline_metrics::TIMELINE_ZOOM_MIN;

// Zoom slider position (0..ZOOM_SLIDER_STEPS) <-> pixels per second, on a
// log scale so every step zooms by the same ratio.
inline qreal SliderToZoom(int value)
{
    const qreal fraction =
        qBound(0, value, ZOOM_SLIDER_STEPS) / qreal(ZOOM_SLIDER_STEPS);
    return TIMELINE_ZOOM_MIN
        * std::pow(TIMELINE_ZOOM_MAX / TIMELINE_ZOOM_MIN, fraction);
}

inline int ZoomToSlider(qreal pixels_per_second)
{
    const qreal clamped = qBound(
        TIMELINE_ZOOM_MIN,
        pixels_per_second,
        TIMELINE_ZOOM_MAX);
    return qRound(
        ZOOM_SLIDER_STEPS
        * std::log(clamped / TIMELINE_ZOOM_MIN)
        / std::log(TIMELINE_ZOOM_MAX / TIMELINE_ZOOM_MIN));
}
}

namespace lighttrack::ui
{
class LightTrackPage final : public QWidget
{
public:
    LightTrackPage(
        OpenRGBPluginAPIInterface* plugin_api,
        QWidget* parent = nullptr);
    ~LightTrackPage() override;

    LightTrackPage(const LightTrackPage&) = delete;
    LightTrackPage& operator=(const LightTrackPage&) = delete;

    void ReloadDevices();
    void PrepareForDeviceReload();
    void PauseForProfileLoad();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void UpdateMinimumTimelineDuration();

    void WatchSettingsObject(QObject* object);
    void ScheduleHistoryCommit();

    void InitializeHistory();
    void FlushPendingHistory();
    void CommitHistorySnapshot();
    void UpdateHistoryButtons();
    void Undo();
    void Redo();

    LayoutSnapshot CaptureLayoutState();
    bool TryCaptureLayoutState(
        LayoutSnapshot& snapshot,
        QString& error);
    bool ApplyLayoutState(
        const LayoutSnapshot& layout,
        QString& error,
        QStringList& warnings);
    void NewLayout();
    // Window to pick which devices appear in the timeline.
    void ChooseDevices();
    bool SaveLayout();
    // Saves to the current layout file without any dialog; falls back to
    // SaveLayout() when the layout has never been saved.
    bool QuickSaveLayout();
    bool WriteLayout(const QString& path, LayoutSnapshot layout);
    void UpdateSaveTooltips();
    void LoadLayout();

    bool EnsureClipEffect(ClipId clip_id);
    void CreateClipSettings(ClipId clip_id);
    void ShowClipSettings(std::optional<ClipId> clip_id);
    // Shows the clip's settings in the settings window and brings it up.
    void OpenClipSettingsWindow(ClipId clip_id);
    // Closes the settings window once it has nothing left to show.
    void HideEmptySettingsWindow();
    void RemoveClipSettings(ClipId clip_id);
    void DuplicateClipSettings(
        ClipId source_clip_id,
        ClipId duplicated_clip_id);
    void ClearAllClipSettings();

    void StartRuntime(qint64 position_ms);
    void StopRuntime();
    void SyncRuntime(qint64 position_ms);
    void ScheduleRuntimeBoundary(qint64 position_ms);
    void UpdateRuntimeBoundary();

    bool SetMusicFile(
        const QString& path,
        qint64 expected_duration_ms);
    void ChooseMusic();
    bool OpenMusic(const QString& path);
    void PlayMusic();
    void PauseMusic();
    void StopMusic();
    void SeekMusic(qint64 position_ms);
    void UpdateMusicPosition();
    qint64 MusicPositionMs() const;
    void CloseMusic();
    void FinishMusicPlayback();
    void MarkMusicError();

    void SetPlaybackControls(bool enabled, bool playing);
    void SetPlaybackRate(double rate);
    // Shows the playhead time in the transport bar.
    void UpdateTimeDisplay(qint64 position_ms);
    // record_history: user changed it (undoable); false when restoring.
    void SetWaveformStyle(WaveformStyle style, bool record_history);

    openrgb::OpenRgbTimelineBackend backend;
    // Temporary flicker diagnostic (see PlaybackDiagnostics).
    std::unique_ptr<openrgb::PlaybackDiagnostics> diagnostics;
    openrgb::ExternalEffectsSuspender external_effects;
    audio::MiniaudioAudioService audio_service;
    // DJ-style sound while dragging the playhead (paused/stopped).
    audio::AudioScrubber scrubber;
    persistence::LayoutFileRepository layout_repository;
    TimelineEditor* timeline_editor = nullptr;
    QDialog* settings_window = nullptr;
    QStackedWidget* settings_stack = nullptr;
    QLabel* settings_placeholder = nullptr;
    QPushButton* toolbar_undo_button = nullptr;
    QPushButton* toolbar_redo_button = nullptr;
    QPushButton* toolbar_new_button = nullptr;
    QPushButton* toolbar_save_button = nullptr;
    QPushButton* toolbar_quick_save_button = nullptr;
    QPushButton* toolbar_load_button = nullptr;
    QPushButton* toolbar_music_button = nullptr;
    QPushButton* toolbar_play_button = nullptr;
    QPushButton* toolbar_pause_button = nullptr;
    QPushButton* toolbar_stop_button = nullptr;
    QPushButton* toolbar_speed_button = nullptr;
    QMenu* speed_menu = nullptr;
    QTimer* scrub_stop_timer = nullptr;
    QLabel* transport_time_label = nullptr;
    QPushButton* toolbar_snap_button = nullptr;
    QPushButton* toolbar_waveform_button = nullptr;
    QMenu* waveform_menu = nullptr;
    QTimer* music_timer = nullptr;
    QTimer* runtime_boundary_timer = nullptr;
    QTimer* history_commit_timer = nullptr;
    QString music_path;
    QString current_layout_path;
    std::optional<LayoutSnapshot> persisted_layout;
    qint64 music_duration_ms = 0;
    bool music_loaded = false;
    bool music_playing = false;
    // Slow play: 1.0 = normal speed.
    double playback_rate = 1.0;
    WaveformStyle waveform_style = WaveformStyle::Smooth;
    // Lights are driven by the timeline (playing or paused at the playhead).
    bool output_active = false;
    QVector<qreal> music_spectrum_preview;
    std::map<ClipId, QWidget*> clip_settings;
    SnapshotHistory<LayoutSnapshot> history{
        page_detail::HISTORY_LIMIT};
    bool applying_snapshot = false;
    bool replacing_layout_effects = false;
};
}
