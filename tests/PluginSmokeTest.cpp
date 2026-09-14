#include "OpenRgbTestHost.h"
#include <QPointer>
#include "OpenRGBPluginInterface.h"
#include "ResourceManagerCallback.h"

#include <QApplication>
#include <QJsonObject>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGraphicsView>
#include <QMimeData>
#include <QPluginLoader>
#include <QTemporaryDir>
#include <QThread>
#include <QTreeWidget>

#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <thread>

namespace
{
void Require(bool condition, const char* message)
{
    if(!condition)
    {
        std::cerr << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

template<typename Function>
void FromWorker(Function function)
{
    std::atomic<bool> done{false};
    std::thread worker([&]()
    {
        function();
        done = true;
    });
    while(!done)
    {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    worker.join();
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    Require(argc == 2, "Expected the plugin DLL path");

    QPluginLoader loader(QString::fromLocal8Bit(argv[1]));
    const QJsonObject metadata = loader.metaData();
    Require(metadata.value("IID").toString() == "org.openrgb.OpenRGBPluginInterface", "Incorrect plugin IID");
    const QJsonObject details = metadata.value("MetaData").toObject();
    Require(details.value("OpenRGBPluginAPIVersion").toInt() == 5, "Missing API 5 metadata");
    Require(details.value("Id").toString() == "org.openrgb.lighttrackplugin", "Missing plugin ID");

    QObject* instance = loader.instance();
    if(instance == nullptr)
    {
        std::cerr << loader.errorString().toStdString() << std::endl;
    }
    auto* plugin = qobject_cast<OpenRGBPluginInterface*>(instance);
    Require(plugin != nullptr, "Could not load or cast the plugin through API 5");
    Require(plugin->GetPluginAPIVersion() == 5, "Incorrect runtime API version");
    Require(plugin->GetPluginInfo().ProtocolVersion == 0, "Unsupported SDK protocol must be zero");

    TestPluginAPI api;
    Require(api.configuration.isValid(), "Could not create isolated settings directory");
    api.on_detection = [&]()
    {
        plugin->ResourceManagerUpdated(RESOURCEMANAGER_UPDATE_REASON_DEVICE_LIST_UPDATED);
        plugin->ResourceManagerUpdated(RESOURCEMANAGER_UPDATE_REASON_DETECTION_COMPLETE);
    };

    for(int cycle = 0; cycle < 2; ++cycle)
    {
        plugin->Load(&api);
        QWidget* page = plugin->GetWidget();
        Require(page != nullptr && page == plugin->GetWidget(), "GetWidget must return the existing page");
        Require(api.detection_waits == static_cast<unsigned int>(cycle + 1), "Detection should be awaited once per page");

        int effects = 0;
        for(QTreeWidget* tree : page->findChildren<QTreeWidget*>())
        {
            for(int category = 0; category < tree->topLevelItemCount(); ++category)
            {
                effects += tree->topLevelItem(category)->childCount();
            }
        }
        Require(effects > 0, "The release_1.0 effect catalog is empty");

        unsigned int queries = api.device_queries;
        plugin->ResourceManagerUpdated(RESOURCEMANAGER_UPDATE_REASON_DETECTION_PROGRESS_CHANGED);
        Require(api.device_queries == queries, "Progress updates must not rebuild the timeline");
        plugin->ResourceManagerUpdated(RESOURCEMANAGER_UPDATE_REASON_DEVICE_LIST_UPDATED);
        QApplication::processEvents();
        Require(api.device_queries == ++queries, "GUI-thread device notifications must rebuild devices");
        FromWorker([&]() { plugin->ResourceManagerUpdated(RESOURCEMANAGER_UPDATE_REASON_DEVICE_LIST_UPDATED); });
        QApplication::processEvents();
        Require(api.device_queries == ++queries, "Worker notifications must queue a GUI rebuild");
        plugin->ResourceManagerUpdated(RESOURCEMANAGER_UPDATE_REASON_DETECTION_COMPLETE);
        QApplication::processEvents();
        Require(api.device_queries == ++queries, "Detection completion must refresh devices");

        plugin->ResourceManagerUpdated(RESOURCEMANAGER_UPDATE_REASON_DETECTION_STARTED);
        for(int device = 0; device < 5; ++device)
        {
            plugin->ResourceManagerUpdated(RESOURCEMANAGER_UPDATE_REASON_DEVICE_LIST_UPDATED);
            QApplication::processEvents();
        }
        Require(api.device_queries == queries, "A rescan must not rebuild intermediate device lists");
        plugin->ResourceManagerUpdated(RESOURCEMANAGER_UPDATE_REASON_DETECTION_COMPLETE);
        QApplication::processEvents();
        Require(api.device_queries == ++queries, "A rescan must produce one final rebuild");

        plugin->OnProfileAboutToLoad();
        FromWorker([&]() { plugin->OnProfileAboutToLoad(); });
        plugin->OnProfileLoad(nlohmann::json::object());
        Require(plugin->OnProfileSave().empty(), "No timeline profile format is currently supported");
        plugin->ProfileManagerUpdated(0);
        plugin->SettingsManagerUpdated(0);
        unsigned int response_size = 99;
        Require(plugin->OnSDKCommand(999, nullptr, &response_size) == nullptr && response_size == 0,
                "Unsupported SDK packets must return an empty response");
        plugin->OnSDKCommand(999, nullptr, nullptr);
        Require(plugin->GetTrayMenu() == nullptr, "Unexpected tray menu");

        QPointer<QWidget> guarded_page(page);
        plugin->Unload();
        Require(guarded_page.isNull(), "Unload must destroy the owned page");
        plugin->ResourceManagerUpdated(RESOURCEMANAGER_UPDATE_REASON_DEVICE_LIST_UPDATED);
        QApplication::processEvents();
        Require(api.device_queries == queries, "Unloaded plugins must not access the host API");
        std::thread late_notification(api.on_detection);
        late_notification.join();
        QApplication::processEvents();
        std::cout << "Load/unload cycle " << cycle + 1 << ": " << effects << " effects" << std::endl;
    }

    // Preserve a real clip across a rescan without reading removed devices.
    TestController output(TestController::LinearSetup());
    api.devices = {&output};
    plugin->Load(&api);
    QWidget* device_page = plugin->GetWidget();
    device_page->resize(1400, 700);
    device_page->show();
    QApplication::processEvents();
    QMimeData effect_data;
    effect_data.setData("application/x-lighttrack-effect", "SpectrumCycling");
    bool added = false;
    for(QGraphicsView* view : device_page->findChildren<QGraphicsView*>())
    {
        const QPoint position = view->mapFromScene(QPointF(220, 45));
        QDragEnterEvent enter(position, Qt::CopyAction, &effect_data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &enter);
        QDropEvent drop(position, Qt::CopyAction, &effect_data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &drop);
        added = added || drop.isAccepted();
    }
    Require(added, "Could not create the clip used by the rescan regression");
    FromWorker([&]() { plugin->ResourceManagerUpdated(RESOURCEMANAGER_UPDATE_REASON_DETECTION_STARTED); });
    output.retired = true;
    api.devices.clear();
    plugin->ResourceManagerUpdated(RESOURCEMANAGER_UPDATE_REASON_DEVICE_LIST_UPDATED);
    plugin->ResourceManagerUpdated(RESOURCEMANAGER_UPDATE_REASON_DETECTION_COMPLETE);
    QApplication::processEvents();
    Require(output.retired_reads == 0, "Layout restoration read a removed controller");
    plugin->Unload();
    Require(api.virtual_controllers.empty(), "Plugin unload leaked virtual controllers");

    // The host can destroy its widget before calling Unload.
    plugin->Load(&api);
    delete plugin->GetWidget();
    plugin->Unload();
    plugin->Load(&api);
    plugin->Unload();
    Require(loader.unload(), "Could not unload the plugin");
    return EXIT_SUCCESS;
}
