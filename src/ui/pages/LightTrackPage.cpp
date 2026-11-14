#include "ui/pages/LightTrackPage.h"

#include "ui/UiHelpers.h"
#include "ui/pages/LightTrackPagePrivate.h"
#include "integrations/openrgb/PerformanceStats.h"
#include "ui/timeline/TimelineEditor.h"

#include <QAction>
#include <QActionGroup>
#include <QDialog>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QSizePolicy>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
using namespace lighttrack::ui::page_detail;

QPushButton* ToolbarButton(
    ushort codepoint,
    QWidget* parent)
{
    QPushButton* button =
        new QPushButton(QString(QChar(codepoint)), parent);
    button->setFixedSize(
        static_cast<int>(TOOLBAR_BUTTON_SIZE),
        static_cast<int>(TOOLBAR_BUTTON_SIZE));
    button->setFocusPolicy(Qt::NoFocus);

    QFont font(lighttrack::ui::LucideFontFamily());
    font.setPixelSize(
        static_cast<int>(TOOLBAR_ICON_SIZE));
    button->setFont(font);
    return button;
}
}

namespace lighttrack::ui
{
using namespace page_detail;

LightTrackPage::LightTrackPage(
    OpenRGBPluginAPIInterface* plugin_api,
    QWidget* parent) :
    QWidget(parent),
    backend(plugin_api),
    diagnostics(std::make_unique<openrgb::PlaybackDiagnostics>(plugin_api))
{
    QVBoxLayout* page_layout = new QVBoxLayout(this);
    page_layout->setContentsMargins(
        static_cast<int>(PAGE_MARGIN),
        static_cast<int>(PAGE_MARGIN),
        static_cast<int>(PAGE_MARGIN),
        static_cast<int>(PAGE_MARGIN));
    page_layout->setSpacing(0);

    QWidget* toolbar = new QWidget(this);
    toolbar->setObjectName("lightTrackToolbar");
    toolbar->setFixedHeight(
        static_cast<int>(TOOLBAR_HEIGHT));
    toolbar->setStyleSheet(R"(
        QWidget#lightTrackToolbar QPushButton {
            background-color: transparent;
            border: 1px solid transparent;
            border-radius: 6px;
            padding: 0;
        }

        QWidget#lightTrackToolbar QPushButton:hover {
            background-color: rgba(127, 127, 127, 45);
        }

        QWidget#lightTrackToolbar QPushButton:pressed {
            background-color: rgba(127, 127, 127, 80);
        }

        QWidget#lightTrackToolbar QPushButton#toolbarSnapButton:checked {
            background-color: rgba(64, 188, 255, 70);
            border-color: rgba(64, 188, 255, 180);
        }

        QWidget#lightTrackToolbar QPushButton#toolbarSnapButton:checked:hover {
            background-color: rgba(64, 188, 255, 95);
        }

        QWidget#lightTrackToolbar QPushButton#toolbarMusicButton {
            padding: 0 10px;
        }
    )");
    QGridLayout* toolbar_layout =
        new QGridLayout(toolbar);
    toolbar_layout->setContentsMargins(0, 0, 0, 0);
    toolbar_layout->setSpacing(0);
    toolbar_layout->setColumnStretch(0, 1);
    toolbar_layout->setColumnStretch(1, 0);
    toolbar_layout->setColumnStretch(2, 1);

    QWidget* toolbar_left_group = new QWidget(toolbar);
    toolbar_left_group->setSizePolicy(
        QSizePolicy::Fixed,
        QSizePolicy::Preferred);
    QHBoxLayout* toolbar_left_layout =
        new QHBoxLayout(toolbar_left_group);
    toolbar_left_layout->setContentsMargins(0, 0, 0, 0);
    toolbar_left_layout->setSpacing(
        static_cast<int>(TOOLBAR_BUTTON_SPACING));
    toolbar_undo_button =
        ToolbarButton(0xE2A1, toolbar_left_group);
    toolbar_redo_button =
        ToolbarButton(0xE2A0, toolbar_left_group);
    toolbar_snap_button =
        ToolbarButton(0xE2B5, toolbar_left_group);
    toolbar_new_button =
        new QPushButton("New", toolbar_left_group);
    toolbar_new_button->setFixedSize(
        48,
        static_cast<int>(TOOLBAR_BUTTON_SIZE));
    toolbar_new_button->setFocusPolicy(Qt::NoFocus);
    toolbar_save_button =
        ToolbarButton(0xE14D, toolbar_left_group);
    toolbar_quick_save_button =
        ToolbarButton(0xE710, toolbar_left_group);
    toolbar_quick_save_button->setObjectName("toolbarQuickSaveButton");
    toolbar_quick_save_button->setAccessibleName("Quick save");
    toolbar_quick_save_button->setToolTip(
        "Quick save\nNot saved yet: asks for a file the first time");
    toolbar_load_button =
        ToolbarButton(0xE318, toolbar_left_group);
    toolbar_undo_button->setToolTip("Undo (Ctrl+Z)");
    toolbar_redo_button->setToolTip(
        "Redo (Ctrl+Shift+Z / Ctrl+Y)");
    toolbar_new_button->setToolTip(
        "New layout (Ctrl+N)");
    toolbar_save_button->setToolTip(
        "Save layout (Ctrl+S)");
    toolbar_load_button->setToolTip(
        "Load layout (Ctrl+O)");
    toolbar_undo_button->setAccessibleName("Undo");
    toolbar_redo_button->setAccessibleName("Redo");
    toolbar_snap_button->setObjectName("toolbarSnapButton");
    toolbar_snap_button->setCheckable(true);
    toolbar_snap_button->setChecked(true);
    toolbar_snap_button->setToolTip(
        "Snap clips to the timeline grid and other clip edges");
    toolbar_snap_button->setAccessibleName("Timeline snapping");
    toolbar_new_button->setAccessibleName("New layout");
    toolbar_save_button->setAccessibleName("Save layout");
    toolbar_load_button->setAccessibleName("Load layout");
    toolbar_undo_button->setEnabled(false);
    toolbar_redo_button->setEnabled(false);
    toolbar_left_layout->addWidget(toolbar_undo_button);
    toolbar_left_layout->addWidget(toolbar_redo_button);
    toolbar_left_layout->addWidget(toolbar_snap_button);
    toolbar_left_layout->addWidget(toolbar_new_button);
    toolbar_left_layout->addWidget(toolbar_save_button);
    toolbar_left_layout->addWidget(toolbar_quick_save_button);
    toolbar_left_layout->addWidget(toolbar_load_button);

    QWidget* toolbar_right_group = new QWidget(toolbar);
    toolbar_right_group->setSizePolicy(
        QSizePolicy::Fixed,
        QSizePolicy::Preferred);
    QHBoxLayout* toolbar_right_layout =
        new QHBoxLayout(toolbar_right_group);
    toolbar_right_layout->setContentsMargins(0, 0, 0, 0);
    toolbar_right_layout->setSpacing(6);
    toolbar_right_layout->addStretch();
    toolbar_waveform_button =
        ToolbarButton(0xE55B, toolbar_right_group);
    toolbar_waveform_button->setObjectName(
        "toolbarWaveformButton");
    toolbar_waveform_button->setToolTip("Waveform style");
    toolbar_waveform_button->setAccessibleName("Waveform style");
    waveform_menu = new QMenu(toolbar_waveform_button);
    QActionGroup* waveform_group = new QActionGroup(waveform_menu);
    waveform_group->setExclusive(true);
    const std::pair<WaveformStyle, const char*> waveform_styles[] = {
        {WaveformStyle::Studio, "Studio (DAW) waveform"},
        {WaveformStyle::Smooth, "Smooth waveform"},
        {WaveformStyle::Bars, "Thin rounded bars"},
        {WaveformStyle::Line, "Line waveform"},
        {WaveformStyle::Progress, "Played / unplayed bars"},
        {WaveformStyle::Classic, "Classic blocks"}};
    for(const auto& entry : waveform_styles)
    {
        QAction* action = waveform_menu->addAction(entry.second);
        action->setCheckable(true);
        action->setData(static_cast<int>(entry.first));
        action->setChecked(entry.first == waveform_style);
        waveform_group->addAction(action);
        const WaveformStyle style = entry.first;
        connect(
            action,
            &QAction::triggered,
            this,
            [this, style]()
            {
                SetWaveformStyle(style, true);
            });
    }
    connect(
        toolbar_waveform_button,
        &QPushButton::clicked,
        this,
        [this]()
        {
            waveform_menu->popup(
                toolbar_waveform_button->mapToGlobal(
                    QPoint(0, toolbar_waveform_button->height())));
        });
    toolbar_right_layout->addWidget(toolbar_waveform_button);
    // Zoom slider lives in the transport bar under the timeline.
    QSlider* zoom_slider = new QSlider(Qt::Horizontal, this);
    // Logarithmic: each step zooms by the same ratio across the wide range.
    zoom_slider->setObjectName("timelineZoomSlider");
    zoom_slider->setRange(0, ZOOM_SLIDER_STEPS);
    zoom_slider->setValue(ZoomToSlider(GRID_WIDTH));
    zoom_slider->setMinimumWidth(160);
    zoom_slider->setFocusPolicy(Qt::NoFocus);
    zoom_slider->setToolTip("Timeline zoom");
    toolbar_music_button =
        new QPushButton("Select Music File", toolbar_right_group);
    toolbar_music_button->setObjectName(
        "toolbarMusicButton");
    toolbar_music_button->setFixedHeight(
        static_cast<int>(TOOLBAR_BUTTON_SIZE));
    toolbar_music_button->setFocusPolicy(Qt::NoFocus);
    toolbar_right_layout->addWidget(toolbar_music_button);

    toolbar_layout->addWidget(
        toolbar_left_group,
        0,
        0,
        Qt::AlignLeft | Qt::AlignVCenter);
    // Transport buttons live in the transport bar under the timeline.
    QWidget* toolbar_playback_group = new QWidget(this);
    toolbar_playback_group->setSizePolicy(
        QSizePolicy::Fixed,
        QSizePolicy::Preferred);
    QHBoxLayout* toolbar_playback_layout =
        new QHBoxLayout(toolbar_playback_group);
    toolbar_playback_layout->setContentsMargins(0, 0, 0, 0);
    toolbar_playback_layout->setSpacing(
        static_cast<int>(TOOLBAR_BUTTON_SPACING));
    toolbar_play_button =
        ToolbarButton(0xE13C, toolbar_playback_group);
    toolbar_pause_button =
        ToolbarButton(0xE12E, toolbar_playback_group);
    toolbar_stop_button =
        ToolbarButton(0xE167, toolbar_playback_group);
    toolbar_play_button->setToolTip("Play (Space)");
    toolbar_pause_button->setToolTip("Pause (Space)");
    toolbar_stop_button->setToolTip("Stop");
    toolbar_play_button->setAccessibleName("Play");
    toolbar_pause_button->setAccessibleName("Pause");
    toolbar_stop_button->setAccessibleName("Stop");
    toolbar_play_button->setEnabled(false);
    toolbar_pause_button->setEnabled(false);
    toolbar_stop_button->setEnabled(false);
    toolbar_playback_layout->addWidget(toolbar_play_button);
    toolbar_playback_layout->addWidget(toolbar_pause_button);
    toolbar_playback_layout->addWidget(toolbar_stop_button);

    // Follow playhead: when off, the timeline does not scroll during playback.
    QPushButton* follow_button =
        ToolbarButton(0xE1DA, toolbar_playback_group);
    follow_button->setObjectName("followPlayheadButton");
    follow_button->setAccessibleName("Follow playhead");
    follow_button->setCheckable(true);
    follow_button->setChecked(true);
    follow_button->setToolTip(
        "Follow playhead: scroll the timeline during playback");
    connect(
        follow_button,
        &QPushButton::toggled,
        this,
        [this, follow_button](bool follow)
        {
            timeline_editor->SetFollowPlayhead(follow);
            follow_button->setToolTip(
                follow
                    ? "Follow playhead: scroll the timeline during playback"
                    : "Not following the playhead (click to follow)");
        });
    toolbar_playback_layout->addWidget(follow_button);

    // Slow play: playback speed menu.
    toolbar_speed_button =
        new QPushButton("1x", this);
    toolbar_speed_button->setObjectName("toolbarSpeedButton");
    toolbar_speed_button->setAccessibleName("Playback speed");
    toolbar_speed_button->setToolTip(
        "Playback speed (slower also lowers the pitch)");
    toolbar_speed_button->setFixedSize(
        44,
        static_cast<int>(TOOLBAR_BUTTON_SIZE));
    toolbar_speed_button->setFocusPolicy(Qt::NoFocus);
    speed_menu = new QMenu(toolbar_speed_button);
    QActionGroup* speed_group = new QActionGroup(speed_menu);
    speed_group->setExclusive(true);
    for(const double rate : {0.25, 0.5, 0.75, 1.0})
    {
        QAction* action = speed_menu->addAction(
            QString("%1x").arg(rate));
        action->setCheckable(true);
        action->setChecked(rate == 1.0);
        action->setData(rate);
        speed_group->addAction(action);
        connect(
            action,
            &QAction::triggered,
            this,
            [this, rate]() { SetPlaybackRate(rate); });
    }
    connect(
        toolbar_speed_button,
        &QPushButton::clicked,
        this,
        [this]()
        {
            speed_menu->popup(toolbar_speed_button->mapToGlobal(
                QPoint(0, toolbar_speed_button->height())));
        });
    toolbar_layout->addWidget(
        toolbar_right_group,
        0,
        2,
        Qt::AlignRight | Qt::AlignVCenter);

    QFrame* toolbar_separator = new QFrame(this);
    toolbar_separator->setFrameShape(QFrame::HLine);
    toolbar_separator->setFrameShadow(QFrame::Sunken);

    QWidget* content = new QWidget(this);
    QHBoxLayout* content_layout =
        new QHBoxLayout(content);
    content_layout->setContentsMargins(
        0,
        static_cast<int>(GAP),
        0,
        0);
    content_layout->setSpacing(0);

    timeline_editor = new TimelineEditor(this);
    timeline_editor->SetEffects(backend.Effects());

    // Effect settings live in their own window, opened by double-clicking
    // a clip on the timeline.
    settings_window = new QDialog(this);
    settings_window->setObjectName("effectSettingsWindow");
    settings_window->setWindowTitle("Effect Settings");
    settings_window->setWindowFlags(
        Qt::Tool
        | Qt::WindowTitleHint
        | Qt::WindowCloseButtonHint);
    settings_window->setModal(false);
    settings_window->resize(
        static_cast<int>(SETTINGS_PANEL_PREFERRED_WIDTH),
        640);
    QVBoxLayout* settings_window_layout =
        new QVBoxLayout(settings_window);
    settings_window_layout->setContentsMargins(0, 0, 0, 0);
    settings_stack = new QStackedWidget(settings_window);
    settings_window_layout->addWidget(settings_stack);
    music_timer = new QTimer(this);
    music_timer->setInterval(33);
    music_timer->setTimerType(Qt::PreciseTimer);
    runtime_boundary_timer = new QTimer(this);
    runtime_boundary_timer->setSingleShot(true);
    runtime_boundary_timer->setTimerType(Qt::PreciseTimer);
    scrub_stop_timer = new QTimer(this);
    scrub_stop_timer->setSingleShot(true);
    scrub_stop_timer->setInterval(250);
    connect(
        scrub_stop_timer,
        &QTimer::timeout,
        this,
        [this]() { scrubber.Stop(); });
    history_commit_timer = new QTimer(this);
    history_commit_timer->setSingleShot(true);
    history_commit_timer->setInterval(250);
    timeline_editor->SetMusicSeekCallback(
        [this](qint64 position_ms)
        {
            SeekMusic(position_ms);
        });

    settings_placeholder = new QLabel(
        "Select an effect card on the timeline to configure it.",
        settings_stack);
    settings_placeholder->setAlignment(Qt::AlignCenter);
    settings_placeholder->setWordWrap(true);
    settings_placeholder->setMargin(24);
    settings_stack->addWidget(settings_placeholder);

    timeline_editor->SetClipActivatedCallback(
        [this](ClipId clip_id)
        {
            OpenClipSettingsWindow(clip_id);
        });
    timeline_editor->SetClipSelectedCallback(
        [this](std::optional<ClipId> clip_id)
        {
            // Only an open settings window follows the selection; a single
            // click never opens it.
            if(!settings_window->isVisible() || !clip_id.has_value())
            {
                return;
            }
            OpenClipSettingsWindow(*clip_id);
        });
    timeline_editor->SetClipRemovedCallback(
        [this](ClipId clip_id)
        {
            RemoveClipSettings(clip_id);
        });
    timeline_editor->SetClipDuplicatedCallback(
        [this](ClipId source_clip_id, ClipId duplicated_clip_id)
        {
            DuplicateClipSettings(
                source_clip_id,
                duplicated_clip_id);
        });
    timeline_editor->SetTimelineChangedCallback([this]()
    {
        UpdateMinimumTimelineDuration();
        if(output_active)
        {
            StartRuntime(MusicPositionMs());
        }
        CommitHistorySnapshot();
    });
    timeline_editor->SetLaneRenamedCallback(
        [this](int lane_index, const QString& name)
        {
            if(!backend.RenameLane(lane_index, name))
            {
                return false;
            }
            CommitHistorySnapshot();
            return true;
        });
    timeline_editor->SetLaneHighlightedCallback(
        [this](int lane_index, bool highlighted)
        {
            if(!backend.SetLaneHighlighted(
                lane_index,
                highlighted))
            {
                return false;
            }
            CommitHistorySnapshot();
            return true;
        });
    timeline_editor->SetChooseDevicesCallback(
        [this]()
        {
            ChooseDevices();
        });
    timeline_editor->SetLaneDisabledCallback(
        [this](int lane_index, bool disabled)
        {
            if(!backend.SetLaneDisabled(
                lane_index,
                disabled))
            {
                return false;
            }
            CommitHistorySnapshot();
            return true;
        });

    content_layout->addWidget(timeline_editor, 1);
    page_layout->addWidget(toolbar);
    page_layout->addSpacing(
        static_cast<int>(TOOLBAR_SEPARATOR_GAP));
    page_layout->addWidget(toolbar_separator);
    page_layout->addWidget(content, 1);

    // Transport bar under the timeline (VEGAS-style): rate, transport
    // buttons, a long zoom slider and the playhead time.
    QWidget* transport_bar = new QWidget(this);
    transport_bar->setObjectName("lightTrackTransportBar");
    transport_bar->setFixedHeight(
        static_cast<int>(TOOLBAR_HEIGHT) + 6);
    transport_bar->setStyleSheet(R"(
        QWidget#lightTrackTransportBar QPushButton {
            background-color: transparent;
            border: 1px solid transparent;
            border-radius: 6px;
            padding: 0;
        }

        QWidget#lightTrackTransportBar QPushButton:hover {
            background-color: rgba(127, 127, 127, 45);
        }

        QWidget#lightTrackTransportBar QPushButton:pressed {
            background-color: rgba(127, 127, 127, 80);
        }

        QWidget#lightTrackTransportBar QPushButton:checked {
            background-color: rgba(64, 188, 255, 70);
            border-color: rgba(64, 188, 255, 180);
        }

        QWidget#lightTrackTransportBar QPushButton:checked:hover {
            background-color: rgba(64, 188, 255, 95);
        }

        QWidget#lightTrackTransportBar QLabel#transportTimeLabel {
            font-family: Consolas, monospace;
            font-size: 14px;
            padding: 0 6px;
        }
    )");
    QHBoxLayout* transport_layout = new QHBoxLayout(transport_bar);
    transport_layout->setContentsMargins(0, 6, 0, 0);
    transport_layout->setSpacing(
        static_cast<int>(TOOLBAR_BUTTON_SPACING));

    QLabel* rate_label = new QLabel("Rate:", transport_bar);
    toolbar_speed_button->setParent(transport_bar);
    transport_layout->addWidget(rate_label);
    transport_layout->addWidget(toolbar_speed_button);
    transport_layout->addSpacing(12);

    toolbar_playback_group->setParent(transport_bar);
    transport_layout->addWidget(toolbar_playback_group);
    transport_layout->addSpacing(16);

    QPushButton* zoom_out_button = ToolbarButton(0xE1B7, transport_bar);
    zoom_out_button->setToolTip("Zoom out");
    zoom_out_button->setAccessibleName("Zoom out");
    QPushButton* zoom_in_button = ToolbarButton(0xE1B6, transport_bar);
    zoom_in_button->setToolTip("Zoom in");
    zoom_in_button->setAccessibleName("Zoom in");
    zoom_slider->setParent(transport_bar);
    transport_layout->addWidget(zoom_out_button);
    transport_layout->addWidget(zoom_slider, 1);
    transport_layout->addWidget(zoom_in_button);
    connect(zoom_out_button, &QPushButton::clicked, zoom_slider,
        [zoom_slider]() { zoom_slider->setValue(zoom_slider->value() - 50); });
    connect(zoom_in_button, &QPushButton::clicked, zoom_slider,
        [zoom_slider]() { zoom_slider->setValue(zoom_slider->value() + 50); });

    transport_layout->addSpacing(12);
    transport_time_label = new QLabel(transport_bar);
    transport_time_label->setObjectName("transportTimeLabel");
    transport_time_label->setToolTip("Playhead position");
    transport_layout->addWidget(transport_time_label);
    UpdateTimeDisplay(0);

    page_layout->addWidget(transport_bar);

    connect(
        toolbar_music_button,
        &QPushButton::clicked,
        this,
        [this]() { ChooseMusic(); });
    connect(
        toolbar_undo_button,
        &QPushButton::clicked,
        this,
        [this]() { Undo(); });
    connect(
        toolbar_redo_button,
        &QPushButton::clicked,
        this,
        [this]() { Redo(); });
    connect(
        toolbar_new_button,
        &QPushButton::clicked,
        this,
        [this]() { NewLayout(); });
    connect(
        toolbar_save_button,
        &QPushButton::clicked,
        this,
        [this]() { SaveLayout(); });
    connect(
        toolbar_quick_save_button,
        &QPushButton::clicked,
        this,
        [this]() { QuickSaveLayout(); });
    connect(
        toolbar_load_button,
        &QPushButton::clicked,
        this,
        [this]() { LoadLayout(); });
    connect(
        toolbar_play_button,
        &QPushButton::clicked,
        this,
        [this]() { PlayMusic(); });
    connect(
        toolbar_pause_button,
        &QPushButton::clicked,
        this,
        [this]() { PauseMusic(); });
    connect(
        toolbar_stop_button,
        &QPushButton::clicked,
        this,
        [this]() { StopMusic(); });
    connect(
        toolbar_snap_button,
        &QPushButton::toggled,
        this,
        [this](bool enabled)
        {
            timeline_editor->SetSnappingEnabled(enabled);
        });
    connect(
        zoom_slider,
        &QSlider::valueChanged,
        this,
        [this](int value)
        {
            timeline_editor->SetHorizontalZoom(
                qRound(SliderToZoom(value)));
        });
    connect(
        music_timer,
        &QTimer::timeout,
        this,
        [this]()
        {
            namespace perf = openrgb::performance;
            const perf::Clock::time_point start = perf::Clock::now();
            UpdateMusicPosition();
            perf::RecordUi("playback tick", perf::MsSince(start));
        });
    connect(
        runtime_boundary_timer,
        &QTimer::timeout,
        this,
        [this]() { UpdateRuntimeBoundary(); });
    connect(
        history_commit_timer,
        &QTimer::timeout,
        this,
        [this]() { CommitHistorySnapshot(); });

    QShortcut* playback_shortcut =
        new QShortcut(QKeySequence(Qt::Key_Space), this);
    playback_shortcut->setContext(
        Qt::WidgetWithChildrenShortcut);
    playback_shortcut->setAutoRepeat(false);
    connect(
        playback_shortcut,
        &QShortcut::activated,
        this,
        [this]()
        {
            if(music_playing)
            {
                PauseMusic();
            }
            else
            {
                PlayMusic();
            }
        });

    QShortcut* undo_shortcut =
        new QShortcut(QKeySequence::Undo, this);
    undo_shortcut->setContext(
        Qt::WidgetWithChildrenShortcut);
    connect(
        undo_shortcut,
        &QShortcut::activated,
        this,
        [this]() { Undo(); });

    QShortcut* redo_shortcut =
        new QShortcut(QKeySequence::Redo, this);
    redo_shortcut->setContext(
        Qt::WidgetWithChildrenShortcut);
    connect(
        redo_shortcut,
        &QShortcut::activated,
        this,
        [this]() { Redo(); });

    const QKeySequence alternate_redo_sequence(
        QStringLiteral("Ctrl+Y"));
    if(QKeySequence(QKeySequence::Redo)
        != alternate_redo_sequence)
    {
        QShortcut* alternate_redo_shortcut =
            new QShortcut(alternate_redo_sequence, this);
        alternate_redo_shortcut->setContext(
            Qt::WidgetWithChildrenShortcut);
        connect(
            alternate_redo_shortcut,
            &QShortcut::activated,
            this,
            [this]() { Redo(); });
    }

    const QKeySequence alternate_shift_redo_sequence(
        QStringLiteral("Ctrl+Shift+Z"));
    if(QKeySequence(QKeySequence::Redo)
        != alternate_shift_redo_sequence)
    {
        QShortcut* alternate_shift_redo_shortcut =
            new QShortcut(
                alternate_shift_redo_sequence,
                this);
        alternate_shift_redo_shortcut->setContext(
            Qt::WidgetWithChildrenShortcut);
        connect(
            alternate_shift_redo_shortcut,
            &QShortcut::activated,
            this,
            [this]() { Redo(); });
    }

    QShortcut* new_shortcut =
        new QShortcut(QKeySequence::New, this);
    new_shortcut->setContext(
        Qt::WidgetWithChildrenShortcut);
    new_shortcut->setAutoRepeat(false);
    connect(
        new_shortcut,
        &QShortcut::activated,
        this,
        [this]() { NewLayout(); });

    QShortcut* save_shortcut =
        new QShortcut(QKeySequence::Save, this);
    save_shortcut->setContext(
        Qt::WidgetWithChildrenShortcut);
    connect(
        save_shortcut,
        &QShortcut::activated,
        this,
        [this]() { SaveLayout(); });

    QShortcut* load_shortcut =
        new QShortcut(QKeySequence::Open, this);
    load_shortcut->setContext(
        Qt::WidgetWithChildrenShortcut);
    connect(
        load_shortcut,
        &QShortcut::activated,
        this,
        [this]() { LoadLayout(); });

    ReloadDevices();
    InitializeHistory();
}

LightTrackPage::~LightTrackPage()
{
    CloseMusic();
    backend.ClearClips();
    ClearAllClipSettings();
}

QWidget* CreateLightTrackPage(
    OpenRGBPluginAPIInterface* plugin_api)
{
    return new LightTrackPage(plugin_api);
}

void ReloadLightTrackDevices(QWidget* page)
{
    static_cast<LightTrackPage*>(page)->ReloadDevices();
}

void PrepareLightTrackForDeviceReload(QWidget* page)
{
    static_cast<LightTrackPage*>(page)->PrepareForDeviceReload();
}

void PauseLightTrackForProfileLoad(QWidget* page)
{
    static_cast<LightTrackPage*>(page)->PauseForProfileLoad();
}
}
