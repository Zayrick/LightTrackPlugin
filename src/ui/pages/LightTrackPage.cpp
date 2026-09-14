#include "ui/pages/LightTrackPage.h"

#include "ui/UiHelpers.h"
#include "ui/pages/LightTrackPagePrivate.h"
#include "ui/timeline/TimelineEditor.h"

#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
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
    backend(plugin_api)
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
    toolbar_save_button =
        ToolbarButton(0xE14D, toolbar_left_group);
    toolbar_load_button =
        ToolbarButton(0xE318, toolbar_left_group);
    toolbar_undo_button->setToolTip("Undo (Ctrl+Z)");
    toolbar_redo_button->setToolTip(
        "Redo (Ctrl+Shift+Z / Ctrl+Y)");
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
    toolbar_save_button->setAccessibleName("Save layout");
    toolbar_load_button->setAccessibleName("Load layout");
    toolbar_undo_button->setEnabled(false);
    toolbar_redo_button->setEnabled(false);
    toolbar_left_layout->addWidget(toolbar_undo_button);
    toolbar_left_layout->addWidget(toolbar_redo_button);
    toolbar_left_layout->addWidget(toolbar_snap_button);
    toolbar_left_layout->addWidget(toolbar_save_button);
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
    QSlider* zoom_slider =
        new QSlider(Qt::Horizontal, toolbar_right_group);
    zoom_slider->setRange(
        static_cast<int>(TIMELINE_ZOOM_MIN),
        static_cast<int>(TIMELINE_ZOOM_MAX));
    zoom_slider->setValue(static_cast<int>(GRID_WIDTH));
    zoom_slider->setFixedWidth(96);
    zoom_slider->setFocusPolicy(Qt::NoFocus);
    zoom_slider->setToolTip("Timeline zoom");
    toolbar_right_layout->addWidget(zoom_slider);
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
    QWidget* toolbar_playback_group = new QWidget(toolbar);
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
    toolbar_layout->addWidget(
        toolbar_playback_group,
        0,
        1,
        Qt::AlignCenter);
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
    settings_stack = new QStackedWidget(this);
    music_timer = new QTimer(this);
    music_timer->setInterval(33);
    music_timer->setTimerType(Qt::PreciseTimer);
    runtime_boundary_timer = new QTimer(this);
    runtime_boundary_timer->setSingleShot(true);
    runtime_boundary_timer->setTimerType(Qt::PreciseTimer);
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

    timeline_editor->SetClipSelectedCallback(
        [this](std::optional<ClipId> clip_id)
        {
            ShowClipSettings(clip_id);
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
        if(music_playing)
        {
            SyncRuntime(MusicPositionMs());
        }
        CommitHistorySnapshot();
    });
    timeline_editor->SetLaneRenamedCallback(
        [this](int lane_index, const QString& name)
        {
            return backend.RenameLane(lane_index, name);
        });
    timeline_editor->SetLaneHighlightedCallback(
        [this](int lane_index, bool highlighted)
        {
            return backend.SetLaneHighlighted(
                lane_index,
                highlighted);
        });
    timeline_editor->SetLaneDisabledCallback(
        [this](int lane_index, bool disabled)
        {
            return backend.SetLaneDisabled(
                lane_index,
                disabled);
        });

    QWidget* right_column = new QWidget(this);
    QVBoxLayout* right_layout =
        new QVBoxLayout(right_column);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(0);
    QLabel* settings_header =
        HeaderLabel("Effect Settings", right_column);
    settings_header->setFixedHeight(
        static_cast<int>(RULER_HEIGHT));
    right_layout->addWidget(settings_header);
    right_layout->addWidget(settings_stack);
    right_column->setMinimumWidth(
        static_cast<int>(SETTINGS_PANEL_MIN_WIDTH));
    right_column->setSizePolicy(
        QSizePolicy::Preferred,
        QSizePolicy::Expanding);

    QSplitter* content_splitter =
        new QSplitter(Qt::Horizontal, content);
    content_splitter->setChildrenCollapsible(false);
    content_splitter->setHandleWidth(static_cast<int>(GAP));
    content_splitter->addWidget(timeline_editor);
    content_splitter->addWidget(right_column);
    content_splitter->setStretchFactor(0, 1);
    content_splitter->setStretchFactor(1, 0);
    content_splitter->setSizes({
        static_cast<int>(
            TIMELINE_MIN_WIDTH
            + LABEL_WIDTH
            + EFFECTS_PANEL_WIDTH
            + GAP),
        static_cast<int>(SETTINGS_PANEL_PREFERRED_WIDTH)
    });

    content_layout->addWidget(content_splitter, 1);
    page_layout->addWidget(toolbar);
    page_layout->addSpacing(
        static_cast<int>(TOOLBAR_SEPARATOR_GAP));
    page_layout->addWidget(toolbar_separator);
    page_layout->addWidget(content, 1);

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
        toolbar_save_button,
        &QPushButton::clicked,
        this,
        [this]() { SaveLayout(); });
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
            timeline_editor->SetHorizontalZoom(value);
        });
    connect(
        music_timer,
        &QTimer::timeout,
        this,
        [this]() { UpdateMusicPosition(); });
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
