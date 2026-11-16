#include "automod.hpp"

namespace yc {
TwitchAutoMod::TwitchAutoMod(Hub *owner) : QObject(owner), hub(owner) {
    refreshTimer.setSingleShot(true);
    retryTimer.setSingleShot(true);
    watchdog.setSingleShot(true);
    connect(&refreshTimer, &QTimer::timeout, this, &TwitchAutoMod::refresh);
    connect(&retryTimer, &QTimer::timeout, this, &TwitchAutoMod::refresh);
    connect(&watchdog, &QTimer::timeout, this, [this] { if (active) active->abort(); });
    connect(hub->accounts, &Accounts::changed, this, &TwitchAutoMod::scheduleRefresh);
    connect(hub, &Hub::sourceChanged, this, [this](Source *) { scheduleRefresh(); });
}

void TwitchAutoMod::scheduleRefresh() {
    if (!refreshTimer.isActive()) refreshTimer.start(100);
}

void TwitchAutoMod::refresh() {
    if (hub->testMode) return;
    const auto uid = str(hub->accounts->twitch["user_id"]);
    const auto token = str(hub->accounts->twitch["access_token"]);
    const auto key = uid + ':' + QString::fromLatin1(QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256).toHex());
    bool wanted = false;
    if (hub->accounts->twitchAllows({"moderator:manage:automod"})) {
        for (auto *source : hub->sources) {
            if (source && source->platform == "twitch" && !source->closed && !source->isVod &&
                QRegularExpression("^[0-9]{1,20}$").match(source->room()).hasMatch() && hub->hasModerationRole(source)) {
                wanted = true; break;
            }
        }
    }
    if (!wanted || (!accountKey.isEmpty() && key != accountKey)) {
        stopping = true;
        if (active) active->abort();
        if (migrating) migrating->abort();
        active.clear(); migrating.clear(); session.clear(); subscribed.clear(); pending.clear(); subscriptionIds.clear(); retryAfter.clear();
        watchdog.stop(); retryTimer.stop(); stopping = false;
    }
    accountKey = key;
    if (!wanted) return;
    if (!active && !retryTimer.isActive()) connectSocket(QUrl("wss://eventsub.wss.twitch.tv/ws"));
    else if (!session.isEmpty()) subscribe();
}

void TwitchAutoMod::connectSocket(const QUrl &url, bool migration) {
    if (url.scheme() != "wss" || url.host().compare("eventsub.wss.twitch.tv", Qt::CaseInsensitive) != 0 ||
        !url.userInfo().isEmpty() || (url.port(-1) != -1 && url.port() != 443)) return;
    if ((migration && migrating) || (!migration && active)) return;
    auto *socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    socket->setMaxAllowedIncomingFrameSize(65536);
    socket->setMaxAllowedIncomingMessageSize(65536);
    if (migration) migrating = socket; else active = socket;
    connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &text) {
        if (text.size() > 65536) { socket->abort(); return; }
        const auto document = QJsonDocument::fromJson(text.toUtf8());
        if (document.isObject()) frame(socket, document.object());
    });
    connect(socket, &QWebSocket::disconnected, this, [this, socket] {
        if (socket == migrating) { migrating.clear(); socket->deleteLater(); return; }
        if (socket == active) {
            active.clear(); session.clear(); subscribed.clear(); pending.clear(); subscriptionIds.clear(); retryAfter.clear(); watchdog.stop();
            if (!stopping && !retryTimer.isActive()) {
                retryTimer.start(retryMs); retryMs = qMin(retryMs * 2, 60000);
            }
        }
        socket->deleteLater();
    });
    socket->open(url);
}

void TwitchAutoMod::frame(QWebSocket *socket, const QJsonObject &message) {
    if (socket != active && socket != migrating) return;
    const auto metadata = message["metadata"].toObject();
    const auto type = str(metadata["message_type"]);
    const auto payload = message["payload"].toObject();
    if (type == "session_welcome") {
        const auto info = payload["session"].toObject();
        const auto id = str(info["id"]);
        if (id.isEmpty() || id.size() > 256) { socket->abort(); return; }
        const bool moved = socket == migrating;
        if (moved) {
            auto old = active; active = socket; migrating.clear();
            if (old) old->close();
        } else { subscribed.clear(); pending.clear(); subscriptionIds.clear(); retryAfter.clear(); }
        session = id; retryMs = 3000;
        watchdog.start((qBound(10, info["keepalive_timeout_seconds"].toInt(10), 600) + 5) * 1000);
        if (!moved) subscribe();
        return;
    }
    if (socket != active) return;
    if (watchdog.isActive()) watchdog.start();
    if (type == "session_reconnect") {
        connectSocket(QUrl(str(at(payload, {"session", "reconnect_url"}))), true); return;
    }
    if (type != "notification") return;
    const auto id = str(metadata["message_id"]);
    if (id.isEmpty() || id.size() > 128 || eventIds.contains(id)) return;
    eventIds.insert(id); eventOrder.enqueue(id);
    while (eventOrder.size() > 512) eventIds.remove(eventOrder.dequeue());
    event(payload);
}

void TwitchAutoMod::subscribe() {
    if (!active || session.isEmpty()) return;
    const QString currentSession = session, moderator = str(hub->accounts->twitch["user_id"]);
    QSet<QString> wanted;
    for (auto *source : hub->sources) {
        if (!source || source->platform != "twitch" || source->closed || source->isVod ||
            !QRegularExpression("^[0-9]{1,20}$").match(source->room()).hasMatch() || !hub->hasModerationRole(source)) continue;
        for (const auto &type : {QStringLiteral("automod.message.hold"), QStringLiteral("automod.message.update")}) {
            const QString key = source->room() + ':' + type;
            wanted.insert(key);
            if (subscribed.contains(key) || pending.contains(key) || retryAfter.value(key) > QDateTime::currentMSecsSinceEpoch()) continue;
            pending.insert(key);
            const QJsonObject body{{"type", type}, {"version", "2"},
                {"condition", QJsonObject{{"broadcaster_user_id", source->room()}, {"moderator_user_id", moderator}}},
                {"transport", QJsonObject{{"method", "websocket"}, {"session_id", currentSession}}}};
            hub->accounts->helix("eventsub/subscriptions", [this, currentSession, key](QJsonObject result, QString error) {
                if (session != currentSession) return;
                pending.remove(key);
                const auto id = str(firstItem(result["data"].toArray()).toObject()["id"]);
                if (error.isEmpty() && !id.isEmpty()) {
                    subscribed.insert(key); subscriptionIds[key] = id; scheduleRefresh();
                } else {
                    retryAfter[key] = QDateTime::currentMSecsSinceEpoch() + 30000;
                    if (!retryTimer.isActive()) retryTimer.start(30000);
                }
            }, "POST", {}, body);
        }
    }
    const auto existing = subscribed;
    for (const auto &key : existing) {
        const auto id = subscriptionIds.value(key), marker = "remove:" + key;
        if (wanted.contains(key) || id.isEmpty() || pending.contains(marker) || retryAfter.value(marker) > QDateTime::currentMSecsSinceEpoch()) continue;
        pending.insert(marker);
        hub->accounts->helix("eventsub/subscriptions", [this, currentSession, key, marker](QJsonObject, QString error) {
            if (session != currentSession) return;
            pending.remove(marker);
            if (error.isEmpty()) { subscribed.remove(key); subscriptionIds.remove(key); scheduleRefresh(); }
            else {
                retryAfter[marker] = QDateTime::currentMSecsSinceEpoch() + 30000;
                if (!retryTimer.isActive()) retryTimer.start(30000);
            }
        }, "DELETE", {{"id", id}});
    }
}

void TwitchAutoMod::event(const QJsonObject &payload) {
    const auto subscription = payload["subscription"].toObject();
    const auto type = str(subscription["type"]);
    if (type != "automod.message.hold" && type != "automod.message.update") return;
    const auto condition = subscription["condition"].toObject();
    const auto moderator = str(hub->accounts->twitch["user_id"]);
    const auto info = payload["event"].toObject();
    const auto broadcaster = str(info["broadcaster_user_id"]), id = str(info["message_id"]);
    if (moderator.isEmpty() || str(condition["moderator_user_id"]) != moderator ||
        str(condition["broadcaster_user_id"]) != broadcaster || broadcaster.isEmpty() || id.isEmpty() || id.size() > 128) return;
    for (auto *source : hub->sources) {
        if (!source || source->closed || source->isVod || source->platform != "twitch" ||
            source->room() != broadcaster || !hub->hasModerationRole(source)) continue;
        if (type == "automod.message.update") {
            const auto status = str(info["status"]).toLower();
            if (QStringList{"approved", "denied", "expired"}.contains(status)) resolve(source, id, status);
            continue;
        }
        if (source->autoModPending.contains(id) || source->seen.contains("automod:" + id)) continue;
        Message message;
        message.system = true; message.special = "automod-held"; message.apiId = id;
        message.id = "automod:" + id;
        message.name = str(info["user_name"]).left(100);
        message.login = str(info["user_login"]).left(100);
        message.uid = str(info["user_id"]).left(40);
        message.text = str(at(info, {"message", "text"})).left(4000);
        message.stamp = QDateTime::fromString(str(info["held_at"]), Qt::ISODateWithMs);
        if (!message.stamp.isValid()) message.stamp = QDateTime::currentDateTimeUtc();
        source->autoModPending.insert(id);
        source->publish(message);
    }
}

void TwitchAutoMod::resolve(Source *source, const QString &messageId, const QString &status) {
    if (!source || !source->autoModPending.remove(messageId)) return;
    source->autoModInFlight.remove(messageId);
    for (auto &message : source->history) if (message.special == "automod-held" && message.apiId == messageId) {
        message.special = "automod-resolved"; message.alertKind = status; break;
    }
    emit source->changed();
}

void TwitchAutoMod::review(Source *source, const QString &messageId, bool allow, JsonCallback done) {
    if (!source || source->closed || source->platform != "twitch" || !hub->hasModerationRole(source) ||
        !source->autoModPending.contains(messageId) || source->autoModInFlight.contains(messageId)) {
        done({}, "This AutoMod message is no longer available."); return;
    }
    if (!hub->accounts->twitchAllows({"moderator:manage:automod"})) {
        done({}, "Enable the required Twitch permissions in Settings → Accounts."); return;
    }
    source->autoModInFlight.insert(messageId);
    QPointer<Source> safe(source);
    const QJsonObject body{{"user_id", str(hub->accounts->twitch["user_id"])}, {"msg_id", messageId},
                           {"action", allow ? "ALLOW" : "DENY"}};
    hub->accounts->helix("moderation/automod/message", [this, safe, messageId, allow, done](QJsonObject result, QString error) {
        if (safe) {
            safe->autoModInFlight.remove(messageId);
            if (error.isEmpty()) resolve(safe, messageId, allow ? "approved" : "denied");
        }
        done(result, error);
    }, "POST", {}, body);
}
} // namespace yc
