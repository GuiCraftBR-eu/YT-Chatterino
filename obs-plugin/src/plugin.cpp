#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <QtGlobal>
#include <windows.h>
#include <cstring>

extern "C" __declspec(dllexport) bool obs_module_load();

namespace {
constexpr const char *DockId = "yt-chatterino-mixed-chat";
using AddDock = bool (*)(const char *, const char *, void *);
QPointer<QWidget> dock;

QString sidecarPath() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&obs_module_load), &module)) return {};
    wchar_t path[32768];
    const DWORD length = GetModuleFileNameW(module, path, DWORD(std::size(path)));
    if (!length || length >= std::size(path)) return {};
    QDir root = QFileInfo(QString::fromWCharArray(path, int(length))).absoluteDir();
    const QString current = root.filePath("data/sidecar/YT-Chatterino.exe");
    if (QFileInfo::exists(current)) return current;
    if (!root.cdUp() || !root.cdUp()) return {};
    return root.filePath("data/obs-plugins/yt-chatterino-obs/sidecar/YT-Chatterino.exe");
}

class ChatDock final : public QWidget {
  public:
    explicit ChatDock(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("yt-chatterino-obs-dock");
        setMinimumSize(220, 160);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
        notice = new QLabel(tr("Starting YT-Chatterino…"), this);
        notice->setAlignment(Qt::AlignCenter); notice->setWordWrap(true);
        retry = new QPushButton(tr("Retry"), this); retry->hide();
        layout->addWidget(notice, 1); layout->addWidget(retry);
        connect(retry, &QPushButton::clicked, this, [this] { start(); });
        timer.setInterval(100);
        connect(&timer, &QTimer::timeout, this, [this] { attach(); });
        connect(&process, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this,
                [this](int, QProcess::ExitStatus) { clearWindow(); fail(tr("YT-Chatterino stopped. Press Retry to reopen it.")); });
        connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) fail(tr("Could not start YT-Chatterino. Check the plugin installation."));
        });
        QTimer::singleShot(0, this, [this] { start(); });
    }
    ~ChatDock() override {
        timer.stop();
        if (native && IsWindow(native)) PostMessageW(native, WM_CLOSE, 0, 0);
        if (process.state() != QProcess::NotRunning && !process.waitForFinished(2000)) {
            process.kill(); process.waitForFinished(1000);
        }
        clearWindow();
    }
  private:
    QProcess process;
    QTemporaryDir privateDir;
    QTimer timer;
    QLabel *notice = nullptr;
    QPushButton *retry = nullptr;
    QPointer<QWidget> container;
    HWND native = nullptr;
    int attempts = 0;
    void clearWindow() {
        timer.stop(); native = nullptr;
        if (container) { delete container; container = nullptr; }
    }
    void fail(const QString &message) {
        timer.stop(); notice->setText(message); notice->show(); retry->show();
    }
    void start() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            if (!process.waitForFinished(1500)) { fail(tr("Could not restart YT-Chatterino.")); return; }
        }
        clearWindow(); retry->hide(); notice->setText(tr("Starting YT-Chatterino…")); notice->show();
        const QString exe = sidecarPath();
        if (!privateDir.isValid() || !QFileInfo::exists(exe)) {
            fail(tr("YT-Chatterino files are missing. Reinstall the OBS plugin.")); return;
        }
        const QString handlePath = privateDir.filePath("window-id");
        QFile::remove(handlePath);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.remove("QT_PLUGIN_PATH"); environment.remove("QT_QPA_PLATFORM_PLUGIN_PATH");
        environment.remove("QT_QPA_PLATFORM");
        process.setProcessEnvironment(environment);
        process.setWorkingDirectory(QFileInfo(exe).absolutePath());
        QStringList arguments{"--obs-dock", "--obs-window-id", handlePath};
        const QString testProfile = environment.value("YTCHATTERINO_OBS_PROFILE");
        if (!testProfile.isEmpty()) arguments << "--profile" << testProfile;
        process.start(exe, arguments);
        attempts = 0; timer.start();
    }
    void attach() {
        QFile file(privateDir.filePath("window-id"));
        if (!file.open(QIODevice::ReadOnly)) {
            if (++attempts >= 300) fail(tr("YT-Chatterino did not start in time."));
            return;
        }
        bool ok = false;
        const auto id = file.read(40).trimmed().toULongLong(&ok);
        auto handle = reinterpret_cast<HWND>(quintptr(id));
        DWORD owner = 0;
        if (ok && IsWindow(handle)) GetWindowThreadProcessId(handle, &owner);
        if (!ok || !handle || owner != DWORD(process.processId())) {
            fail(tr("Could not attach the YT-Chatterino window.")); return;
        }
        auto *foreign = QWindow::fromWinId(WId(id));
        if (!foreign) { fail(tr("Could not attach the YT-Chatterino window.")); return; }
        native = handle;
        container = QWidget::createWindowContainer(foreign, this);
        container->setObjectName("yt-chatterino-foreign-window");
        container->setFocusPolicy(Qt::StrongFocus);
        layout()->addWidget(container); notice->hide(); retry->hide(); timer.stop();
        qInfo("[YT-Chatterino OBS] Mixed chat attached.");
    }
};
} // namespace

extern "C" __declspec(dllexport) void obs_module_set_pointer(void *) {}
extern "C" __declspec(dllexport) unsigned int obs_module_ver() { return 33u << 24; }
extern "C" __declspec(dllexport) const char *obs_module_description() { return "YT-Chatterino mixed chat dock"; }
extern "C" __declspec(dllexport) bool obs_module_load() {
    if (!qobject_cast<QApplication *>(QCoreApplication::instance())) return false;
    const HMODULE frontend = GetModuleHandleW(L"obs-frontend-api.dll");
    const FARPROC address = frontend ? GetProcAddress(frontend, "obs_frontend_add_dock_by_id") : nullptr;
    AddDock addDock = nullptr;
    static_assert(sizeof(addDock) == sizeof(address));
    std::memcpy(&addDock, &address, sizeof(addDock));
    if (!addDock) return false;
    auto *widget = new ChatDock();
    if (!addDock(DockId, "YT-Chatterino", widget)) { delete widget; return false; }
    dock = widget;
    qInfo("[YT-Chatterino OBS] Dock registered.");
    return true;
}
extern "C" __declspec(dllexport) void obs_module_unload() {
    if (dock) { delete dock; dock = nullptr; }
}
