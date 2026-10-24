#include "updater.hpp"
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <signal.h>
#include <unistd.h>
#endif
#include <stdexcept>

namespace yc {
static QString assetPrefix() {
    return "https://github.com/" + Repo + "/releases/download/";
}

static QString archiveTool() {
#ifdef Q_OS_WIN
    return qEnvironmentVariable("SystemRoot", "C:/Windows") + "/System32/tar.exe";
#else
    auto tool = QStandardPaths::findExecutable("bsdtar");
    return tool.isEmpty() ? QStandardPaths::findExecutable("tar") : tool;
#endif
}

Updater::Updater(Config *c, Network *n, QWidget *w, QObject *p) : QObject(p), cfg(c), net(n), window(w) {
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, this, &Updater::check);
}

QString Updater::kind() const {
#ifdef Q_OS_WIN
    return QFileInfo::exists(QCoreApplication::applicationDirPath() + "/update-manifest.json")
               ? "windows"
               : "development";
#elif defined(Q_OS_LINUX)
    return QCoreApplication::applicationDirPath() == "/usr/bin" ? "pacman" : "development";
#else
    return "development";
#endif
}

void Updater::start() {
    if (cfg->data["automatic_updates"].toBool(true)) {
        timer.start(2500);
    }
}

bool Updater::hasUpdate() const {
    return versionNewer(str(available.value("version")), APP_VERSION);
}

QJsonObject Updater::releaseInfo(const QJsonObject &d, const QString &current, const QString &kind,
                                 QString *error) {
    if (d["draft"].toBool() || d["prerelease"].toBool()) {
        return {};
    }
    QString version = str(d["tag_name"]);
    if (!validVersion(version)) {
        *error = "Invalid release version.";
        return {};
    }
    if (!versionNewer(version, current)) {
        return {};
    }
    if (version.startsWith('v')) {
        version.remove(0, 1);
    }
    if (kind == "development") {
        return {{"version", version}, {"kind", kind}, {"published", d["published_at"]}};
    }
    QString pattern = kind == "windows"
                          ? "^YT-Chatterino-Windows11-" + QRegularExpression::escape(version) + "\\.zip$"
                          : "^yt-chatterino-" + QRegularExpression::escape(version) +
                                "-[0-9]+-(x86_64|any)\\.pkg\\.tar\\.zst$";
    for (const auto &v : d["assets"].toArray()) {
        auto asset = v.toObject();
        QString name = str(asset["name"]);
        if (!QRegularExpression(pattern).match(name).hasMatch()) {
            continue;
        }
        QString expected = assetPrefix() + "v" + version + '/' + name, digest = str(asset["digest"]);
        qint64 size = asset["size"].toInteger();
        if (str(asset["browser_download_url"]) != expected ||
            !QRegularExpression("^sha256:[0-9a-f]{64}$").match(digest).hasMatch() || size <= 0 ||
            size > 256 * 1024 * 1024) {
            *error = "The release has no valid download checksum or size.";
            return {};
        }
        return {{"version", version},
                {"url", expected},
                {"name", name},
                {"sha256", digest.mid(7)},
                {"size", size},
                {"kind", kind},
                {"published", d["published_at"]}};
    }
    *error = "The update package is not available yet. Check again later.";
    return {};
}

void Updater::check() {
    if (busy) {
        return;
    }
    timer.stop();
    busy = true;
    state = "checking";
    error.clear();
    render();
    net->json(QUrl("https://api.github.com/repos/" + Repo + "/releases/latest"),
              [this](QJsonObject d, QString e) {
                  busy = false;
                  if (e.isEmpty()) {
                      available = releaseInfo(d, APP_VERSION, kind(), &e);
                  }
                  error = e;
                  state = e.isEmpty() ? (available.isEmpty() ? "current" : "available") : "error";
                  render();
                  emit changed();
                  if (cfg->data["automatic_updates"].toBool(true)) {
                      timer.start(6 * 60 * 60 * 1000);
                  }
              },
              "GET", {}, {{"X-GitHub-Api-Version", "2022-11-28"}});
}

void Updater::show() {
    if (dialog) {
        dialog->show();
        dialog->raise();
        render();
        return;
    }
    auto *d = new QDialog(window);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(cfg->T("App updates"));
    auto *l = new QVBoxLayout(d);
    l->addWidget(new QLabel(cfg->T("Installed version: {version}", {{"version", APP_VERSION}}), d));
    status = new QLabel(d);
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    l->addWidget(status);
    auto *help =
        new QLabel(cfg->T("Installing closes and restarts the app. Your accounts and settings are kept."), d);
    help->setWordWrap(true);
    help->setStyleSheet("color:#8b8b94;");
    l->addWidget(help);
    auto *row = new QHBoxLayout;
    auto *checkButton = new QPushButton(cfg->T("Check now"), d), *close = new QPushButton(cfg->T("Close"), d);
    installButton = new QPushButton(cfg->T("Install update and restart"), d);
    row->addWidget(checkButton);
    row->addWidget(close);
    row->addStretch();
    row->addWidget(installButton);
    l->addLayout(row);
    connect(checkButton, &QPushButton::clicked, this, &Updater::check);
    connect(close, &QPushButton::clicked, d, &QDialog::close);
    connect(installButton, &QPushButton::clicked, this, &Updater::install);
    dialog = d;
    connect(d, &QObject::destroyed, this, [this] {
        status = nullptr;
        installButton = nullptr;
    });
    d->resize(560, 220);
    d->show();
    darkTitlebar(d);
    render();
    if (state.isEmpty()) {
        check();
    }
}

void Updater::render() {
    if (!dialog || !status || !installButton) {
        return;
    }
    QString message;
    if (state == "checking") {
        message = cfg->T("Checking for updates…");
    } else if (state == "downloading") {
        message = cfg->T("Downloading update…");
    } else if (state == "preparing") {
        message = cfg->T("Verifying and preparing update…");
    } else if (!error.isEmpty()) {
        message = cfg->T(error);
    } else if (!hasUpdate()) {
        message = cfg->T("You are up to date.");
    } else {
        message = cfg->T(
            "Version {version} is available · {date}",
            {{"version", str(available["version"])}, {"date", cfg->date(str(available["published"]))}});
    }
    status->setText(message);
    installButton->setEnabled(hasUpdate() && !busy);
    installButton->setText(
        cfg->T(kind() == "development" ? "Open release page" : "Install update and restart"));
}

void Updater::install() {
    if (busy || !hasUpdate()) {
        return;
    }
    if (kind() == "development") {
        QDesktopServices::openUrl(QUrl("https://github.com/" + Repo + "/releases/latest"));
        return;
    }
    busy = true;
    error.clear();
    state = "downloading";
    render();
    auto info = available;
    net->bytes(
        QUrl(str(info["url"])),
        [this, info](QByteArray bytes, QString e) {
            if (e.isEmpty() &&
                (bytes.size() != info["size"].toInteger() ||
                 QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) !=
                     str(info["sha256"]))) {
                e = "Update checksum verification failed. No files were changed.";
            }
            if (!e.isEmpty()) {
                error = e;
                state = "error";
                busy = false;
                render();
                return;
            }
            state = "preparing";
            render();
            prepare(bytes);
        },
        "GET", {}, {}, 256 * 1024 * 1024);
}

bool Updater::safePath(QString path) {
    path.replace('\\', '/');
    if (path.isEmpty() || path.startsWith('/') || path.contains(':') ||
        path.contains(QRegularExpression("[\\x00-\\x1f]"))) {
        return false;
    }
    for (const auto &part : path.split('/')) {
        if (part == ".." || part == "." || part.endsWith('.') || part.endsWith(' ')) {
            return false;
        }
    }
    return true;
}

bool Updater::copyTree(const QString &from, const QString &to, QString *error) {
    QDir().mkpath(to);
    QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        auto info = it.fileInfo();
        QString rel = QDir(from).relativeFilePath(info.absoluteFilePath());
        if (!safePath(rel) || info.isSymLink()) {
            *error = "Symbolic links are not allowed in an update.";
            return false;
        }
        QString target = to + '/' + rel;
        if (info.isDir()) {
            QDir().mkpath(target);
            continue;
        }
        QDir().mkpath(QFileInfo(target).path());
        if (QFileInfo::exists(target) && !QFile::remove(target)) {
            *error = "Could not replace an update file.";
            return false;
        }
        if (!QFile::copy(info.absoluteFilePath(), target)) {
            *error = "Could not copy an update file: " + rel;
            return false;
        }
    }
    return true;
}

void Updater::prepare(QByteArray bytes) {
    const QString target = QCoreApplication::applicationDirPath();
    const QString container = QFileInfo(target).dir().absoluteFilePath(
        ".ytchatterino-update-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(container)) {
        error = "The app folder is not writable. Extract the app to a writable folder.";
        busy = false;
        render();
        return;
    }
    QString download = container + '/' + str(available["name"]);
    QSaveFile file(download);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = "Could not save the update download.";
        busy = false;
        render();
        return;
    }
    if (kind() == "pacman") {
        auto *process = new QProcess(this);
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, process](int code, QProcess::ExitStatus) {
                    process->deleteLater();
                    if (code) {
                        error = "The package update was cancelled or failed.";
                        busy = false;
                        render();
                        return;
                    }
                    QProcess::startDetached(QCoreApplication::applicationFilePath(), {},
                                            QCoreApplication::applicationDirPath());
                    emit restartRequested();
                });
        connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) {
                error = "Install polkit to allow package updates from the app.";
                busy = false;
                render();
            }
        });
        process->start("pkexec", {"pacman", "-U", "--noconfirm", download});
        return;
    }
    auto *listing = new QProcess(this);
    auto fail = [this](QString e) {
        error = e;
        busy = false;
        state = "error";
        render();
    };
    connect(listing, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, listing, download, container, target, fail](int code, QProcess::ExitStatus) {
                auto raw = listing->readAllStandardOutput();
                listing->deleteLater();
                if (code || raw.size() > 4 * 1024 * 1024) {
                    fail("Could not inspect the update archive.");
                    return;
                }
                for (QString line : QString::fromUtf8(raw).split('\n', Qt::SkipEmptyParts)) {
                    line = line.trimmed();
                    if (!safePath(line) || !(line == "YT-Chatterino/" || line.startsWith("YT-Chatterino/"))) {
                        fail("Unsafe path in the update archive.");
                        return;
                    }
                }
                auto *types = new QProcess(this);
                connect(
                    types, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                    [this, types, download, container, target, fail](int result, QProcess::ExitStatus) {
                        auto lines =
                            QString::fromUtf8(types->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
                        types->deleteLater();
                        if (result) {
                            fail("Could not inspect update file types.");
                            return;
                        }
                        for (const auto &line : lines) {
                            if (!line.startsWith('-') && !line.startsWith('d')) {
                                fail("Links are not allowed in an update archive.");
                                return;
                            }
                        }
                        QString extracted = container + "/extracted";
                        QDir().mkpath(extracted);
                        auto *unpack = new QProcess(this);
                        connect(unpack, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                                [this, unpack, extracted, container, target, fail](int code,
                                                                                   QProcess::ExitStatus) {
                                    unpack->deleteLater();
                                    if (code) {
                                        fail("The update archive could not be extracted.");
                                        return;
                                    }
                                    QString stage = extracted + "/YT-Chatterino";
                                    auto packaged = readObject(stage + "/update-manifest.json"),
                                         current = readObject(target + "/update-manifest.json");
                                    if (str(packaged["version"]) != str(available["version"]) ||
                                        !QFileInfo::exists(stage + "/YT-Chatterino.exe") ||
                                        current["files"].toArray().isEmpty()) {
                                        fail("The update package or installed manifest is incomplete.");
                                        return;
                                    }
                                    for (const auto &v : packaged["files"].toArray()) {
                                        QString rel = v.toString();
                                        auto info = QFileInfo(stage + '/' + rel);
                                        if (!safePath(rel) || !info.isFile() || info.isSymLink()) {
                                            fail("The update manifest contains an invalid file.");
                                            return;
                                        }
                                    }
                                    QString helper = container + "/helper";
                                    QDir().mkpath(helper);
                                    for (const auto &v : current["files"].toArray()) {
                                        QString rel = v.toString();
                                        if (!safePath(rel)) {
                                            fail("Invalid installed manifest.");
                                            return;
                                        }
                                        QString from = target + '/' + rel, to = helper + '/' + rel;
                                        if (!QFileInfo(from).isFile()) {
                                            continue;
                                        }
                                        if (QFileInfo(from).isSymLink()) {
                                            fail("Links are not allowed in the application folder.");
                                            return;
                                        }
                                        QDir().mkpath(QFileInfo(to).path());
                                        if (!QFile::copy(from, to)) {
                                            fail("Could not prepare the update helper.");
                                            return;
                                        }
                                    }
                                    QJsonObject manifest{
                                        {"kind", "windows"},
                                        {"target", target},
                                        {"stage", stage},
                                        {"container", container},
                                        {"version", available["version"]},
                                        {"parent_pid", QCoreApplication::applicationPid()},
                                        {"profile", cfg->home},
                                        {"backup", QFileInfo(target).dir().absoluteFilePath(
                                                       ".ytchatterino-backup-" +
                                                       QUuid::createUuid().toString(QUuid::WithoutBraces))}};
                                    QString path = container + "/update.json";
                                    if (!writeObject(path, manifest) ||
                                        !QProcess::startDetached(helper + "/YT-Chatterino.exe",
                                                                 {"--apply-update", path}, helper)) {
                                        fail("Could not start the update helper.");
                                        return;
                                    }
                                    emit restartRequested();
                                });
                        connect(unpack, &QProcess::errorOccurred, this, [fail](QProcess::ProcessError e) {
                            if (e == QProcess::FailedToStart) {
                                fail("The system archive utility is unavailable.");
                            }
                        });
                        unpack->start(archiveTool(), {"-xf", download, "-C", extracted});
                    });
                types->start(archiveTool(), {"-tvf", download});
                connect(types, &QProcess::errorOccurred, this, [fail](QProcess::ProcessError error) {
                    if (error == QProcess::FailedToStart) {
                        fail("The system archive utility is unavailable.");
                    }
                });
            });
    connect(listing, &QProcess::errorOccurred, this, [fail](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            fail("The system archive utility is unavailable.");
        }
    });
    listing->start(archiveTool(), {"-tf", download});
}

static bool processAlive(qint64 pid) {
    if (pid <= 0) {
        return false;
    }
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!process) {
        return false;
    }
    bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
#else
    return ::kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
}

static void terminateChild(qint64 pid) {
    if (pid <= 0) {
        return;
    }
#ifdef Q_OS_WIN
    HANDLE p = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, DWORD(pid));
    if (p) {
        TerminateProcess(p, 1);
        WaitForSingleObject(p, 10000);
        CloseHandle(p);
    }
#else
    ::kill(pid_t(pid), SIGTERM);
    for (int i = 0; i < 100 && processAlive(pid); ++i) {
        QThread::msleep(100);
    }
#endif
}

static bool samePath(QString a, QString b) {
    a = QDir::fromNativeSeparators(QDir::cleanPath(a));
    b = QDir::fromNativeSeparators(QDir::cleanPath(b));
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

static bool plainDirectory(const QString &path) {
    QFileInfo info(path);
    return info.isAbsolute() && info.isDir() && !info.isSymLink() &&
           samePath(info.absoluteFilePath(), info.canonicalFilePath());
}

bool Updater::validTransaction(const QJsonObject &m, const QString &path, bool staged) {
    QString target = QDir::cleanPath(str(m["target"])), stage = QDir::cleanPath(str(m["stage"])),
            container = QDir::cleanPath(str(m["container"])), backup = QDir::cleanPath(str(m["backup"]));
    QFileInfo manifest(path), backupInfo(backup);
    if (m["kind"].toString() != "windows" || manifest.fileName() != "update.json" || manifest.isSymLink() ||
        !samePath(manifest.absolutePath(), container) ||
        !QFileInfo(container).fileName().startsWith(".ytchatterino-update-") || !plainDirectory(container) ||
        !plainDirectory(target) ||
        !samePath(QFileInfo(target).absolutePath(), QFileInfo(container).absolutePath()) ||
        samePath(target, QDir(target).rootPath()) || !QDir::isAbsolutePath(stage) ||
        !stage.startsWith(container + '/') || !safePath(QDir(container).relativeFilePath(stage)) ||
        !samePath(backupInfo.absolutePath(), QFileInfo(target).absolutePath()) ||
        !backupInfo.fileName().startsWith(".ytchatterino-backup-") || !validVersion(str(m["version"]))) {
        return false;
    }
    if (staged && (!plainDirectory(stage) || backupInfo.exists() || backupInfo.isSymLink())) {
        return false;
    }
    return true;
}

int Updater::applyUpdate(const QString &path, bool interactive) {
    auto manifest = readObject(path);
    QString target = QDir::cleanPath(str(manifest["target"])),
            stage = QDir::cleanPath(str(manifest["stage"])),
            container = QDir::cleanPath(str(manifest["container"])),
            backup = QDir::cleanPath(str(manifest["backup"]));
    bool moved = false, installed = false, validated = false;
    qint64 child = 0;
    try {
        if (!validTransaction(manifest, path)) {
            throw std::runtime_error("Invalid update manifest.");
        }
        validated = true;
        qint64 parent = manifest["parent_pid"].toInteger();
        if (parent <= 0 || parent == QCoreApplication::applicationPid()) {
            throw std::runtime_error("Invalid parent process.");
        }
        for (int i = 0; i < 300 && processAlive(parent); ++i) {
            QThread::msleep(100);
        }
        if (processAlive(parent)) {
            throw std::runtime_error("The application did not close in time.");
        }
        auto old = readObject(target + "/update-manifest.json"),
             fresh = readObject(stage + "/update-manifest.json");
        if (old["files"].toArray().isEmpty() || str(fresh["version"]) != str(manifest["version"])) {
            throw std::runtime_error("Invalid application manifest.");
        }
        for (auto v : fresh["files"].toArray()) {
            QString rel = v.toString();
            QFileInfo info(stage + '/' + rel);
            if (!safePath(rel) || !info.isFile() || info.isSymLink() ||
                !plainDirectory(info.absolutePath())) {
                throw std::runtime_error("Invalid staged application file.");
            }
        }
        QSet<QString> owned;
        for (const auto &v : old["files"].toArray()) {
            if (!safePath(v.toString())) {
                throw std::runtime_error("Invalid installed path.");
            }
            owned.insert(v.toString().toCaseFolded());
        }
        QDirIterator it(target, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            auto info = it.fileInfo();
            QString rel = QDir(target).relativeFilePath(info.absoluteFilePath());
            if (!safePath(rel) || info.isSymLink()) {
                throw std::runtime_error("Links are not allowed in the app folder.");
            }
            if (info.isDir()) {
                if (QFileInfo(stage + '/' + rel).isFile()) {
                    throw std::runtime_error("An update file conflicts with an existing folder.");
                }
                if (!QDir().mkpath(stage + '/' + rel)) {
                    throw std::runtime_error("Could not preserve a user folder.");
                }
                continue;
            }
            if (owned.contains(rel.toCaseFolded())) {
                continue;
            }
            if (QFileInfo::exists(stage + '/' + rel)) {
                throw std::runtime_error("The update would overwrite an unrecognized user file.");
            }
            QDir().mkpath(QFileInfo(stage + '/' + rel).path());
            if (!QFile::copy(info.absoluteFilePath(), stage + '/' + rel)) {
                throw std::runtime_error("Could not preserve a user file.");
            }
        }
        if (!QDir().rename(target, backup)) {
            throw std::runtime_error("Could not back up the application. Close other app instances.");
        }
        moved = true;
        if (!QDir().rename(stage, target)) {
            throw std::runtime_error("Could not install the update.");
        }
        installed = true;
        QString ready = container + "/ready.json";
        QStringList args{"--profile", str(manifest["profile"]), "--updated", ready};
        if (!QProcess::startDetached(target + "/YT-Chatterino.exe", args, target, &child)) {
            throw std::runtime_error("The updated app could not start.");
        }
        for (int i = 0; i < 400; ++i) {
            if (QFileInfo::exists(ready)) {
                writeObject(container + "/done.json", {{"success", true}, {"version", manifest["version"]}});
                return 0;
            }
            if (!processAlive(child)) {
                break;
            }
            QThread::msleep(100);
        }
        throw std::runtime_error("The updated app did not finish starting.");
    } catch (const std::exception &e) {
        if (child && processAlive(child)) {
            terminateChild(child);
        }
        if (installed) {
            QDir().rename(target, container + "/failed-install");
        }
        if (moved) {
            QDir().rename(backup, target);
        }
        if (validated) {
            writeObject(container + "/failure.json", {{"error", QString::fromUtf8(e.what())}});
        }
        if (moved) {
            QProcess::startDetached(target + "/YT-Chatterino.exe", {"--profile", str(manifest["profile"])},
                                    target);
        }
        if (interactive) {
            QMessageBox::critical(nullptr, "YT-Chatterino update", QString::fromUtf8(e.what()));
        }
        return 1;
    }
}

void Updater::acknowledge(const QString &ready) {
    QFileInfo f(ready);
    QString container = f.absolutePath();
    auto manifest = readObject(container + "/update.json");
    if (f.fileName() != "ready.json" || !validTransaction(manifest, container + "/update.json", false) ||
        !samePath(str(manifest["target"]), QCoreApplication::applicationDirPath()) ||
        str(manifest["version"]) != APP_VERSION ||
        str(readObject(QCoreApplication::applicationDirPath() + "/update-manifest.json")["version"]) !=
            APP_VERSION) {
        return;
    }
    writeObject(ready, {{"pid", QCoreApplication::applicationPid()}});
    auto *timer = new QTimer(qApp);
    timer->setInterval(1000);
    QObject::connect(timer, &QTimer::timeout, qApp, [timer, container] {
        if (QFileInfo::exists(container + "/done.json")) {
            timer->stop();
            QDir(container).removeRecursively();
            timer->deleteLater();
        }
    });
    timer->start();
}
} // namespace yc
