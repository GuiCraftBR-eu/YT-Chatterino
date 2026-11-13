#include "application.hpp"
#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>
#endif
int main(int argc, char **argv) {
#ifdef Q_OS_WIN
    SetCurrentProcessExplicitAppUserModelID(L"GuiCraftBR.YTChatterino");
#endif
    QApplication app(argc, argv);
    yc::configureApplication(app);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"profile", "Use an isolated profile directory.", "path"});
    parser.addOption({"apply-update", "Apply a prepared native update.", "manifest"});
    parser.addOption({"updated", "Acknowledge a completed update.", "manifest"});
    parser.addOption({"obs-dock", "Use a single mixed chat for the OBS dock."});
    parser.addOption({"obs-window-id", "Write the OBS dock window handle to a private file.", "path"});
    parser.process(app);
    if (parser.isSet("apply-update")) {
        return yc::Updater::applyUpdate(parser.value("apply-update"));
    }
    const bool obsDock = parser.isSet("obs-dock");
    QString profile = parser.value("profile");
    if (obsDock && profile.isEmpty())
        profile = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("OBS");
    yc::MainWindow window(profile, false, obsDock);
    window.show();
    yc::darkTitlebar(&window);
    if (obsDock && parser.isSet("obs-window-id")) {
        QSaveFile handleFile(parser.value("obs-window-id"));
        if (!handleFile.open(QIODevice::WriteOnly) ||
            handleFile.write(QByteArray::number(quintptr(window.winId()))) <= 0 || !handleFile.commit()) return 2;
    }
    if (parser.isSet("updated")) {
        const auto manifest = parser.value("updated");
        QTimer::singleShot(500, &window, [manifest] { yc::Updater::acknowledge(manifest); });
    }
    return app.exec();
}
