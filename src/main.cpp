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
    parser.process(app);
    if (parser.isSet("apply-update")) {
        return yc::Updater::applyUpdate(parser.value("apply-update"));
    }
    yc::MainWindow window(parser.value("profile"));
    window.show();
    yc::darkTitlebar(&window);
    if (parser.isSet("updated")) {
        const auto manifest = parser.value("updated");
        QTimer::singleShot(500, &window, [manifest] { yc::Updater::acknowledge(manifest); });
    }
    return app.exec();
}
