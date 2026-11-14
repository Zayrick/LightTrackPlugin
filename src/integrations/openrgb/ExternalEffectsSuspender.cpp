#include "integrations/openrgb/ExternalEffectsSuspender.h"
#include "integrations/openrgb/PlaybackDiagnostics.h"

#include <QAbstractButton>
#include <QApplication>
#include <QLabel>
#include <QStringList>
#include <QWidget>

#include <cstring>

namespace lighttrack::openrgb
{
namespace
{
// Names defined by the OpenRGB Effects plugin UI (OpenRGBEffectTab.h,
// EffectTabHeader.h/.ui). Each effect tab header owns a "start_stop" toggle
// whose tooltip reflects the effect state.
constexpr const char* EFFECTS_TAB_CLASS = "OpenRGBEffectTab";
constexpr const char* EFFECT_HEADER_CLASS = "EffectTabHeader";
constexpr const char* START_STOP_NAME = "start_stop";
constexpr const char* STARTED_TOOLTIP = "Started";

bool IsClass(const QObject* object, const char* class_name)
{
    return object != nullptr
        && std::strcmp(
            object->metaObject()->className(),
            class_name) == 0;
}

bool IsRunning(const QAbstractButton* toggle)
{
    return toggle->toolTip() == QLatin1String(STARTED_TOOLTIP);
}

QVector<QAbstractButton*> FindEffectToggles()
{
    QVector<QAbstractButton*> toggles;
    if(qApp == nullptr)
    {
        return toggles;
    }

    for(QWidget* widget : QApplication::allWidgets())
    {
        if(!IsClass(widget, EFFECTS_TAB_CLASS))
        {
            continue;
        }

        for(QAbstractButton* toggle :
            widget->findChildren<QAbstractButton*>(
                QLatin1String(START_STOP_NAME)))
        {
            if(IsClass(toggle->parent(), EFFECT_HEADER_CLASS))
            {
                toggles.push_back(toggle);
            }
        }
    }
    return toggles;
}
}

ExternalEffectsSuspender::~ExternalEffectsSuspender()
{
    Resume();
}

void ExternalEffectsSuspender::Suspend()
{
    if(suspended)
    {
        return;
    }
    suspended = true;
    stopped_toggles.clear();

    const QVector<QAbstractButton*> toggles = FindEffectToggles();
    QStringList report;
    for(QAbstractButton* toggle : toggles)
    {
        const QLabel* name =
            toggle->parentWidget()->findChild<QLabel*>("effect_name");
        const QString label = name != nullptr ? name->text() : QString("?");
        const bool running = IsRunning(toggle);
        if(toggle->isEnabled() && running)
        {
            toggle->click();
            stopped_toggles.push_back(toggle);
        }
        report << QString("%1 [%2 -> %3]")
            .arg(label)
            .arg(running ? "running" : "stopped")
            .arg(IsRunning(toggle) ? "still running" : "stopped");
    }
    PlaybackDiagnostics::Note(QString(
        "Effects plugin: %1 effect tabs found, %2 paused: %3")
        .arg(toggles.size())
        .arg(stopped_toggles.size())
        .arg(report.join(", ")));
}

void ExternalEffectsSuspender::Resume()
{
    if(!suspended)
    {
        return;
    }
    suspended = false;

    // Skip effects that were closed or restarted by the user meanwhile.
    for(const QPointer<QAbstractButton>& toggle : stopped_toggles)
    {
        if(toggle && toggle->isEnabled() && !IsRunning(toggle))
        {
            toggle->click();
        }
    }
    stopped_toggles.clear();
}
}
