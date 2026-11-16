#include "application.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>

OBS_DECLARE_MODULE()
MODULE_EXPORT const char *obs_module_description(void) { return "YT-Chatterino mixed chat dock"; }

namespace {
constexpr const char *dockId = "yt-chatterino-mixed-chat";
QPointer<yc::MainWindow> dock;
}

bool obs_module_load(void) {
    if (!qobject_cast<QApplication *>(QCoreApplication::instance())) return false;
    QString profile = qEnvironmentVariable("YTCHATTERINO_OBS_PROFILE");
    if (profile.isEmpty())
        profile = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
                      .filePath("YT-Chatterino/OBS");
    auto *window = new yc::MainWindow(profile, qEnvironmentVariableIsSet("YTCHATTERINO_OBS_TEST_MODE"), true, true);
    window->setObjectName("yt-chatterino-obs-dock");
    window->setWindowFlags(Qt::Widget);
    window->setStyleSheet("QMainWindow,QDialog{background:#000;color:#efeff1;} "
                          "QLineEdit,QComboBox{background:#111;color:#efeff1;border:1px solid #333;padding:3px;} "
                          "QPushButton,QToolButton{background:#181818;color:#efeff1;border:0;padding:5px;} "
                          "QToolButton:hover,QPushButton:hover{background:#303030;} QLabel{color:#efeff1;}");
    if (!obs_frontend_add_dock_by_id(dockId, "YT-Chatterino", window)) { delete window; return false; }
    dock = window;
    blog(LOG_INFO, "[YT-Chatterino] OBS dock registered");
    return true;
}

void obs_module_unload(void) {
    obs_frontend_remove_dock(dockId);
    if (dock) { delete dock; dock = nullptr; }
}
