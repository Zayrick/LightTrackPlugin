#include "OpenRgbTestHost.h"
#include "ResourceManagerCallback.h"
#include "ui/timeline/internal/TimelineEditorInternal.h"

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QPluginLoader>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>

#include <cstdlib>
#include <functional>
#include <iostream>

namespace
{
using lighttrack::timeline_internal::LaneActionStateRole;
using json = nlohmann::json;

class TestWindowFilter final : public QObject
{
protected:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if(event->type() == QEvent::Polish)
        {
            auto* widget = qobject_cast<QWidget*>(object);
            if(widget != nullptr && widget->isWindow())
            {
                widget->setAttribute(Qt::WA_DontShowOnScreen);
            }
        }
        return QObject::eventFilter(object, event);
    }
};

void Require(bool condition, const char* message)
{
    if(!condition)
    {
        std::cerr << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

QPushButton* Button(QWidget* page, const QString& name)
{
    for(QPushButton* button : page->findChildren<QPushButton*>())
    {
        if(button->accessibleName() == name || button->objectName() == name)
        {
            return button;
        }
    }
    Require(false, "Missing toolbar button");
    return nullptr;
}

QTreeWidget* LaneTree(QWidget* page)
{
    for(QTreeWidget* tree : page->findChildren<QTreeWidget*>())
    {
        if(tree->topLevelItemCount() > 0
            && tree->topLevelItem(0)->text(0) == "Music")
        {
            return tree;
        }
    }
    Require(false, "Missing lane tree");
    return nullptr;
}

void FileAction(QWidget* page, const QString& action, const QString& path)
{
    bool accepted = false;
    QTimer::singleShot(0, [&]()
    {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        Require(dialog != nullptr, "Expected a layout file dialog");
        dialog->selectFile(path);
        accepted = QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
    });
    Button(page, action)->click();
    Require(accepted, "Could not accept the layout file dialog");
}

bool NewAction(
    QWidget* page,
    QMessageBox::StandardButton choice,
    const std::function<void()>& before_choice = {},
    bool use_shortcut = false)
{
    bool prompted = false;
    QTimer responder;
    responder.setSingleShot(true);
    QObject::connect(&responder, &QTimer::timeout, [&]()
    {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        Require(prompt != nullptr && prompt->windowTitle() == "New layout",
            "Expected the unsaved layout prompt");
        Require(prompt->standardButtons()
                == (QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel),
            "The new layout prompt must offer Save, Discard and Cancel");
        Require(prompt->escapeButton() == prompt->button(QMessageBox::Cancel),
            "Closing the prompt must cancel creation");
        prompted = true;
        if(before_choice)
        {
            before_choice();
        }
        prompt->button(choice)->click();
    });
    responder.start(0);
    if(use_shortcut)
    {
        bool activated = false;
        for(QShortcut* shortcut : page->findChildren<QShortcut*>())
        {
            if(shortcut->key() == QKeySequence(QKeySequence::New))
            {
                activated = QMetaObject::invokeMethod(shortcut, "activated", Qt::DirectConnection);
                break;
            }
        }
        Require(activated, "Missing New layout shortcut");
    }
    else
    {
        Button(page, "New layout")->click();
    }
    responder.stop();
    return prompted;
}

json ReadLayout(const QString& path)
{
    QFile file(path);
    Require(file.open(QIODevice::ReadOnly), "Could not open saved layout");
    return json::parse(file.readAll().toStdString());
}

void SetState(QTreeWidgetItem* item, int state)
{
    item->setData(0, LaneActionStateRole, state);
}

int State(QTreeWidgetItem* item)
{
    return item->data(0, LaneActionStateRole).toInt();
}

void AddClip(QWidget* page)
{
    page->resize(1400, 700);
    page->show();
    QApplication::processEvents();
    QMimeData data;
    data.setData("application/x-lighttrack-effect", "SpectrumCycling");
    bool added = false;
    for(QGraphicsView* view : page->findChildren<QGraphicsView*>())
    {
        const QPoint position = view->mapFromScene(QPointF(220, 45));
        QDragEnterEvent enter(position, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &enter);
        QDropEvent drop(position, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &drop);
        added = added || drop.isAccepted();
    }
    Require(added, "Could not add a clip for the layout roundtrip");
}
}

int main(int argc, char** argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    TestWindowFilter window_filter;
    application.installEventFilter(&window_filter);
    Require(argc == 2, "Expected the plugin DLL path");
    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
    auto* plugin = qobject_cast<OpenRGBPluginInterface*>(loader.instance());
    Require(plugin != nullptr, "Could not load the plugin");

    RGBController_Setup setup = TestController::LinearSetup();
    setup.zones[0].segments.resize(2);
    for(unsigned int index = 0; index < 2; ++index)
    {
        segment& part = setup.zones[0].segments[index];
        part.name = index == 0 ? "Left" : "Right";
        part.type = ZONE_TYPE_LINEAR;
        part.start_idx = index;
        part.leds_count = 1;
    }
    TestController output(setup);
    setup = TestController::LinearSetup();
    setup.name = "Other controller";
    TestController other(setup);
    TestPluginAPI api;
    api.devices = {&output, &other};
    Require(api.configuration.isValid(), "Could not create isolated settings directory");
    const QDir directory(api.configuration.path());
    const QString defaults_path = directory.filePath("defaults.lighttrack");
    const QString configured_path = directory.filePath("configured.lighttrack");
    const QString clips_path = directory.filePath("clips.lighttrack");
    const QString roundtrip_path = directory.filePath("roundtrip.lighttrack");
    const QString device_name = QString::fromUtf8(u8"Desk \u706f");

    plugin->Load(&api);
    QWidget* page = plugin->GetWidget();
    page->resize(1400, 700);
    page->show();
    QApplication::processEvents();
    QTreeWidget* tree = LaneTree(page);
    Require(NewAction(page, QMessageBox::Cancel), "A layout that has never been saved must prompt");
    FileAction(page, "Save layout", defaults_path);
    tree->topLevelItem(1)->setText(0, device_name);
    Require(Button(page, "Undo")->isEnabled(), "A lane rename did not enter history");
    Button(page, "Undo")->click();
    Require(tree->topLevelItem(1)->text(0) == "Router output", "Undo did not restore the device name");
    Button(page, "Redo")->click();
    Require(tree->topLevelItem(1)->text(0) == device_name, "Redo did not restore the device name");

    auto* zone_item = tree->topLevelItem(1)->child(0);
    zone_item->setText(0, "Desk zone");
    zone_item->child(1)->setText(0, "Desk segment");
    SetState(zone_item, 1);
    Require(output.colors[0] == ToRGBColor(255, 255, 255), "Highlight did not light the zone");
    Button(page, "Undo")->click();
    Require(State(tree->topLevelItem(1)->child(0)) == 0 && output.colors[0] == 0,
        "Undo did not restore the highlight state and output");
    Button(page, "Redo")->click();
    zone_item = tree->topLevelItem(1)->child(0);
    Require(State(zone_item) == 1, "Redo did not restore the highlight state");
    SetState(zone_item->child(0), 2);
    Require(output.colors[0] == 0, "Disable did not override the parent highlight");
    Button(page, "Undo")->click();
    Require(State(tree->topLevelItem(1)->child(0)->child(0)) == 0
        && output.colors[0] == ToRGBColor(255, 255, 255),
        "Undo did not restore the disabled state and output");
    Button(page, "Redo")->click();
    zone_item = tree->topLevelItem(1)->child(0);
    SetState(zone_item->child(1), 1);
    SetState(tree->topLevelItem(2), 3);
    FileAction(page, "Save layout", configured_path);
    const json configured = ReadLayout(configured_path);
    Require(configured.at("clips").empty(), "Expected a layout without clips");
    Require(configured.at("lanes").size() == 6, "Settings must include every device, zone and segment");
    plugin->Unload();

    // A new backend and reordered devices must recover settings by identity.
    api.devices = {&other, &output};
    plugin->Load(&api);
    page = plugin->GetWidget();
    tree = LaneTree(page);
    FileAction(page, "Load layout", configured_path);
    const auto check_configured = [&]()
    {
        Require(tree->topLevelItem(2)->text(0) == device_name, "Device name was not restored after reload");
        auto* zone = tree->topLevelItem(2)->child(0);
        Require(zone->text(0) == "Desk zone" && zone->child(1)->text(0) == "Desk segment",
            "Zone or segment name was not restored");
        Require(State(zone) == 1 && State(zone->child(0)) == 2 && State(zone->child(1)) == 1
            && State(tree->topLevelItem(1)) == 3, "Lane flags were not restored by device identity");
        Require(output.colors[0] == 0 && output.colors[1] == ToRGBColor(255, 255, 255)
            && other.colors[0] == 0, "Restored lane flags did not reach the output");
    };
    check_configured();
    Button(page, "Undo")->click();
    Require(tree->topLevelItem(2)->text(0) == "Router output"
        && State(tree->topLevelItem(1)) == 0, "Undo did not restore pre-load lane settings");
    Button(page, "Redo")->click();
    check_configured();
    plugin->ResourceManagerUpdated(RESOURCEMANAGER_UPDATE_REASON_DEVICE_LIST_UPDATED);
    QApplication::processEvents();
    check_configured();

    AddClip(page);
    FileAction(page, "Save layout", clips_path);
    const json with_clips = ReadLayout(clips_path);
    Require(with_clips.at("clips").size() == 1, "Expected one saved clip");
    FileAction(page, "Load layout", defaults_path);
    Require(tree->topLevelItem(2)->text(0) == "Router output"
        && tree->topLevelItem(2)->child(0)->text(0) == "Output"
        && State(tree->topLevelItem(1)) == 0
        && State(tree->topLevelItem(2)->child(0)) == 0
        && State(tree->topLevelItem(2)->child(0)->child(0)) == 0
        && output.colors[1] == 0, "Loading defaults did not clear the previous settings");
    FileAction(page, "Load layout", clips_path);
    check_configured();
    FileAction(page, "Save layout", roundtrip_path);
    Require(ReadLayout(roundtrip_path) == with_clips, "Lane restoration changed the saved clips or settings");

    const auto check_new_layout = [&]()
    {
        Require(tree->topLevelItem(2)->text(0) == "Router output"
            && tree->topLevelItem(2)->child(0)->text(0) == "Output"
            && tree->topLevelItem(2)->child(0)->child(1)->text(0) == "Right"
            && State(tree->topLevelItem(1)) == 0
            && State(tree->topLevelItem(2)->child(0)) == 0
            && State(tree->topLevelItem(2)->child(0)->child(0)) == 0
            && State(tree->topLevelItem(2)->child(0)->child(1)) == 0,
            "New must restore default lane names and flags");
        Require(output.colors[0] == 0 && output.colors[1] == 0 && other.colors[0] == 0,
            "New must clear highlighted output");
        Require(!Button(page, "Undo")->isEnabled() && !Button(page, "Redo")->isEnabled(),
            "New must reset undo and redo history");
        Require(Button(page, "Save layout")->toolTip() == "Save layout (Ctrl+S)",
            "New must forget the previous file path");
        Require(page->findChild<QSlider*>("saturation") == nullptr,
            "New must remove effect settings widgets");
        Require(page->findChild<QPushButton*>("toolbarMusicButton")->text() == "Select Music File"
            && !Button(page, "Play")->isEnabled() && !Button(page, "Stop")->isEnabled(),
            "New must reset music and playback controls");
    };

    Require(!NewAction(page, QMessageBox::Cancel), "An unchanged saved layout must not prompt");
    check_new_layout();
    Require(NewAction(page, QMessageBox::Cancel), "New must start with an unpersisted layout");

    FileAction(page, "Load layout", clips_path);
    Require(!NewAction(page, QMessageBox::Cancel, {}, true), "A freshly loaded layout must not prompt");
    check_new_layout();

    FileAction(page, "Load layout", clips_path);
    tree->topLevelItem(2)->setText(0, "Unsaved device");
    Require(NewAction(page, QMessageBox::Cancel), "Changes since loading must prompt");
    Require(tree->topLevelItem(2)->text(0) == "Unsaved device", "Cancel must preserve the current layout");

    bool save_cancelled = false;
    Require(NewAction(page, QMessageBox::Save, [&]()
    {
        QTimer::singleShot(0, [&]()
        {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            Require(dialog != nullptr, "Save before New must open a file dialog");
            save_cancelled = true;
            dialog->reject();
        });
    }), "Save cancellation test did not prompt");
    Require(save_cancelled && tree->topLevelItem(2)->text(0) == "Unsaved device"
        && Button(page, "Save layout")->toolTip().contains(clips_path),
        "Cancelling Save must preserve the layout and current path");
    Require(NewAction(page, QMessageBox::Cancel), "A cancelled save must leave changes unsaved");

    const QString failed_directory = directory.filePath("save-failure");
    Require(QDir().mkdir(failed_directory), "Could not prepare failing save destination");
    bool save_failed = false;
    Require(NewAction(page, QMessageBox::Save, [&]()
    {
        QTimer::singleShot(0, [&]()
        {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            Require(dialog != nullptr, "Expected Save before New file dialog");
            dialog->selectFile(QDir(failed_directory).filePath("failed.lighttrack"));
            Require(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection),
                "Could not accept failing save destination");
            // Simulate the selected destination disappearing before the write.
            Require(QDir().rmdir(failed_directory), "Could not remove failing save destination");
            QTimer::singleShot(0, [&]()
            {
                auto* error = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                Require(error != nullptr && error->windowTitle() == "Cannot save layout",
                    "Expected a save error");
                save_failed = true;
                error->accept();
            });
        });
    }), "Save failure test did not prompt");
    Require(save_failed && tree->topLevelItem(2)->text(0) == "Unsaved device"
        && Button(page, "Save layout")->toolTip().contains(clips_path),
        "Failed Save must preserve the layout and current path");
    Require(NewAction(page, QMessageBox::Cancel), "Failed Save must leave changes unsaved");

    Button(page, "Undo")->click();
    check_configured();
    Button(page, "Redo")->click();
    Require(NewAction(page, QMessageBox::Cancel), "Redoing an unsaved edit must prompt");
    Button(page, "Undo")->click();
    Require(!NewAction(page, QMessageBox::Cancel), "Undoing back to the persisted state must not prompt");
    check_new_layout();

    FileAction(page, "Load layout", clips_path);
    auto* saturation = page->findChild<QSlider*>("saturation");
    Require(saturation != nullptr, "Missing effect settings slider");
    saturation->setValue(saturation->value() - 1);
    Require(NewAction(page, QMessageBox::Cancel),
        "Effect settings changes must prompt even before the history timer fires");
    const QString before_new_path = directory.filePath("before-new.lighttrack");
    Require(NewAction(page, QMessageBox::Save, [&]()
    {
        QTimer::singleShot(0, [&]()
        {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            Require(dialog != nullptr, "Expected Save before New file dialog");
            dialog->selectFile(before_new_path);
            Require(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection),
                "Could not save before creating a layout");
        });
    }), "Save before New did not prompt");
    check_new_layout();
    const json before_new = ReadLayout(before_new_path);
    Require(before_new.at("clips").size() == 1
        && before_new.at("clips").at(0).at("settings") != with_clips.at("clips").at(0).at("settings"),
        "Save before New must persist the latest effect settings and clips");

    FileAction(page, "Load layout", clips_path);
    tree->topLevelItem(2)->setText(0, "Discard this name");
    Require(NewAction(page, QMessageBox::Discard), "Discard test did not prompt");
    check_new_layout();
    Require(ReadLayout(clips_path) == with_clips, "Discard must not overwrite the existing layout file");
    plugin->ResourceManagerUpdated(RESOURCEMANAGER_UPDATE_REASON_DEVICE_LIST_UPDATED);
    QApplication::processEvents();
    check_new_layout();
    const QString new_path = directory.filePath("new.lighttrack");
    FileAction(page, "Save layout", new_path);
    const json empty_layout = ReadLayout(new_path);
    Require(empty_layout.at("clips").empty() && empty_layout.at("music").at("path") == ""
        && empty_layout.at("music").at("durationMs") == 0,
        "New must remove all persisted music and clips");

    // Even an unavailable music selection is layout content that must be guarded.
    const QString audio_path = directory.filePath("unavailable.wav");
    QFile audio_file(audio_path);
    Require(audio_file.open(QIODevice::WriteOnly), "Could not create the audio test file");
    Require(audio_file.write("invalid audio") > 0, "Could not write the audio test file");
    audio_file.close();
    FileAction(page, "toolbarMusicButton", audio_path);
    Require(Button(page, "toolbarMusicButton")->text() == "unavailable.wav",
        "Could not select music for the new layout test");
    Require(NewAction(page, QMessageBox::Cancel), "Music changes since saving must prompt");
    Require(Button(page, "toolbarMusicButton")->text() == "unavailable.wav",
        "Cancel must preserve the music selection");
    Require(NewAction(page, QMessageBox::Discard), "Discarding music changes must prompt");
    check_new_layout();
    const QString music_reset_path = directory.filePath("music-reset.lighttrack");
    FileAction(page, "Save layout", music_reset_path);
    Require(ReadLayout(music_reset_path) == empty_layout, "New must clear the persisted music selection");

    plugin->Unload();
    Require(api.virtual_controllers.empty(), "Lane persistence test leaked virtual controllers");
    Require(loader.unload(), "Could not unload the plugin");
    return EXIT_SUCCESS;
}
