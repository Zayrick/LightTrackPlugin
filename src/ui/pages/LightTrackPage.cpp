#include "ui/pages/LightTrackPage.h"

#include "application/AudioService.h"
#include "application/LayoutRepository.h"
#include "application/TimelineBackend.h"
#include "ui/UiHelpers.h"
#include "ui/pages/LightTrackPagePrivate.h"
#include "ui/timeline/TimelineEditor.h"

#include <QFont>
#include <QFontDatabase>
#include <QFrame>
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

#include <utility>

namespace
{
using namespace lighttrack::ui::page_detail;

QString LucideFontFamily()
{
    static const QString family = []()
    {
        const int font_id = QFontDatabase::addApplicationFont(
            ":/lighttrack/fonts/lucide.ttf");
        if(font_id < 0)
        {
            return QString();
        }

        const QStringList families =
            QFontDatabase::applicationFontFamilies(font_id);
        return families.isEmpty()
            ? QString()
            : families.first();
    }();
    return family;
}

QPushButton* ToolbarButton(
    ushort codepoint,
    QWidget* parent)
{
    QPushButton* button =
        new QPushButton(QString(QChar(codepoint)), parent);
    button->setFixedSize(
        static_cast<int>(TOOLBAR_HEIGHT),
        static_cast<int>(TOOLBAR_HEIGHT));
    button->setFocusPolicy(Qt::NoFocus);

    QFont font(LucideFontFamily());
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
    std::unique_ptr<TimelineBackend> backend,
    std::unique_ptr<AudioService> audio_service,
    std::unique_ptr<LayoutRepository> layout_repository,
    QWidget* parent) :
    QWidget(parent),
    backend(std::move(backend)),
    audio_service(std::move(audio_service)),
    layout_repository(std::move(layout_repository))
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
            border-radius: 8px;
            padding: 0;
        }

        QWidget#lightTrackToolbar QPushButton:hover {
            background-color: rgba(127, 127, 127, 45);
        }

        QWidget#lightTrackToolbar QPushButton:pressed {
            background-color: rgba(127, 127, 127, 80);
        }

        QWidget#lightTrackToolbar QPushButton#toolbarMusicButton {
            padding: 0 10px;
        }
    )");
    QHBoxLayout* toolbar_layout =
        new QHBoxLayout(toolbar);
    toolbar_layout->setContentsMargins(0, 0, 0, 0);
    toolbar_layout->setSpacing(0);

    toolbar_left_group = new QWidget(toolbar);
    toolbar_left_group->setSizePolicy(
        QSizePolicy::Fixed,
        QSizePolicy::Preferred);
    QHBoxLayout* toolbar_left_layout =
        new QHBoxLayout(toolbar_left_group);
    toolbar_left_layout->setContentsMargins(0, 0, 0, 0);
    toolbar_left_layout->setSpacing(0);
    toolbar_undo_button =
        ToolbarButton(0xE2A1, toolbar_left_group);
    toolbar_redo_button =
        ToolbarButton(0xE2A0, toolbar_left_group);
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
    toolbar_save_button->setAccessibleName("Save layout");
    toolbar_load_button->setAccessibleName("Load layout");
    toolbar_undo_button->setEnabled(false);
    toolbar_redo_button->setEnabled(false);
    toolbar_left_layout->addWidget(toolbar_undo_button);
    toolbar_left_layout->addWidget(toolbar_redo_button);
    toolbar_left_layout->addWidget(toolbar_save_button);
    toolbar_left_layout->addWidget(toolbar_load_button);

    toolbar_right_group = new QWidget(toolbar);
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
        static_cast<int>(TOOLBAR_HEIGHT));
    toolbar_music_button->setFocusPolicy(Qt::NoFocus);
    toolbar_right_layout->addWidget(toolbar_music_button);
    UpdateToolbarSideWidths();

    toolbar_layout->addWidget(toolbar_left_group);
    toolbar_layout->addStretch();
    toolbar_play_button = ToolbarButton(0xE13C, toolbar);
    toolbar_play_button->setEnabled(false);
    toolbar_layout->addWidget(toolbar_play_button);
    toolbar_layout->addStretch();
    toolbar_layout->addWidget(toolbar_right_group);

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
    if(this->backend != nullptr)
    {
        timeline_editor->SetEffects(
            this->backend->Effects());
    }
    settings_stack = new QStackedWidget(this);
    music_timer = new QTimer(this);
    music_timer->setInterval(33);
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
    timeline_editor->SetTimelineChangedCallback([this]()
    {
        UpdateMinimumTimelineDuration();
        CommitHistorySnapshot();
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
        [this]() { ToggleMusicPlayback(); });
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
        history_commit_timer,
        &QTimer::timeout,
        this,
        [this]() { CommitHistorySnapshot(); });

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
    if(backend != nullptr)
    {
        backend->ClearClips();
    }
    ClearAllClipSettings();
}

QWidget* CreateLightTrackPage(
    std::unique_ptr<TimelineBackend> backend,
    std::unique_ptr<AudioService> audio_service,
    std::unique_ptr<LayoutRepository> layout_repository)
{
    return new LightTrackPage(
        std::move(backend),
        std::move(audio_service),
        std::move(layout_repository));
}

void ReloadLightTrackDevices(QWidget* page)
{
    if(page != nullptr)
    {
        static_cast<LightTrackPage*>(page)->ReloadDevices();
    }
}
}
