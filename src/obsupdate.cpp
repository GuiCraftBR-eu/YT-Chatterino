#include "obsupdate.hpp"
#include "updater.hpp"

namespace yc {
ObsPluginUpdater::ObsPluginUpdater(Network *network, QObject *parent, QString rootOverride)
    : QObject(parent), net(network), rootPath(rootOverride.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath("YT-Chatterino/OBS/plugin-updates")
        : QDir::cleanPath(rootOverride)) {
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, this, &ObsPluginUpdater::check);
}

void ObsPluginUpdater::start() { timer.start(15000); }

QJsonObject ObsPluginUpdater::releaseInfo(const QJsonObject &release, const QString &current) {
    if (release["draft"].toBool() || release["prerelease"].toBool()) return {};
    QString version = str(release["tag_name"]);
    if (!validVersion(version) || !versionNewer(version, current)) return {};
    if (version.startsWith('v')) version.remove(0, 1);
    const QString name = "YT-Chatterino-OBS-Windows-" + version + ".zip";
    const QString url = "https://github.com/" + Repo + "/releases/download/v" + version + '/' + name;
    for (const auto &value : release["assets"].toArray()) {
        const auto asset = value.toObject();
        const auto digest = str(asset["digest"]);
        const auto size = asset["size"].toInteger();
        if (str(asset["name"]) == name && str(asset["browser_download_url"]) == url &&
            QRegularExpression("^sha256:[a-f0-9]{64}$").match(digest).hasMatch() && size > 0 && size <= 256 * 1024 * 1024)
            return {{"version", version}, {"url", url}, {"size", double(size)}, {"sha256", digest.mid(7)}};
    }
    return {};
}

bool ObsPluginUpdater::safeListing(const QByteArray &listing, QSet<QString> *files) {
    if (listing.isEmpty() || listing.size() > 4 * 1024 * 1024) return false;
    QSet<QString> entries;
    for (const auto &raw : QString::fromUtf8(listing).split('\n', Qt::SkipEmptyParts)) {
        const auto name = raw.trimmed();
        if (!Updater::safePath(name) || !name.startsWith("yt-chatterino-obs/") || name.contains("//")) return false;
        if (name.endsWith('/')) continue;
        if (entries.contains(name) || entries.size() >= 4096) return false;
        entries.insert(name);
    }
    if (files) *files = entries;
    return !entries.isEmpty();
}

void ObsPluginUpdater::check() {
    if (busy) return;
    busy = true;
    QPointer<ObsPluginUpdater> safe(this);
    net->json(QUrl("https://api.github.com/repos/" + Repo + "/releases/latest"), [safe](QJsonObject release, QString error) {
        if (!safe) return;
        const auto info = error.isEmpty() ? releaseInfo(release, APP_VERSION) : QJsonObject();
        if (info.isEmpty()) { safe->busy = false; safe->timer.start((error.isEmpty() ? 6 : 1) * 60 * 60 * 1000); return; }
        const QDir root(safe->rootPath);
        const auto current = readObject(root.filePath("current.json"));
        const auto directory = str(current["directory"]), version = str(current["version"]);
        if (version == str(info["version"]) &&
            QRegularExpression("^[0-9]+\\.[0-9]+\\.[0-9]+-[a-f0-9-]{36}$").match(directory).hasMatch()) {
            QFile core(root.filePath(directory + "/yt-chatterino-obs-core.dll"));
            QCryptographicHash hash(QCryptographicHash::Sha256);
            const auto manifest = readObject(root.filePath(directory + "/data/sidecar/update-manifest.json"));
            if (core.open(QIODevice::ReadOnly) && hash.addData(&core) &&
                QString::fromLatin1(hash.result().toHex()) == str(current["core_sha256"]) &&
                str(manifest["version"]) == version && QFileInfo::exists(root.filePath(directory + "/data/sidecar/YT-Chatterino.exe"))) {
                safe->busy = false; safe->timer.start(6 * 60 * 60 * 1000); return;
            }
        }
        safe->net->bytes(QUrl(str(info["url"])), [safe, info](QByteArray bytes, QString problem) {
            if (!safe) return;
            if (!problem.isEmpty() || bytes.size() != info["size"].toInteger() ||
                QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) != str(info["sha256"])) {
                safe->busy = false; safe->timer.start(60 * 60 * 1000); return;
            }
            safe->stage(info, bytes);
        }, "GET", {}, {}, 256 * 1024 * 1024);
    }, "GET", {}, {{"X-GitHub-Api-Version", "2022-11-28"}});
}

void ObsPluginUpdater::runArchive(const QStringList &arguments, std::function<void(bool, QByteArray)> done) {
    const QString tool = qEnvironmentVariable("SystemRoot", "C:/Windows") + "/System32/tar.exe";
    auto *process = new QProcess(this);
    auto finished = QSharedPointer<bool>::create(false);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [process, finished, done](int code, QProcess::ExitStatus status) {
                if (*finished) return;
                *finished = true; const auto output = process->readAllStandardOutput(); process->deleteLater();
                done(status == QProcess::NormalExit && code == 0, output);
            });
    connect(process, &QProcess::errorOccurred, this, [process, finished, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || *finished) return;
        *finished = true; process->deleteLater(); done(false, {});
    });
    process->start(tool, arguments);
}

void ObsPluginUpdater::stage(const QJsonObject &info, const QByteArray &bytes) {
    auto fail = [safe = QPointer<ObsPluginUpdater>(this)] {
        if (safe) { safe->busy = false; safe->timer.start(60 * 60 * 1000); }
    };
    const QDir root(rootPath);
    if (!QDir().mkpath(root.absolutePath())) { fail(); return; }
    const QFileInfo rootInfo(root.absolutePath());
    if (!rootInfo.isDir() || rootInfo.isSymLink() || rootInfo.isJunction() || rootInfo.canonicalFilePath().isEmpty() ||
        QDir::cleanPath(rootInfo.canonicalFilePath()).compare(QDir::cleanPath(rootInfo.absoluteFilePath()), Qt::CaseInsensitive) != 0) {
        fail(); return;
    }
    auto incoming = QSharedPointer<QTemporaryDir>::create(root.filePath(".incoming-XXXXXX"));
    if (!incoming->isValid() || !QFileInfo(incoming->path()).canonicalFilePath().startsWith(rootInfo.canonicalFilePath() + '/', Qt::CaseInsensitive)) {
        fail(); return;
    }
    const auto zip = incoming->filePath("update.zip");
    QSaveFile file(zip);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { fail(); return; }
    QPointer<ObsPluginUpdater> safe(this);
    runArchive({"-tf", zip}, [safe, incoming, zip, info, root, fail](bool ok, QByteArray listing) {
        if (!safe) return;
        auto entries = QSharedPointer<QSet<QString>>::create();
        if (!ok || !safeListing(listing, entries.data())) { fail(); return; }
        safe->runArchive({"-tvf", zip}, [safe, incoming, zip, info, root, entries, fail](bool ok, QByteArray listing) {
            if (!safe) return;
            if (!ok || listing.size() > 4 * 1024 * 1024) { fail(); return; }
            for (const auto &line : listing.split('\n'))
                if (!line.trimmed().isEmpty() && !line.startsWith('-') && !line.startsWith('d')) { fail(); return; }
            const auto extracted = incoming->filePath("extracted");
            if (!QDir().mkpath(extracted)) { fail(); return; }
            safe->runArchive({"-xf", zip, "-C", extracted}, [safe, incoming, info, root, entries, extracted, fail](bool ok, QByteArray) {
                if (!safe) return;
                if (!ok) { fail(); return; }
                const QDir folder(extracted + "/yt-chatterino-obs");
                const QFileInfo coreInfo(folder.filePath("yt-chatterino-obs-core.dll"));
                const QFileInfo bootInfo(folder.filePath("yt-chatterino-obs.dll"));
                const QFileInfo manifestInfo(folder.filePath("data/sidecar/update-manifest.json"));
                if (!coreInfo.isFile() || coreInfo.isSymLink() || coreInfo.isJunction() || coreInfo.size() > 64 * 1024 * 1024 ||
                    !bootInfo.isFile() || bootInfo.isSymLink() || !manifestInfo.isFile() || manifestInfo.size() > 1024 * 1024) { fail(); return; }
                QFile core(coreInfo.absoluteFilePath()), boot(bootInfo.absoluteFilePath());
                if (!core.open(QIODevice::ReadOnly) || !boot.open(QIODevice::ReadOnly) || core.peek(2) != "MZ" || boot.peek(2) != "MZ") { fail(); return; }
                QCryptographicHash hash(QCryptographicHash::Sha256);
                if (!hash.addData(&core)) { fail(); return; }
                const auto coreHash = QString::fromLatin1(hash.result().toHex());
                const auto manifest = readObject(manifestInfo.absoluteFilePath());
                if (str(manifest["version"]) != str(info["version"]) || manifest["files"].toArray().isEmpty()) { fail(); return; }
                QSet<QString> expected{"yt-chatterino-obs/yt-chatterino-obs.dll", "yt-chatterino-obs/yt-chatterino-obs-core.dll",
                                       "yt-chatterino-obs/LEIA-ME-OBS.txt"};
                for (const auto &value : manifest["files"].toArray()) {
                    const auto name = str(value);
                    if (!Updater::safePath(name)) { fail(); return; }
                    const auto entry = "yt-chatterino-obs/data/sidecar/" + name;
                    const QFileInfo item(folder.filePath("data/sidecar/" + name));
                    if (expected.contains(entry) || !item.isFile() || item.isSymLink() || item.isJunction()) { fail(); return; }
                    expected.insert(entry);
                }
                if (*entries != expected || !expected.contains("yt-chatterino-obs/data/sidecar/YT-Chatterino.exe")) { fail(); return; }
                core.close(); boot.close();
                const QString directory = str(info["version"]) + '-' + QUuid::createUuid().toString(QUuid::WithoutBraces);
                if (!QDir().rename(folder.absolutePath(), root.filePath(directory)) ||
                    !writeObject(root.filePath("current.json"), {{"version", info["version"]}, {"directory", directory}, {"core_sha256", coreHash}})) {
                    fail(); return;
                }
                safe->busy = false; safe->timer.start(6 * 60 * 60 * 1000);
                if (safe->notify) safe->notify("OBS update ready. Restart OBS to apply it.");
            });
        });
    });
}
} // namespace yc
