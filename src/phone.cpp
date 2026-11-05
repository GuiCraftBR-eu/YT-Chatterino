#include "phone.hpp"
#include <algorithm>

namespace yc {
static QByteArray randomHex(int size) {
    QByteArray bytes(size, Qt::Uninitialized);
    auto *rng = QRandomGenerator::system();
    for (int i = 0; i < size; ++i) bytes[i] = char(rng->bounded(256));
    return bytes.toHex();
}

static QString safeImage(const QStringList &urls) {
    for (const auto &url : urls)
        if (Images::allowed(QUrl(url))) return url;
    return {};
}

PhoneServer::PhoneServer(Hub *h, QObject *parent) : QObject(parent), hub(h), listener(this) {
    connect(&listener, &QTcpServer::newConnection, this, &PhoneServer::acceptConnections);
}

bool PhoneServer::start(Pane *p, QHostAddress ip, quint16 port) {
    stop();
    if (!p || ip.protocol() != QAbstractSocket::IPv4Protocol || ip.isNull() || ip == QHostAddress::AnyIPv4)
        return false;
    if (!listener.listen(ip, port)) return false;
    pane = p; bindAddress = ip;
    code = QString("%1").arg(QRandomGenerator::system()->bounded(1000000), 6, 10, QChar('0'));
    codeAge.start();
    paneDestroyed = connect(p, &QObject::destroyed, this, &PhoneServer::stop);
    return true;
}

void PhoneServer::stop() {
    const bool wasActive = listener.isListening();
    disconnect(paneDestroyed);
    paneDestroyed = {};
    listener.close();
    for (auto *socket : buffers.keys()) { socket->abort(); socket->deleteLater(); }
    buffers.clear(); sessions.clear(); attempts.clear(); pane.clear(); code.clear();
    if (wasActive) emit stopped();
}
bool PhoneServer::isActive() const { return listener.isListening() && pane; }
QString PhoneServer::address() const {
    return isActive() ? QString("http://%1:%2").arg(bindAddress.toString()).arg(listener.serverPort()) : QString();
}
QString PhoneServer::pairingCode() const { return code; }

void PhoneServer::acceptConnections() {
    while (listener.hasPendingConnections()) {
        auto *socket = listener.nextPendingConnection();
        if (!socket) continue;
        if (buffers.size() >= 24) { socket->abort(); socket->deleteLater(); continue; }
        buffers.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { receive(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            buffers.remove(socket); socket->deleteLater();
        });
        QTimer::singleShot(45000, socket, [socket] { socket->abort(); });
    }
}

void PhoneServer::receive(QTcpSocket *socket) {
    if (!buffers.contains(socket)) return;
    auto &bytes = buffers[socket];
    bytes += socket->readAll();
    if (bytes.size() > 32768) { respond(socket, 413, "text/plain", "Request too large"); return; }
    const int split = bytes.indexOf("\r\n\r\n");
    if (split < 0) {
        if (bytes.size() > 16384) respond(socket, 413, "text/plain", "Headers too large");
        return;
    }
    const auto lines = bytes.left(split).split('\n');
    const auto request = lines.value(0).trimmed().split(' ');
    if (request.size() != 3 || !request[1].startsWith('/') || request[1].startsWith("//") ||
        request[2] != "HTTP/1.1") {
        respond(socket, 400, "text/plain", "Bad request"); return;
    }
    QMap<QByteArray, QByteArray> headers;
    for (int i = 1; i < lines.size(); ++i) {
        const auto line = lines[i].trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0) { respond(socket, 400, "text/plain", "Bad headers"); return; }
        const auto name = line.left(colon).trimmed().toLower();
        if (headers.contains(name)) { respond(socket, 400, "text/plain", "Duplicate header"); return; }
        headers[name] = line.mid(colon + 1).trimmed();
    }
    if (headers.contains("transfer-encoding")) { respond(socket, 400, "text/plain", "Unsupported request"); return; }
    bool ok = true;
    const int length = headers.contains("content-length") ? headers["content-length"].toInt(&ok) : 0;
    if (!ok || length < 0 || length > 8192) { respond(socket, 413, "text/plain", "Invalid body"); return; }
    if (bytes.size() < split + 4 + length) return;
    const auto body = bytes.mid(split + 4, length);
    bytes.clear();
    disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
    handle(socket, request[0], QUrl::fromEncoded(request[1]), headers, body);
}

void PhoneServer::respond(QTcpSocket *socket, int status, QByteArray type, QByteArray body, QByteArray extra) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
    const QByteArray reason = status == 200 ? "OK" : status == 401 ? "Unauthorized" :
                              status == 403 ? "Forbidden" : status == 404 ? "Not Found" :
                              status == 429 ? "Too Many Requests" : status == 413 ? "Payload Too Large" : "Bad Request";
    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n";
    out += "Content-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) +
           "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
           "X-Frame-Options: DENY\r\nReferrer-Policy: no-referrer\r\n"
           "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; "
           "img-src 'self' https: data:; connect-src 'self'; frame-ancestors 'none'\r\n";
    out += extra + "\r\n" + body;
    socket->write(out); socket->disconnectFromHost();
}

Source *PhoneServer::allowedSource(const QString &key) const {
    if (!pane) return nullptr;
    for (auto source : pane->sources)
        if (source && !source->closed && source->key == key) return source;
    return nullptr;
}

Message PhoneServer::findMessage(qint64 seq, Source *source) const {
    if (!pane || !source || seq <= 0) return {};
    for (auto i = pane->view->messages.crbegin(); i != pane->view->messages.crend(); ++i)
        if (i->seq == seq && i->source == source->key && !i->system) return *i;
    return {};
}

QJsonObject PhoneServer::renderMessage(const Message &m) const {
    auto *source = allowedSource(m.source);
    QJsonArray badges, spans;
    for (auto badge : m.badges) {
        if (badge.urls.isEmpty() && source) {
            auto full = source->badges.value(badge.id + '/' + badge.version);
            if (!full.urls.isEmpty()) badge = full;
        }
        badges.append(QJsonObject{{"name", badge.label.isEmpty() ? badge.role : badge.label},
                                  {"url", safeImage(badge.urls)}});
    }
    QVector<Span> images = m.emotes;
    const QString text = visibleMessageText(m, *hub->cfg);
    if (source && !m.deleted) {
        auto matches = QRegularExpression("\\S+").globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            auto i = source->emotes.constFind(match.captured());
            if (i != source->emotes.cend()) {
                auto span = i.value(); span.start = match.capturedStart(); span.end = match.capturedEnd();
                images << span;
            }
        }
    }
    std::sort(images.begin(), images.end(), [](const Span &a, const Span &b) {
        return a.start == b.start ? a.end > b.end : a.start < b.start;
    });
    int last = 0;
    for (const auto &span : images) {
        const auto url = safeImage(span.urls);
        if (url.isEmpty() || span.start < last || span.end <= span.start || span.end > text.size()) continue;
        spans.append(QJsonObject{{"start", span.start}, {"end", span.end}, {"name", span.code}, {"url", url}});
        last = span.end;
    }
    return {{"seq", double(m.seq)}, {"source", m.source}, {"name", m.name}, {"login", m.login},
            {"text", text}, {"time", m.stamp.isValid() ? hub->cfg->time(m.stamp) : QString()},
            {"color", userColor(m).name()}, {"system", m.system}, {"deleted", m.deleted},
            {"special", m.special}, {"alert", visibleAlertText(m, *hub->cfg)}, {"alertBodyStart", m.alertBodyStart},
            {"badges", badges}, {"emotes", spans},
            {"reply", QJsonObject{{"name", m.reply["name"]}, {"text", m.reply["text"]}}}};
}

QJsonObject PhoneServer::state(qint64 after, quint32 clientRevision) const {
    QJsonArray sources, messages;
    if (!pane) return {};
    for (auto source : pane->sources) if (source && !source->closed) {
        sources.append(QJsonObject{{"key", source->key}, {"name", source->name()}, {"platform", source->platform},
                                   {"moderate", hub->hasModerationRole(source)}});
    }
    const auto &all = pane->view->messages;
    const int first = qMax(0, int(all.size()) - 250);
    quint32 revision = hub->cfg->data["show_deleted_messages"].toBool(false) ? 2166136262u : 2166136261u;
    for (int i = first; i < all.size(); ++i)
        if (all[i].deleted) revision = (revision ^ quint32(all[i].seq)) * 16777619u;
    qint64 highest = after;
    bool reset = after <= 0 || revision != clientRevision;
    if (after > 0 && first > 0 && all[first].seq > after) reset = true;
    for (int i = first; i < all.size(); ++i) {
        const auto &m = all[i];
        if (reset || m.seq > after) messages.append(renderMessage(m));
        highest = qMax(highest, m.seq);
    }
    return {{"sources", sources}, {"messages", messages}, {"after", double(highest)},
            {"revision", double(revision)}, {"reset", reset}};
}

void PhoneServer::handle(QTcpSocket *socket, QByteArray method, QUrl target,
                         QMap<QByteArray, QByteArray> headers, QByteArray body) {
    const auto host = address().mid(QString("http://").size()).toLatin1();
    if (!isActive() || headers.value("host").toLower() != host.toLower() ||
        !target.isRelative() || !target.fragment().isEmpty()) {
        respond(socket, 403, "text/plain", "Forbidden"); return;
    }
    const auto path = target.path();
    if (method == "GET" && (path == "/" || path == "/phone.js" || path == "/phone.css")) {
        QFile file(path == "/" ? ":/phone/index.html" : ":/phone" + path);
        if (!file.open(QIODevice::ReadOnly)) { respond(socket, 404, "text/plain", "Not found"); return; }
        const QByteArray type = path == "/" ? "text/html; charset=utf-8" :
                                path.endsWith(".js") ? "text/javascript; charset=utf-8" : "text/css; charset=utf-8";
        respond(socket, 200, type, file.readAll()); return;
    }
    if (method == "GET" && path == "/api/config") {
        respond(socket, 200, "application/json", QJsonDocument(QJsonObject{{"language", hub->cfg->language()}})
                                               .toJson(QJsonDocument::Compact));
        return;
    }
    if (method == "POST" && (headers.value("origin") != address().toLatin1() ||
                             !headers.value("content-type").toLower().startsWith("application/json"))) {
        respond(socket, 403, "text/plain", "Forbidden"); return;
    }
    if (method == "POST" && path == "/api/pair") {
        const auto client = socket->peerAddress().toString();
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        for (auto it = attempts.begin(); it != attempts.end();)
            if (it->until <= now) it = attempts.erase(it); else ++it;
        if (!attempts.contains(client) && attempts.size() >= 256) {
            respond(socket, 429, "text/plain", "Too many attempts"); return;
        }
        auto &a = attempts[client];
        if (a.until <= now) { a.count = 0; a.until = now + 60; }
        if (a.count >= 5) { respond(socket, 429, "text/plain", "Too many attempts"); return; }
        const auto supplied = QJsonDocument::fromJson(body).object()["code"].toString();
        if (!codeAge.isValid() || codeAge.elapsed() > 300000 || supplied != code || code.isEmpty()) {
            ++a.count; respond(socket, 403, "application/json", "{\"error\":\"Invalid or expired code.\"}"); return;
        }
        for (auto it = sessions.begin(); it != sessions.end();)
            if (it->expires <= now) it = sessions.erase(it); else ++it;
        if (sessions.size() >= 8) { respond(socket, 429, "text/plain", "Too many devices"); return; }
        const auto token = randomHex(32);
        sessions[QCryptographicHash::hash(token, QCryptographicHash::Sha256)] = {now + 12 * 3600, false, 0};
        a.count = 0;
        respond(socket, 200, "application/json", "{\"ok\":true}",
                "Set-Cookie: ytphone=" + token + "; HttpOnly; SameSite=Strict; Path=/\r\n");
        return;
    }
    QByteArray token;
    for (auto cookie : headers.value("cookie").split(';')) {
        cookie = cookie.trimmed();
        if (cookie.startsWith("ytphone=")) token = cookie.mid(8);
    }
    const auto key = QCryptographicHash::hash(token, QCryptographicHash::Sha256);
    auto session = sessions.find(key);
    if (token.size() != 64 || session == sessions.end() || session->expires <= QDateTime::currentSecsSinceEpoch()) {
        if (session != sessions.end()) sessions.erase(session);
        respond(socket, 401, "application/json", "{\"error\":\"Pair again on your PC.\"}"); return;
    }
    if (method == "GET" && path == "/api/state") {
        const QUrlQuery query(target);
        bool ok = false; const auto after = query.queryItemValue("after").toLongLong(&ok);
        bool revisionOk = false; const auto revision = query.queryItemValue("revision").toUInt(&revisionOk);
        respond(socket, 200, "application/json",
                QJsonDocument(state(ok ? qMax(qint64(0), after) : 0, revisionOk ? revision : 0))
                    .toJson(QJsonDocument::Compact));
        return;
    }
    if (method == "GET" && path == "/api/emotes") {
        auto *source = allowedSource(QUrlQuery(target).queryItemValue("source", QUrl::FullyDecoded));
        if (!source) { respond(socket, 400, "application/json", "{\"error\":\"Invalid channel.\"}"); return; }
        QJsonArray emotes;
        QStringList names = source->emotes.keys(); names.sort(Qt::CaseInsensitive);
        for (const auto &name : names) {
            const auto url = safeImage(source->emotes.value(name).urls);
            if (!url.isEmpty()) emotes.append(QJsonObject{{"name", name}, {"url", url}});
            if (emotes.size() >= 500) break;
        }
        respond(socket, 200, "application/json",
                QJsonDocument(QJsonObject{{"emotes", emotes}}).toJson(QJsonDocument::Compact));
        return;
    }
    if (method == "POST" && path == "/api/logout") {
        sessions.remove(key);
        respond(socket, 200, "application/json", "{\"ok\":true}",
                "Set-Cookie: ytphone=; Max-Age=0; HttpOnly; SameSite=Strict; Path=/\r\n"); return;
    }
    if (method == "POST" && (path == "/api/send" || path == "/api/moderate")) {
        if (session->sending) { respond(socket, 429, "application/json", "{\"error\":\"Please wait.\"}"); return; }
        QJsonParseError parseError;
        const auto request = QJsonDocument::fromJson(body, &parseError).object();
        auto *source = allowedSource(str(request["source"]));
        if (parseError.error != QJsonParseError::NoError || !source) {
            respond(socket, 400, "application/json", "{\"error\":\"Invalid channel.\"}"); return;
        }
        const auto seq = qint64(request["seq"].toDouble());
        const auto message = findMessage(seq, source);
        if (path == "/api/send") {
            const auto text = str(request["text"]).trimmed();
            if (text.isEmpty() || text.size() > 2000 || (seq && (message.id.isEmpty() || message.deleted ||
                (source->platform != "twitch" && source->platform != "kick")))) {
                respond(socket, 400, "application/json", "{\"error\":\"Invalid message or reply.\"}"); return;
            }
            session->sending = true;
            const auto requestId = ++session->request;
            QPointer<PhoneServer> self(this); QPointer<QTcpSocket> client(socket);
            QTimer::singleShot(45000, this, [self, key, requestId] {
                if (self && self->sessions.contains(key) && self->sessions[key].request == requestId)
                    self->sessions[key].sending = false;
            });
            hub->send(source, text, message, [self, client, key, requestId](QJsonObject, QString error) {
                if (!self) return;
                if (self->sessions.contains(key) && self->sessions[key].request == requestId)
                    self->sessions[key].sending = false;
                if (!client) return;
                const auto result = error.isEmpty() ? QJsonObject{{"ok", true}} : QJsonObject{{"error", error.left(500)}};
                self->respond(client, error.isEmpty() ? 200 : 400, "application/json",
                              QJsonDocument(result).toJson(QJsonDocument::Compact));
            });
            return;
        }
        const auto action = str(request["action"]);
        const int duration = request["duration"].toInt();
        if (!hub->hasModerationRole(source) || message.id.isEmpty() || message.deleted ||
            !QStringList{"delete", "timeout", "ban", "unban"}.contains(action) ||
            (action == "timeout" && !QList<int>{60, 600, 3600}.contains(duration))) {
            respond(socket, 403, "application/json", "{\"error\":\"Moderation is unavailable.\"}"); return;
        }
        session->sending = true;
        const auto requestId = ++session->request;
        QPointer<PhoneServer> self(this); QPointer<QTcpSocket> client(socket);
        QTimer::singleShot(45000, this, [self, key, requestId] {
            if (self && self->sessions.contains(key) && self->sessions[key].request == requestId)
                self->sessions[key].sending = false;
        });
        hub->moderate(source, message, action, duration, [self, client, key, requestId](QJsonObject, QString error) {
            if (!self) return;
            if (self->sessions.contains(key) && self->sessions[key].request == requestId)
                self->sessions[key].sending = false;
            if (!client) return;
            const auto result = error.isEmpty() ? QJsonObject{{"ok", true}} : QJsonObject{{"error", error.left(500)}};
            self->respond(client, error.isEmpty() ? 200 : 400, "application/json",
                          QJsonDocument(result).toJson(QJsonDocument::Compact));
        });
        return;
    }
    respond(socket, 404, "text/plain", "Not found");
}
}
