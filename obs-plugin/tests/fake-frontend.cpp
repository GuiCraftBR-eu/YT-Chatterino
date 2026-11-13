#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>

extern "C" __declspec(dllexport) bool obs_frontend_add_dock_by_id(const char *id, const char *title, void *widget) {
    auto *window = reinterpret_cast<QMainWindow *>(qApp->property("fixture-host").value<quintptr>());
    if (!window || !widget) return false;
    auto *dock = new QDockWidget(QString::fromUtf8(title), window);
    dock->setObjectName(QString::fromUtf8(id));
    dock->setWidget(static_cast<QWidget *>(widget));
    window->addDockWidget(Qt::RightDockWidgetArea, dock);
    return true;
}
