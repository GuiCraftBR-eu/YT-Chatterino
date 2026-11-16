#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QPointer>
#include <QTemporaryDir>
#include <dlfcn.h>
#include <iostream>

static QMainWindow *host;
static QPointer<QDockWidget> dock;
extern "C" bool obs_frontend_add_dock_by_id(const char *id, const char *title, void *widget) {
    if (!host || !widget || dock) return false;
    dock = new QDockWidget(QString::fromUtf8(title), host);
    dock->setObjectName(QString::fromUtf8(id));
    dock->setWidget(static_cast<QWidget *>(widget));
    host->addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->show();
    return true;
}
extern "C" void obs_frontend_remove_dock(const char *) {
    if (dock) { host->removeDockWidget(dock); delete dock; dock = nullptr; }
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    QTemporaryDir profile; if (!profile.isValid()) return 3;
    qputenv("YTCHATTERINO_OBS_PROFILE", profile.path().toUtf8());
    qputenv("YTCHATTERINO_OBS_TEST_MODE", "1");
    QApplication app(argc, argv);
    app.setApplicationName("OBS fixture");
    QMainWindow window; host = &window;
    auto *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!module) { std::cerr << dlerror() << '\n'; return 4; }
    auto load = reinterpret_cast<bool (*)()>(dlsym(module, "obs_module_load"));
    auto unload = reinterpret_cast<void (*)()>(dlsym(module, "obs_module_unload"));
    if (!load || !unload || !load() || !dock || !dock->widget()) return 5;
    if (dock->widget()->objectName() != "yt-chatterino-obs-dock" || app.applicationName() != "OBS fixture") return 6;
    window.show(); app.processEvents();
    window.resize(700, 500); app.processEvents();
    if (dock->widget()->width() <= 0 || dock->widget()->height() <= 0) return 7;
    unload(); app.processEvents();
    if (dock) return 8;
    // Qt can still have queued work from the module; process exit unloads it safely.
    std::cout << "PASS native Linux OBS dock registration, resize and unload\n";
    return 0;
}
