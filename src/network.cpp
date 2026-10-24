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
                    QMap<QByteArray, QByteArray> headers, qint64 maxBytes) {
    if (transport) {
        transport(url, method, body, headers, std::move(done));
        return;
    }
    if (url.scheme() != "https" && !(url.scheme() == "http" && url.host() == "127.0.0.1")) {
        done({}, "Unsupported connection URL.");
        return;
    }
    QNetworkRequest request(url);
    request.setTransferTimeout(20000);
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
                   QMap<QByteArray, QByteArray> headers) {
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
        method, payload, headers);
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
    });
    visibilityTimer.start();
}

bool Images::allowed(const QUrl &u) {
    if (u.scheme() != "https" || !u.userInfo().isEmpty() || (u.port(-1) != -1 && u.port() != 443)) {
        return false;
    }
    const auto h = u.host().toLower();
    for (const auto &suffix :
         QStringList{"7tv.app", "7tv.io", "betterttv.net", "jtvnw.net", "giphy.com", "googleusercontent.com",
                     "ggpht.com", "gstatic.com", "youtube.com"}) {
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
            failures[urls[0]] = QDateTime::currentSecsSinceEpoch() + 90;
        }
        return;
    }
    const auto key = urls[0];
    if (alternative == 0) {
        if (cache.contains(key) || pending.contains(key) ||
            failures.value(key) > QDateTime::currentSecsSinceEpoch()) {
            return;
        }
        pending.insert(key);
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
            if (size.width() <= 0 || size.height() <= 0 || size.width() > 2048 || size.height() > 2048) {
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
                    e->image = reader.read();
                }
                if (error.isEmpty() && (!e->image.isNull() || e->movie)) {
                    e->use = ++clock;
                    QSize decoded = e->movie ? e->movie->frameRect().size() : e->image.size();
                    e->cost = bytes.size() + decoded.width() * decoded.height() * 4;
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
            failures[key] = QDateTime::currentSecsSinceEpoch() + 90;
            emit ready(key);
        }
    });
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
