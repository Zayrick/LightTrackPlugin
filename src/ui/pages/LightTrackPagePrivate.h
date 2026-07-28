#pragma once

#include "core/LayoutSnapshot.h"
#include "core/SnapshotHistory.h"
#include "ui/timeline/TimelineMetrics.h"

#include <QVector>
#include <QWidget>
#include <QStringList>

#include <map>
#include <memory>
#include <optional>

class QLabel;
class QEvent;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace lighttrack
{
class AudioService;
class LayoutRepository;
class TimelineBackend;
class TimelineEditor;
}

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

using timeline_metrics::EFFECTS_PANEL_WIDTH;
using timeline_metrics::GAP;
using timeline_metrics::GRID_WIDTH;
using timeline_metrics::LABEL_WIDTH;
using timeline_metrics::RULER_HEIGHT;
using timeline_metrics::TIMELINE_MIN_WIDTH;
using timeline_metrics::TIMELINE_ZOOM_MAX;
using timeline_metrics::TIMELINE_ZOOM_MIN;
}

namespace lighttrack::ui
{
class LightTrackPage final : public QWidget
{
public:
    LightTrackPage(
        std::unique_ptr<TimelineBackend> backend,
        std::unique_ptr<AudioService> audio_service,
        std::unique_ptr<LayoutRepository> layout_repository,
        QWidget* parent = nullptr);
    ~LightTrackPage() override;

    LightTrackPage(const LightTrackPage&) = delete;
    LightTrackPage& operator=(const LightTrackPage&) = delete;

    void ReloadDevices();

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
        LayoutSnapshot* snapshot,
        QString* error);
    bool ApplyLayoutState(
        const LayoutSnapshot& layout,
        QString* error,
        QStringList* warnings);
    void SaveLayout();
    void LoadLayout();

    bool EnsureClipEffect(ClipId clip_id);
    void CreateClipSettings(ClipId clip_id);
    void ShowClipSettings(std::optional<ClipId> clip_id);
    void RemoveClipSettings(ClipId clip_id);
    void ClearAllClipSettings();

    void StartRuntime(qint64 position_ms);
    void StopRuntime();
    void SyncRuntime(qint64 position_ms);

    bool SetMusicFile(
        const QString& path,
        qint64 expected_duration_ms);
    void ChooseMusic();
    bool OpenMusic(const QString& path);
    void ToggleMusicPlayback();
    void SeekMusic(qint64 position_ms);
    void UpdateMusicPosition();
    qint64 MusicPositionMs() const;
    void CloseMusic();
    void FinishMusicPlayback();
    void MarkMusicError();

    void SetPlaybackControls(bool enabled, bool playing);

    std::unique_ptr<TimelineBackend> backend;
    std::unique_ptr<AudioService> audio_service;
    std::unique_ptr<LayoutRepository> layout_repository;
    TimelineEditor* timeline_editor = nullptr;
    QStackedWidget* settings_stack = nullptr;
    QLabel* settings_placeholder = nullptr;
    QPushButton* toolbar_undo_button = nullptr;
    QPushButton* toolbar_redo_button = nullptr;
    QPushButton* toolbar_save_button = nullptr;
    QPushButton* toolbar_load_button = nullptr;
    QPushButton* toolbar_music_button = nullptr;
    QPushButton* toolbar_play_button = nullptr;
    QPushButton* toolbar_snap_button = nullptr;
    QTimer* music_timer = nullptr;
    QTimer* history_commit_timer = nullptr;
    QString music_path;
    QString current_layout_path;
    qint64 music_duration_ms = 0;
    bool music_loaded = false;
    bool music_playing = false;
    QVector<qreal> music_spectrum_preview;
    std::map<ClipId, QWidget*> clip_settings;
    SnapshotHistory<LayoutSnapshot> history{
        page_detail::HISTORY_LIMIT};
    bool applying_snapshot = false;
    bool replacing_layout_effects = false;
};
}
