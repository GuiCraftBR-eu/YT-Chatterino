#include "network.hpp"

namespace yc {
Network::Network(QObject *p) : QObject(p), manager(this), chatManager(this) {
    static bool backendSelected = false;
    if (!backendSelected && QSslSocket::availableBackends().contains("schannel")) {
        QSslSocket::setActiveBackend("schannel");
    }
    backendSelected = true;
    manager.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    chatManager.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

QUrl Network::query(QString base, const QMap<QString, QString> &params) {
    QUrl u(base);
    QUrlQuery q(u);
    for (auto i = params.begin(); i != params.end(); ++i) {
        q.addQueryItem(i.key(), i.value());
    }
    u.setQuery(q);
    return u;
}

void Network::bytes(QUrl url, BytesCallback done, QByteArray method, QByteArray body,
                    QMap<QByteArray, QByteArray> headers, qint64 maxBytes, int timeoutMs, Lane lane) {
    if (transport) {
        transport(url, method, body, headers, std::move(done));
        return;
    }
    if (url.scheme() != "https" && !(url.scheme() == "http" && url.host() == "127.0.0.1")) {
        done({}, "Unsupported connection URL.");
        return;
    }
    QNetworkRequest request(url);
    request.setTransferTimeout(timeoutMs);
    // Avoid sharing a stalled HTTP/2 connection between 7TV animations and still previews.
    if (url.host().compare("cdn.7tv.app", Qt::CaseInsensitive) == 0)
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setRawHeader("User-Agent", "YT-Chatterino/" APP_VERSION);
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    for (auto i = headers.begin(); i != headers.end(); ++i) {
        request.setRawHeader(i.key(), i.value());
    }
    // Chat submissions must not wait behind stalled background requests to the same host.
    auto &selectedManager = lane == Lane::ChatSend ? chatManager : manager;
    auto *reply = selectedManager.sendCustomRequest(request, method, body);
    reply->setReadBufferSize(maxBytes + 1);
    auto data = QSharedPointer<QByteArray>::create();
    connect(reply, &QNetworkReply::readyRead, reply, [reply, data, maxBytes] {
        data->append(reply->readAll());
        if (data->size() > maxBytes) {
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [reply, data, maxBytes, done = std::move(done)] {
        data->append(reply->readAll());
        QString error;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (data->size() > maxBytes) {
            error = "Response exceeded the size limit.";
        } else if (reply->error() != QNetworkReply::NoError) {
            auto o = QJsonDocument::fromJson(*data).object();
            QString detail = str(o["message"]);
            if (detail.isEmpty()) {
                detail = str(o["error_description"]);
            }
            if (detail.isEmpty() && o["error"].isString()) {
                detail = str(o["error"]);
            }
            if (detail.isEmpty()) {
                detail = reply->errorString();
            }
            error = QString("HTTP %1: %2").arg(status).arg(detail.left(600));
        }
        reply->deleteLater();
        done(*data, error);
    });
}

void Network::json(QUrl url, JsonCallback done, QByteArray method, QJsonObject body,
                   QMap<QByteArray, QByteArray> headers, qint64 maxBytes, int timeoutMs, Lane lane) {
    headers["Accept"] = "application/json";
    QByteArray payload;
    if (!body.isEmpty()) {
        headers["Content-Type"] = "application/json";
        payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    }
    bytes(
        url,
        [done](QByteArray raw, QString error) {
            QJsonParseError pe;
            auto doc = QJsonDocument::fromJson(raw, &pe);
            if (error.isEmpty() && pe.error != QJsonParseError::NoError && !raw.trimmed().isEmpty()) {
                error = "The server returned invalid JSON.";
            }
            done(doc.isArray() ? QJsonObject{{"items", doc.array()}} : doc.object(), error);
        },
        method, payload, headers, maxBytes, timeoutMs, lane);
}

void Network::sevenTv(const QString &path, bool global, JsonCallback done) {
    auto valid = [global](const QJsonObject &data) {
        if (global) return data.value("emotes").isArray();
        const auto set = data.value("emote_set");
        return set.isNull() || (set.isObject() && set.toObject().value("emotes").isArray());
    };
    auto complete = [done, valid](QJsonObject data, QString error) {
        if (error.isEmpty() && !valid(data)) error = "The 7TV server returned an invalid emote set.";
        done(data, error);
    };
    auto request = [this, path](const QString &host, JsonCallback callback) {
        json(QUrl("https://" + host + "/v3/" + path), std::move(callback), "GET", {}, {},
             32 * 1024 * 1024, 35000);
    };
    request("7tv.io", [request, valid, complete](QJsonObject data, QString error) {
        if ((error.isEmpty() && valid(data)) || error.startsWith("HTTP 404") || error.startsWith("HTTP 429") ||
            (error.startsWith("HTTP 400") && error.contains("invalid platform", Qt::CaseInsensitive))) {
            complete(data, error);
            return;
        }
        request("api.7tv.app", complete);
    });
}

void Network::form(QUrl url, const QMap<QString, QString> &values, JsonCallback done) {
    QUrlQuery q;
    for (auto i = values.begin(); i != values.end(); ++i) {
        q.addQueryItem(i.key(), i.value());
    }
    bytes(url, [done](QByteArray raw, QString e) { done(QJsonDocument::fromJson(raw).object(), e); }, "POST",
          q.query(QUrl::FullyEncoded).toUtf8(), {{"Content-Type", "application/x-www-form-urlencoded"}});
}

Images::Images(Network *n, QObject *p) : QObject(p), net(n) {
    QImageReader::setAllocationLimit(64);
    visibilityTimer.setInterval(500);
    connect(&visibilityTimer, &QTimer::timeout, this, [this] {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (const auto &entry : cache) {
            if (entry->movie && entry->movie->state() == QMovie::Running && now - entry->visibleAt > 700) {
                entry->movie->setPaused(true);
            }
        }
        retryExpired();
    });
    visibilityTimer.start();
    preloadTimer.setSingleShot(true);
    connect(&preloadTimer, &QTimer::timeout, this, &Images::pumpPreloads);
    diskRetryTimer.setInterval(15000);
    connect(&diskRetryTimer, &QTimer::timeout, this, [this] { tryOpenDiskCache(); });
    diskIndexTimer.setSingleShot(true);
    connect(&diskIndexTimer, &QTimer::timeout, this, &Images::saveDiskIndex);
    diskCleanupTimer.setInterval(60 * 60 * 1000);
    connect(&diskCleanupTimer, &QTimer::timeout, this, &Images::startDiskCleanup);
    diskSweepTimer.setSingleShot(true);
    connect(&diskSweepTimer, &QTimer::timeout, this, &Images::sweepDiskCache);
}

Images::~Images() {
    if (diskIndexDirty) saveDiskIndex();
}

void Images::configure(int downloads, int cacheMb, int diskMb) {
    maxDownloads = qBound(1, downloads, 16);
    cacheLimit = qint64(qBound(16, cacheMb, 1024)) * 1024 * 1024;
    diskCache.setMaximumCacheSize(qint64(qBound(64, diskMb, 16384)) * 1024 * 1024);
    trim();
    pumpDownloads();
    if (!preloadQueue.isEmpty() && !preloadTimer.isActive()) preloadTimer.start(0);
}

void Images::setDiskCacheDirectory(const QString &path) {
    if (path.isEmpty() || (!diskDirectory.isEmpty() && diskDirectory != path)) return;
    ++diskGeneration;
    diskDirectory = path;
    QDir().mkpath(path);
    diskLock = std::make_unique<QLockFile>(QDir(path).filePath("cache.lock"));
    if (!tryOpenDiskCache()) diskRetryTimer.start();
}

bool Images::tryOpenDiskCache() {
    if (!diskLock) return false;
    if (!diskLock->isLocked() && !diskLock->tryLock(0)) return false;
    diskCache.setCacheDirectory(diskDirectory);
    diskRetryTimer.stop();
    loadDiskIndex();
    QTimer::singleShot(0, this, [this] { (void)diskCache.cacheSize(); });
    QTimer::singleShot(1000, this, &Images::startDiskCleanup);
    diskCleanupTimer.start();
    emit diskCacheAvailable();
    return true;
}

bool Images::clearDiskCache() {
    ++diskGeneration;
    if (diskCache.cacheDirectory().isEmpty()) return false;
    diskSweepTimer.stop(); diskSweep.reset();
    diskCache.clear();
    diskLastShown.clear(); diskVisibleThrottle.clear(); diskRetired.clear();
    diskIndexDirty = true;
    saveDiskIndex();
    return diskCache.cacheSize() == 0;
}

QUrl Images::diskKey(const QUrl &url) {
    if (!allowed(url)) return {};
    const auto hash = QCryptographicHash::hash(url.toEncoded(QUrl::FullyEncoded), QCryptographicHash::Sha256).toHex();
    return QUrl(QStringLiteral("https://image-cache.invalid/") + QString::fromLatin1(hash));
}

static QString diskId(const QUrl &key) {
    if (key.scheme() != "https" || key.host() != "image-cache.invalid") return {};
    const auto id = key.path().mid(1);
    static const QRegularExpression valid("^[a-f0-9]{64}$");
    return valid.match(id).hasMatch() ? id : QString();
}

qint64 Images::diskNow() const {
    return diskClock ? diskClock() : QDateTime::currentSecsSinceEpoch();
}

bool Images::diskExpired(const QNetworkCacheMetaData &meta) const {
    const auto id = diskId(meta.url());
    const auto shown = diskLastShown.value(id, 0);
    // Visibility updates are batched for one minute; the grace prevents early eviction.
    if (shown > 0) return shown + 7 * 24 * 60 * 60 + 60 <= diskNow();
    return !meta.expirationDate().isValid() || meta.expirationDate().toSecsSinceEpoch() <= diskNow();
}

void Images::loadDiskIndex() {
    diskLastShown.clear(); diskRetired.clear(); diskIndexDirty = false;
    QFile file(QDir(diskDirectory).filePath("usage-v1.json"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024) return;
    const auto data = QJsonDocument::fromJson(file.readAll()).object();
    const auto shown = data["shown"].toObject();
    for (auto it = shown.begin(); it != shown.end(); ++it) {
        const QUrl key("https://image-cache.invalid/" + it.key());
        const qint64 time = it.value().toInteger();
        if (!diskId(key).isEmpty() && time > 0) diskLastShown.insert(it.key(), qMin(time, diskNow()));
    }
    for (const auto &value : data["retired"].toArray()) {
        const auto id = value.toString();
        if (!diskId(QUrl("https://image-cache.invalid/" + id)).isEmpty()) diskRetired.insert(id);
    }
}

void Images::saveDiskIndex() {
    if (!diskIndexDirty || diskCache.cacheDirectory().isEmpty() || !diskLock || !diskLock->isLocked()) return;
    QJsonObject shown;
    for (auto it = diskLastShown.cbegin(); it != diskLastShown.cend(); ++it) shown[it.key()] = double(it.value());
    QJsonArray retired;
    for (const auto &id : diskRetired) retired.append(id);
    if (writeObject(QDir(diskDirectory).filePath("usage-v1.json"), {{"shown", shown}, {"retired", retired}})) {
        diskIndexDirty = false;
    } else if (!diskIndexTimer.isActive()) diskIndexTimer.start(30000);
}

void Images::scheduleDiskIndexSave() {
    diskIndexDirty = true;
    if (!diskIndexTimer.isActive()) diskIndexTimer.start(30000);
}

void Images::markChatVisible(const QStringList &urls) {
    if (diskCache.cacheDirectory().isEmpty() || urls.isEmpty()) return;
    const auto now = diskNow();
    const auto previous = diskVisibleThrottle.value(urls.first(), 0);
    if (previous > 0 && now >= previous && now - previous < 60) return;
    diskVisibleThrottle[urls.first()] = now;
    bool changed = false;
    for (const auto &url : urls) {
        const auto id = diskId(diskKey(QUrl(url)));
        if (id.isEmpty()) continue;
        const bool wasRetired = diskRetired.remove(id);
        if (wasRetired || !diskLastShown.contains(id) || now - diskLastShown.value(id) >= 60 || now < diskLastShown.value(id)) {
            diskLastShown[id] = now;
            changed = true;
        }
    }
    if (changed) scheduleDiskIndexSave();
    const auto entry = cache.value(urls.first());
    if (entry && !entry->diskSource.isEmpty() && !entry->bytes.isEmpty())
        saveDiskImage(QUrl(entry->diskSource), entry->bytes, diskGeneration);
}

void Images::retireDiskKey(const QUrl &key) {
    const auto id = diskId(key);
    if (id.isEmpty()) return;
    if (!diskCache.remove(key)) return;
    diskLastShown.remove(id);
    diskRetired.insert(id);
    scheduleDiskIndexSave();
}

void Images::startDiskCleanup() {
    if (diskCache.cacheDirectory().isEmpty() || diskSweep) return;
    diskSweep = std::make_unique<QDirIterator>(diskDirectory, QStringList{"*.d"}, QDir::Files, QDirIterator::Subdirectories);
    diskSweepTimer.start(0);
}

void Images::sweepDiskCache() {
    if (!diskSweep) return;
    QElapsedTimer budget; budget.start();
    int processed = 0;
    while (diskSweep->hasNext() && processed < 32 && budget.elapsed() < 8) {
        ++processed;
        const auto path = diskSweep->next();
        const auto meta = diskCache.fileMetaData(path);
        if (meta.isValid() && !diskId(meta.url()).isEmpty() && diskExpired(meta)) retireDiskKey(meta.url());
    }
    if (diskSweep->hasNext()) diskSweepTimer.start(0);
    else {
        diskSweep.reset();
        bool changed = false;
        for (auto it = diskLastShown.begin(); it != diskLastShown.end();) {
            if (it.value() + 7 * 24 * 60 * 60 + 60 <= diskNow()) {
                diskRetired.insert(it.key());
                it = diskLastShown.erase(it);
                changed = true;
            } else ++it;
        }
        if (changed) scheduleDiskIndexSave();
    }
}

QByteArray Images::diskImage(const QUrl &url, qint64 maxBytes) {
    if (diskCache.cacheDirectory().isEmpty()) return {};
    const auto key = diskKey(url);
    if (key.isEmpty()) return {};
    const auto meta = diskCache.metaData(key);
    if (!meta.isValid() || meta.url() != key) return {};
    if (diskExpired(meta)) {
        retireDiskKey(key);
        return {};
    }
    std::unique_ptr<QIODevice> file(diskCache.data(key));
    if (!file) { diskCache.remove(key); return {}; }
    const auto content = file->read(maxBytes + 33);
    file.reset();
    if (content.size() <= 32 || content.size() > maxBytes + 32 ||
        content.first(32) != QCryptographicHash::hash(content.sliced(32), QCryptographicHash::Sha256)) {
        diskCache.remove(key);
        return {};
    }
    return content.sliced(32);
}

void Images::saveDiskImage(const QUrl &url, const QByteArray &bytes, quint64 generation) {
    if (generation != diskGeneration || bytes.isEmpty() || diskCache.cacheDirectory().isEmpty()) return;
    const auto key = diskKey(url);
    if (key.isEmpty()) return;
    const auto existing = diskCache.metaData(key);
    if (existing.isValid() && existing.url() == key && !diskExpired(existing)) return;
    QNetworkCacheMetaData meta;
    meta.setUrl(key);
    meta.setSaveToDisk(true);
    meta.setExpirationDate(QDateTime::fromSecsSinceEpoch(diskNow() + 7 * 24 * 60 * 60, QTimeZone::UTC));
    meta.setRawHeaders({{"Content-Type", "application/octet-stream"},
                        {"Content-Length", QByteArray::number(bytes.size() + 32)}});
    auto *file = diskCache.prepare(meta);
    if (!file) return;
    const auto checksum = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    if (file->write(checksum) != checksum.size() || file->write(bytes) != bytes.size()) {
        diskCache.insert(file);
        diskCache.remove(key);
        return;
    }
    diskCache.insert(file);
    const auto limit = diskCache.maximumCacheSize();
    if (diskCache.cacheSize() > limit && limit > 1) {
        // Qt prunes before an insertion; also prune after it when that insertion exceeds the limit.
        diskCache.setMaximumCacheSize(limit - 1);
        diskCache.setMaximumCacheSize(limit);
    }
}

void Images::imageBytes(QUrl url, BytesCallback done, bool foreground, qint64 maxBytes, int timeoutMs,
                        std::function<bool()> active) {
    if (active && !active()) { done({}, "Canceled."); return; }
    const auto cached = diskImage(url, maxBytes);
    if (!cached.isEmpty()) {
        QTimer::singleShot(0, this, [done = std::move(done), active = std::move(active), cached] {
            if (active && !active()) done({}, "Canceled.");
            else done(cached, {});
        });
        return;
    }
    if (!foreground && diskRetired.contains(diskId(diskKey(url)))) {
        done({}, "Cache entry unused.");
        return;
    }
    Download job{std::move(url), std::move(done), maxBytes, timeoutMs, std::move(active)};
    (foreground ? foregroundDownloads : backgroundDownloads).enqueue(std::move(job));
    pumpDownloads();
}

void Images::pumpDownloads() {
    if (pumpingDownloads) return;
    pumpingDownloads = true;
    while (activeDownloads < maxDownloads && (!foregroundDownloads.isEmpty() || !backgroundDownloads.isEmpty())) {
        auto job = !foregroundDownloads.isEmpty() ? foregroundDownloads.dequeue() : backgroundDownloads.dequeue();
        if (job.active && !job.active()) { job.done({}, "Canceled."); continue; }
        ++activeDownloads;
        net->bytes(job.url, [this, done = std::move(job.done)](QByteArray bytes, QString error) {
            --activeDownloads;
            done(std::move(bytes), std::move(error));
            pumpDownloads();
        }, "GET", {}, {}, job.maxBytes, job.timeoutMs);
    }
    pumpingDownloads = false;
}

void Images::retryExpired() {
    auto now = QDateTime::currentSecsSinceEpoch();
    QStringList expired;
    for (auto i = failures.begin(); i != failures.end();) {
        if (i.value() <= now) {
            expired << i.key();
            i = failures.erase(i);
        } else {
            ++i;
        }
    }
    for (auto key : expired) {
        emit ready(key);
    }
}

bool Images::allowed(const QUrl &u) {
    if (u.scheme() != "https" || !u.userInfo().isEmpty() || (u.port(-1) != -1 && u.port() != 443)) {
        return false;
    }
    const auto h = u.host().toLower();
    if (h == "d3aqoihi2n8ty8.cloudfront.net") return true; // Twitch's official Cheermote CDN.
    for (const auto &suffix :
         QStringList{"7tv.app", "7tv.io", "betterttv.net", "jtvnw.net", "giphy.com", "googleusercontent.com",
                     "ggpht.com", "gstatic.com", "youtube.com", "ytimg.com", "files.kick.com", "ext.cdn.kick.com", "stream.kick.com", "images.kick.com"}) {
        if (h == suffix || h.endsWith('.' + suffix)) {
            return true;
        }
    }
    return false;
}

QImage Images::get(const QStringList &urls, bool animate) {
    if (urls.isEmpty()) {
        return {};
    }
    auto e = cache.value(urls[0]);
    if (e) {
        if (e->preview && animate && !pending.contains(urls[0])) request(urls);
        e = cache.value(urls[0]);
        if (!e) return {};
        e->use = ++clock;
        if (animate) {
            e->visibleAt = QDateTime::currentMSecsSinceEpoch();
        }
        if (e->movie && animate && e->movie->state() != QMovie::Running) {
            if (e->movie->state() == QMovie::Paused) {
                e->movie->setPaused(false);
            } else {
                e->movie->start();
            }
        }
        return e->movie ? e->movie->currentImage() : e->image;
    }
    request(urls);
    return {};
}

void Images::request(const QStringList &urls, int alternative) {
    if (urls.isEmpty() || alternative >= urls.size()) {
        if (!urls.isEmpty()) {
            pending.remove(urls[0]);
            failures[urls[0]] = QDateTime::currentSecsSinceEpoch() + 20;
        }
        return;
    }
    const auto key = urls[0];
    if (alternative == 0) {
        if ((cache.contains(key) && !cache.value(key)->preview) || pending.contains(key) ||
            failures.value(key) > QDateTime::currentSecsSinceEpoch()) {
            return;
        }
        pending.insert(key);
        // 7TV animated images can be large or stall; show its small still frame
        // while the animated request continues, then replace it if that succeeds.
        for (const auto &candidate : urls) {
            const QUrl preview(candidate);
            if (preview.host().toLower() != "cdn.7tv.app" ||
                !preview.path().endsWith("_static.webp") || !allowed(preview)) continue;
            requestStillPreview(key, preview);
            break;
        }
    }
    const auto u = QUrl(urls[alternative]);
    if (!allowed(u)) {
        request(urls, alternative + 1);
        return;
    }
    const quint64 generation = diskGeneration;
    imageBytes(u, [this, urls, alternative, key, u, generation](QByteArray bytes, QString error) {
        if (error.isEmpty()) {
            QBuffer probe(&bytes);
            probe.open(QIODevice::ReadOnly);
            QImageReader reader(&probe);
            QSize size = reader.size();
            if (size.width() <= 0 || size.height() <= 0 || size.width() > 4096 || size.height() > 4096) {
                error = "Invalid image dimensions.";
            } else {
                auto e = QSharedPointer<Entry>::create();
                e->bytes = bytes;
                e->diskSource = u.toString();
                e->buffer = QSharedPointer<QBuffer>::create();
                e->buffer->setData(bytes);
                e->buffer->open(QIODevice::ReadOnly);
                if (reader.supportsAnimation()) {
                    e->movie = QSharedPointer<QMovie>::create(e->buffer.data());
                    QSize scaled = size;
                    if (scaled.width() > 384 || scaled.height() > 180) {
                        scaled.scale(384, 180, Qt::KeepAspectRatio);
                    }
                    e->movie->setScaledSize(scaled);
                    e->movie->setCacheMode(QMovie::CacheNone);
                    if (!e->movie->jumpToFrame(0) || e->movie->frameCount() > 1500) {
                        e->movie.reset();
                        error = "Unsupported animation.";
                    } else {
                        connect(e->movie.data(), &QMovie::frameChanged, this,
                                [this, key](int) { emit frame(key); });
                    }
                } else {
                    if (size.width() > 512 || size.height() > 512) {
                        QSize scaled = size; scaled.scale(256, 256, Qt::KeepAspectRatio);
                        reader.setScaledSize(scaled);
                    }
                    e->image = reader.read();
                }
                if (error.isEmpty() && (!e->image.isNull() || e->movie)) {
                    saveDiskImage(u, bytes, generation);
                    e->use = ++clock;
                    QSize decoded = e->movie ? e->movie->frameRect().size() : e->image.size();
                    e->cost = bytes.size() + decoded.width() * decoded.height() * 4;
                    if (auto old = cache.value(key)) total -= old->cost;
                    total += e->cost;
                    cache[key] = e;
                    pending.remove(key);
                    trim();
                    emit ready(key);
                    return;
                }
            }
        }
        if (alternative + 1 < urls.size()) {
            request(urls, alternative + 1);
        } else {
            pending.remove(key);
            failures[key] = QDateTime::currentSecsSinceEpoch() + (error.contains("429") ? 120 : 20);
            qWarning().noquote() << "Image load failed:" << QUrl(key).host() << error;
            emit ready(key);
        }
    });
}

void Images::requestStillPreview(const QString &key, const QUrl &url, int attempt) {
    if (cache.contains(key)) return;
    const quint64 generation = diskGeneration;
    imageBytes(url, [this, key, url, attempt, generation](QByteArray bytes, QString error) {
        if (cache.contains(key)) return;
        if (!error.isEmpty()) {
            if (attempt < 2 && !error.startsWith("HTTP 429"))
                QTimer::singleShot(1500, this, [this, key, url, attempt] { requestStillPreview(key, url, attempt + 1); });
            return;
        }
        QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        const QSize size = reader.size();
        if (size.width() <= 0 || size.height() <= 0 || size.width() > 4096 || size.height() > 4096) return;
        if (size.width() > 512 || size.height() > 512) {
            QSize scaled = size; scaled.scale(256, 256, Qt::KeepAspectRatio); reader.setScaledSize(scaled);
        }
        const auto image = reader.read();
        if (image.isNull() || cache.contains(key)) return;
        saveDiskImage(url, bytes, generation);
        auto entry = QSharedPointer<Entry>::create();
        entry->image = image; entry->bytes = bytes; entry->use = ++clock; entry->preview = true;
        entry->diskSource = url.toString();
        entry->cost = bytes.size() + image.width() * image.height() * 4;
        total += entry->cost; cache[key] = entry; failures.remove(key); trim(); emit ready(key);
    }, true, 2 * 1024 * 1024, 10000);
}

void Images::preload(const QStringList &urls, std::function<bool()> active, bool priority, int attempt) {
    if (urls.isEmpty() || !allowed(QUrl(urls.first()))) return;
    const auto key = urls.first();
    if (cache.contains(key) || pending.contains(key) || preloadKeys.contains(key)) return;
    preloadKeys.insert(key);
    PreloadItem item{urls, std::move(active), priority, attempt};
    if (priority) preloadQueue.prepend(item); else preloadQueue.enqueue(item);
    if (!preloadTimer.isActive()) preloadTimer.start(0);
}

void Images::pumpPreloads() {
    QElapsedTimer budget; budget.start();
    int processed = 0;
    while (preloadActive < maxDownloads && !preloadQueue.isEmpty() && processed < 8 && budget.elapsed() < 8) {
        ++processed;
        const auto item = preloadQueue.dequeue();
        const auto key = item.urls.first();
        if ((item.active && !item.active()) || cache.contains(key) || pending.contains(key)) {
            preloadKeys.remove(key); continue;
        }
        QString selected = key;
        bool still = false;
        for (const auto &candidate : item.urls) {
            const QUrl url(candidate);
            if (url.host().compare("cdn.7tv.app", Qt::CaseInsensitive) == 0 &&
                url.path().endsWith("_static.webp") && allowed(url)) {
                selected = candidate; still = true; break;
            }
        }
        if (!still && QUrl(key).host().compare("cdn.betterttv.net", Qt::CaseInsensitive) == 0 && item.urls.size() > 1 && allowed(QUrl(item.urls[1])))
            selected = item.urls[1];
        ++preloadActive;
        const quint64 generation = diskGeneration;
        imageBytes(QUrl(selected), [this, key, still, item, selected, generation](QByteArray bytes, QString error) {
            --preloadActive; preloadKeys.remove(key);
            if (!error.isEmpty() && !error.startsWith("HTTP 429") && error != "Cache entry unused." && item.attempt < 1 && (!item.active || item.active()))
                QTimer::singleShot(2000, this, [this, item] { preload(item.urls, item.active, item.priority, item.attempt + 1); });
            if (error.isEmpty() && (!item.active || item.active()) && !cache.contains(key) && !pending.contains(key)) {
                QBuffer probe(&bytes); probe.open(QIODevice::ReadOnly);
                QImageReader reader(&probe); const QSize size = reader.size();
                if (size.width() > 0 && size.height() > 0 && size.width() <= 4096 && size.height() <= 4096) {
                    auto entry = QSharedPointer<Entry>::create(); entry->bytes = bytes; entry->preview = still;
                    entry->diskSource = selected;
                    if (!still && reader.supportsAnimation()) {
                        entry->buffer = QSharedPointer<QBuffer>::create(); entry->buffer->setData(bytes);
                        entry->buffer->open(QIODevice::ReadOnly); entry->movie = QSharedPointer<QMovie>::create(entry->buffer.data());
                        QSize scaled = size; if (scaled.width() > 384 || scaled.height() > 180) scaled.scale(384, 180, Qt::KeepAspectRatio);
                        entry->movie->setScaledSize(scaled); entry->movie->setCacheMode(QMovie::CacheNone);
                        if (!entry->movie->jumpToFrame(0) || entry->movie->frameCount() > 1500) entry->movie.reset();
                        else connect(entry->movie.data(), &QMovie::frameChanged, this, [this, key](int) { emit frame(key); });
                    } else {
                        if (size.width() > 512 || size.height() > 512) { QSize scaled = size; scaled.scale(256, 256, Qt::KeepAspectRatio); reader.setScaledSize(scaled); }
                        entry->image = reader.read();
                    }
                    if (entry->movie || !entry->image.isNull()) {
                        saveDiskImage(QUrl(selected), bytes, generation);
                        entry->use = ++clock;
                        const QSize decoded = entry->movie ? entry->movie->frameRect().size() : entry->image.size();
                        entry->cost = bytes.size() + decoded.width() * decoded.height() * 4;
                        total += entry->cost; cache[key] = entry; failures.remove(key); trim(); emit ready(key);
                    }
                }
            }
            if (!preloadTimer.isActive()) preloadTimer.start(0);
        }, false, 8 * 1024 * 1024, 15000, item.active);
    }
    if (!preloadQueue.isEmpty() && preloadActive < maxDownloads && !preloadTimer.isActive()) preloadTimer.start(0);
}

void Images::trim() {
    while (total > cacheLimit && cache.size() > 1) {
        auto oldest = cache.begin();
        for (auto i = cache.begin(); i != cache.end(); ++i) {
            if (i.value()->use < oldest.value()->use) {
                oldest = i;
            }
        }
        auto e = oldest.value();
        total -= e->cost;
        cache.erase(oldest);
    }
    if (failures.size() > 2000) {
        failures.clear();
    }
}
} // namespace yc
