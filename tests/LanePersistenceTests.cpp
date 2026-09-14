#include "OpenRgbTestHost.h"
#include "ResourceManagerCallback.h"
#include "ui/timeline/internal/TimelineEditorInternal.h"

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QMimeData>
#include <QPluginLoader>
#include <QPushButton>

#include <cstdlib>
#include <iostream>

namespace
{
using lighttrack::timeline_internal::LaneActionStateRole;
using json = nlohmann::json;

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
        if(button->accessibleName() == name)
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
    QTreeWidget* tree = LaneTree(page);
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

    plugin->Unload();
    Require(api.virtual_controllers.empty(), "Lane persistence test leaked virtual controllers");
    Require(loader.unload(), "Could not unload the plugin");
    return EXIT_SUCCESS;
}
