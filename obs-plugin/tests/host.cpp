#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QMainWindow>
#include <QScreen>
#include <QTimer>
#include <windows.h>
#include <iostream>

struct ForeignWindow { DWORD own = 0, child = 0; HWND handle = nullptr; };
static BOOL CALLBACK findForeign(HWND window, LPARAM value) {
    auto *result = reinterpret_cast<ForeignWindow *>(value);
    DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    if (pid && pid != result->own) { result->child = pid; result->handle = window; return FALSE; }
    return TRUE;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc != 3) return 2;
    const auto frontendPath = QString::fromLocal8Bit(argv[1]).toStdWString();
    const auto pluginPath = QString::fromLocal8Bit(argv[2]).toStdWString();
    QMainWindow host; host.setWindowTitle("YT-Chatterino OBS fixture"); host.resize(560, 600);
    app.setProperty("fixture-host", QVariant::fromValue(quintptr(&host)));
    HMODULE frontend = LoadLibraryW(frontendPath.c_str());
    HMODULE plugin = frontend ? LoadLibraryW(pluginPath.c_str()) : nullptr;
    if (!plugin) { std::cerr << "Failed to load fixture module: " << GetLastError() << '\n'; return 3; }
    using Load = bool (*)();
    const auto load = reinterpret_cast<Load>(GetProcAddress(plugin, "obs_module_load"));
    if (!load || !load()) { std::cerr << "Failed to register dock\n"; return 4; }
    host.show();
    QTimer timer; int attempts = 0; DWORD childPid = 0;
    QObject::connect(&timer, &QTimer::timeout, &host, [&] {
        auto *dock = host.findChild<QWidget *>("yt-chatterino-obs-dock");
        auto *container = dock ? dock->findChild<QWidget *>("yt-chatterino-foreign-window") : nullptr;
        ForeignWindow foreign{GetCurrentProcessId(), 0, nullptr};
        if (container && host.winId())
            EnumChildWindows(reinterpret_cast<HWND>(host.winId()), findForeign, reinterpret_cast<LPARAM>(&foreign));
        if (foreign.handle) {
            childPid = foreign.child;
            host.resize(720, 480); app.processEvents();
            RECT bounds{}; GetClientRect(foreign.handle, &bounds);
            if (bounds.right <= 0 || bounds.bottom <= 0) { std::cerr << "Embedded window has no size\n"; app.exit(6); return; }
            std::cout << "PASS registered dock, external process window and resize " << bounds.right << 'x' << bounds.bottom << '\n';
            timer.stop();
            QTimer::singleShot(1000, &host, [&, foreign] {
                if (qEnvironmentVariableIsSet("YTCHATTERINO_OBS_FIXTURE_IMAGE") && QGuiApplication::primaryScreen())
                    QGuiApplication::primaryScreen()->grabWindow(WId(foreign.handle)).save(qEnvironmentVariable("YTCHATTERINO_OBS_FIXTURE_IMAGE"));
                app.exit(0);
            });
        } else if (++attempts > 300) {
            std::cerr << "Timed out waiting for the dock; container=" << bool(container) << '\n';
            if (dock) for (auto *label : dock->findChildren<QLabel *>())
                std::cerr << "Dock status: " << label->text().toStdString() << '\n';
            if (qEnvironmentVariableIsSet("YTCHATTERINO_OBS_FIXTURE_IMAGE"))
                host.grab().save(qEnvironmentVariable("YTCHATTERINO_OBS_FIXTURE_IMAGE"));
            app.exit(5);
        }
    });
    timer.start(100);
    const int result = app.exec();
    using Unload = void (*)();
    const auto unload = reinterpret_cast<Unload>(GetProcAddress(plugin, "obs_module_unload"));
    if (unload) unload();
    if (childPid) {
        HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, childPid);
        if (child) {
            const DWORD waited = WaitForSingleObject(child, 3000); CloseHandle(child);
            if (waited != WAIT_OBJECT_0) { std::cerr << "Child process did not stop\n"; return 7; }
        }
    }
    FreeLibrary(plugin); FreeLibrary(frontend);
    return result;
}
