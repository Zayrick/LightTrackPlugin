#include "ui/pages/LightTrackPagePrivate.h"

#include "application/TimelineBackend.h"
#include "ui/timeline/TimelineEditor.h"

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QAction>
#include <QChildEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTimer>

namespace lighttrack::ui
{
bool LightTrackPage::eventFilter(
    QObject* watched,
    QEvent* event)
{
    if(watched != nullptr
        && watched->property(
            "lightTrackHistoryWatched").toBool())
    {
        if(event->type() == QEvent::ChildAdded)
        {
            QPointer<QObject> child =
                static_cast<QChildEvent*>(event)->child();
            QTimer::singleShot(0, this, [this, child]()
            {
                if(child != nullptr)
                {
                    WatchSettingsObject(child);
                }
            });
        }
        else if(event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::KeyRelease
            || event->type() == QEvent::Wheel
            || event->type() == QEvent::FocusOut
            || event->type() == QEvent::Drop
            || event->type() == QEvent::InputMethod)
        {
            ScheduleHistoryCommit();
        }
    }

    return QWidget::eventFilter(watched, event);
}

void LightTrackPage::WatchSettingsObject(QObject* object)
{
    if(object == nullptr
        || object->property(
            "lightTrackHistoryWatched").toBool())
    {
        return;
    }

    object->setProperty("lightTrackHistoryWatched", true);
    object->installEventFilter(this);

    if(QAbstractSlider* slider =
        qobject_cast<QAbstractSlider*>(object))
    {
        connect(
            slider,
            &QAbstractSlider::valueChanged,
            this,
            [this](int) { ScheduleHistoryCommit(); });
    }

    if(QAbstractButton* button =
        qobject_cast<QAbstractButton*>(object))
    {
        connect(
            button,
            &QAbstractButton::clicked,
            this,
            [this](bool) { ScheduleHistoryCommit(); });
        connect(
            button,
            &QAbstractButton::toggled,
            this,
            [this](bool) { ScheduleHistoryCommit(); });
    }

    if(QAction* action = qobject_cast<QAction*>(object))
    {
        connect(
            action,
            &QAction::triggered,
            this,
            [this](bool) { ScheduleHistoryCommit(); });
    }

    if(QComboBox* combo_box =
        qobject_cast<QComboBox*>(object))
    {
        connect(
            combo_box,
            qOverload<int>(
                &QComboBox::currentIndexChanged),
            this,
            [this](int) { ScheduleHistoryCommit(); });
    }

    if(QSpinBox* spin_box =
        qobject_cast<QSpinBox*>(object))
    {
        connect(
            spin_box,
            qOverload<int>(&QSpinBox::valueChanged),
            this,
            [this](int) { ScheduleHistoryCommit(); });
    }

    if(QDoubleSpinBox* spin_box =
        qobject_cast<QDoubleSpinBox*>(object))
    {
        connect(
            spin_box,
            qOverload<double>(
                &QDoubleSpinBox::valueChanged),
            this,
            [this](double) { ScheduleHistoryCommit(); });
    }

    if(QLineEdit* line_edit =
        qobject_cast<QLineEdit*>(object))
    {
        connect(
            line_edit,
            &QLineEdit::editingFinished,
            this,
            [this]() { ScheduleHistoryCommit(); });
    }

    if(QTextEdit* text_edit =
        qobject_cast<QTextEdit*>(object))
    {
        connect(
            text_edit,
            &QTextEdit::textChanged,
            this,
            [this]() { ScheduleHistoryCommit(); });
    }

    if(QPlainTextEdit* text_edit =
        qobject_cast<QPlainTextEdit*>(object))
    {
        connect(
            text_edit,
            &QPlainTextEdit::textChanged,
            this,
            [this]() { ScheduleHistoryCommit(); });
    }

    const QObjectList children = object->children();
    for(QObject* child : children)
    {
        WatchSettingsObject(child);
    }
}

void LightTrackPage::ScheduleHistoryCommit()
{
    if(applying_snapshot
        || !history.IsInitialized()
        || history_commit_timer == nullptr)
    {
        return;
    }

    if(settings_stack != nullptr
        && settings_stack->currentWidget() != nullptr)
    {
        WatchSettingsObject(
            settings_stack->currentWidget());
    }
    history_commit_timer->start();
}

bool LightTrackPage::EnsureClipEffect(ClipId clip_id)
{
    const std::optional<TimelineClip> clip =
        timeline_editor->FindClip(clip_id);
    if(!clip.has_value() || backend == nullptr)
    {
        return false;
    }

    QString ignored_error;
    return backend->EnsureEffect(
        clip->id,
        clip->effect_id,
        &ignored_error);
}

void LightTrackPage::CreateClipSettings(ClipId clip_id)
{
    if(!clip_id.IsValid()
        || backend == nullptr
        || clip_settings.find(clip_id)
            != clip_settings.end())
    {
        return;
    }

    QWidget* page =
        backend->CreateSettingsPage(clip_id, nullptr);
    if(page == nullptr)
    {
        return;
    }

    QScrollArea* scroll_area =
        new QScrollArea(settings_stack);
    scroll_area->setWidgetResizable(true);
    scroll_area->setFrameShape(QFrame::NoFrame);
    scroll_area->setHorizontalScrollBarPolicy(
        Qt::ScrollBarAlwaysOff);
    scroll_area->setWidget(page);
    settings_stack->addWidget(scroll_area);

    clip_settings[clip_id] = scroll_area;
    WatchSettingsObject(page);
}

void LightTrackPage::ShowClipSettings(
    std::optional<ClipId> clip_id)
{
    if(!clip_id.has_value())
    {
        settings_stack->setCurrentWidget(
            settings_placeholder);
        return;
    }

    if(!EnsureClipEffect(*clip_id))
    {
        settings_placeholder->setText(
            "This effect could not be configured.");
        settings_stack->setCurrentWidget(
            settings_placeholder);
        return;
    }

    CreateClipSettings(*clip_id);
    settings_placeholder->setText(
        "Select an effect card on the timeline to configure it.");
    const auto found = clip_settings.find(*clip_id);
    if(found != clip_settings.end())
    {
        settings_stack->setCurrentWidget(found->second);
    }
}

void LightTrackPage::RemoveClipSettings(ClipId clip_id)
{
    if(!clip_id.IsValid())
    {
        return;
    }

    if(backend != nullptr && !replacing_layout_effects)
    {
        // Destroy the signal sender first so every vendor settings-page
        // callback is disconnected before its captured UI is released.
        backend->RemoveClip(clip_id);
    }

    const auto settings = clip_settings.find(clip_id);
    if(settings == clip_settings.end())
    {
        return;
    }

    QWidget* settings_widget = settings->second;
    if(settings_stack->currentWidget() == settings_widget)
    {
        settings_stack->setCurrentWidget(
            settings_placeholder);
    }

    settings_stack->removeWidget(settings_widget);
    delete settings_widget;
    clip_settings.erase(settings);
}

void LightTrackPage::ClearAllClipSettings()
{
    if(settings_stack != nullptr
        && settings_placeholder != nullptr)
    {
        settings_stack->setCurrentWidget(
            settings_placeholder);
    }

    for(auto& entry : clip_settings)
    {
        if(settings_stack != nullptr)
        {
            settings_stack->removeWidget(entry.second);
        }
        delete entry.second;
    }
    clip_settings.clear();
}
}
