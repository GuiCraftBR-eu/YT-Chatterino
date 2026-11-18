#include "network.hpp"

namespace yc {
Network::Network(QObject *p) : QObject(p), manager(this) {
    static bool backendSelected = false;
    if (!backendSelected && QSslSocket::availableBackends().contains("schannel")) {
        QSslSocket::setActiveBackend("schannel");
    }
    backendSelected = true;
    manager.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
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
                    QMap<QByteArray, QByteArray> headers, qint64 maxBytes, int timeoutMs) {
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
    auto *reply = manager.sendCustomRequest(request, method, body);
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
                   QMap<QByteArray, QByteArray> headers, qint64 maxBytes, int timeoutMs) {
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
        method, payload, headers, maxBytes, timeoutMs);
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
    net->bytes(u, [this, urls, alternative, key](QByteArray bytes, QString error) {
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
    net->bytes(url, [this, key, url, attempt](QByteArray bytes, QString error) {
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
        auto entry = QSharedPointer<Entry>::create();
        entry->image = image; entry->bytes = bytes; entry->use = ++clock; entry->preview = true;
        entry->cost = bytes.size() + image.width() * image.height() * 4;
        total += entry->cost; cache[key] = entry; failures.remove(key); trim(); emit ready(key);
    }, "GET", {}, {}, 2 * 1024 * 1024, 10000);
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
    while (preloadActive < 3 && !preloadQueue.isEmpty()) {
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
        net->bytes(QUrl(selected), [this, key, still, item](QByteArray bytes, QString error) {
            --preloadActive; preloadKeys.remove(key);
            if (!error.isEmpty() && !error.startsWith("HTTP 429") && item.attempt < 1 && (!item.active || item.active()))
                QTimer::singleShot(2000, this, [this, item] { preload(item.urls, item.active, item.priority, item.attempt + 1); });
            if (error.isEmpty() && (!item.active || item.active()) && !cache.contains(key) && !pending.contains(key)) {
                QBuffer probe(&bytes); probe.open(QIODevice::ReadOnly);
                QImageReader reader(&probe); const QSize size = reader.size();
                if (size.width() > 0 && size.height() > 0 && size.width() <= 4096 && size.height() <= 4096) {
                    auto entry = QSharedPointer<Entry>::create(); entry->bytes = bytes; entry->preview = still;
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
                        entry->use = ++clock;
                        const QSize decoded = entry->movie ? entry->movie->frameRect().size() : entry->image.size();
                        entry->cost = bytes.size() + decoded.width() * decoded.height() * 4;
                        total += entry->cost; cache[key] = entry; failures.remove(key); trim(); emit ready(key);
                    }
                }
            }
            if (!preloadTimer.isActive()) preloadTimer.start(25);
        }, "GET", {}, {}, 8 * 1024 * 1024, 15000);
    }
}

void Images::trim() {
    while (total > 48 * 1024 * 1024 && cache.size() > 1) {
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
