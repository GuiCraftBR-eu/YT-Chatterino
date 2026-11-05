#include "chat.hpp"
#include <algorithm>

namespace yc {

Message parseKick(const QJsonObject &o, bool historical) {
    Message message;
    message.id = str(o["id"]);
    const auto sender = o["sender"].toObject();
    const auto identity = sender["identity"].toObject();
    message.uid = str(sender["id"]);
    message.name = str(sender["username"]);
    message.login = str(sender["slug"]);
    if (message.login.isEmpty()) message.login = message.name;
    message.color = str(identity["color"]);
    message.history = historical;
    message.stamp = QDateTime::fromString(str(o["created_at"]), Qt::ISODateWithMs);
    if (!message.stamp.isValid()) message.stamp = QDateTime::fromString(str(o["created_at"]), Qt::ISODate);
    if (message.stamp.isValid()) message.stamp = message.stamp.toLocalTime();
    const QString content = str(o["content"]).left(16384);
    static const QRegularExpression emote(R"(\[emote:([A-Za-z0-9_-]{1,80}):([^\]\r\n]{1,100})\])");
    int previous = 0;
    auto matches = emote.globalMatch(content);
    while (matches.hasNext()) {
        const auto match = matches.next();
        message.text += content.mid(previous, match.capturedStart() - previous);
        Span span;
        span.start = message.text.size();
        span.code = match.captured(2);
        message.text += span.code;
        span.end = message.text.size();
        span.urls = {"https://files.kick.com/emotes/" + match.captured(1) + "/fullsize"};
        message.emotes << span;
        previous = match.capturedEnd();
    }
    message.text += content.mid(previous);
    auto badgeRole = [](QString role) {
        role = role.toLower();
        if (role == "broadcaster" || role == "channel_owner") return QString("owner");
        if (role == "moderator" || role == "mod") return QString("moderator");
        if (role == "subscriber" || role == "subscription" || role == "sub") return QString("subscriber");
        return role;
    };
    QSet<QString> existingRoles;
    for (const auto &entry : identity["badges_v2"].toArray()) {
        const auto b = entry.toObject();
        if (b.contains("selected") && !b["selected"].toBool()) continue;
        Badge badge;
        badge.id = str(b["name"]);
        if (badge.id.isEmpty()) badge.id = str(b["badge_type"]);
        badge.role = badgeRole(badge.id);
        badge.label = str(b["name"]);
        const auto url = str(b["image_url"]);
        if (!url.isEmpty()) badge.urls << url;
        if (!badge.id.isEmpty()) { message.badges << badge; existingRoles.insert(badge.role); }
    }
    // Kick's new global badges and its legacy channel roles are separate lists.
    for (const auto &entry : identity["badges"].toArray()) {
        const auto b = entry.toObject();
        Badge badge;
        badge.id = str(b["type"]);
        badge.role = badgeRole(badge.id);
        badge.label = str(b["text"]);
        badge.version = str(b["count"]);
        if (!badge.id.isEmpty() && !existingRoles.contains(badge.role)) {
            message.badges << badge; existingRoles.insert(badge.role);
        }
    }
    auto metadata = o["metadata"].toObject();
    if (o["metadata"].isString()) metadata = QJsonDocument::fromJson(str(o["metadata"]).toUtf8()).object();
    if (str(o["type"]) == "celebration" && !metadata["celebration"].toObject().isEmpty())
        message.special = "alert";
    const auto original = metadata["original_message"].toObject();
    const auto originalSender = metadata["original_sender"].toObject();
    if (!str(original["id"]).isEmpty()) {
        const auto originalName = str(originalSender["username"]);
        message.reply = {{"id", original["id"]}, {"text", original["content"]},
                         {"name", originalName}, {"login", originalName},
                         {"thread_id", str(o["thread_parent_id"]).isEmpty() ? original["id"] : o["thread_parent_id"]}};
    }
    return message;
}

void Source::kickError(QString error) {
    requesting = false;
    if (closed) return;
    if (error.contains("429")) {
        kickRetryMs = qMax(kickRetryMs, 120000);
        kickPollCooldownUntil = QDateTime::currentMSecsSinceEpoch() + kickRetryMs;
    }
    connectionStatus(hub->cfg->T("Kick chat is temporarily unavailable. Retrying…"));
    poll.start(kickRetryMs);
    kickRetryMs = qMin(300000, kickRetryMs * 2);
}

void Source::kickUpdateChatModes(const QJsonObject &room) {
    QJsonObject modes;
    for (const auto &pair : QList<QPair<QString, QString>>{
             {"followers_mode", "followers"}, {"following_min_duration", "follow_minutes"},
             {"subscribers_mode", "subscribers"}, {"emotes_mode", "emotes"},
             {"slow_mode", "slow"}, {"message_interval", "slow_seconds"}}) {
        if (!room.value(pair.first).isUndefined() && !room.value(pair.first).isNull()) {
            const auto value = room.value(pair.first);
            if (value.isObject()) {
                const auto setting = value.toObject();
                if (setting.contains("enabled")) modes[pair.second] = setting["enabled"];
                if (pair.first == "followers_mode" && setting.contains("min_duration"))
                    modes["follow_minutes"] = setting["min_duration"];
                if (pair.first == "slow_mode" && setting.contains("message_interval"))
                    modes["slow_seconds"] = setting["message_interval"];
            } else modes[pair.second] = value;
        }
    }
    const auto age = room["account_age"].toObject();
    if (age.contains("enabled")) modes["account_age"] = age["enabled"];
    if (age.contains("min_duration")) modes["account_age_minutes"] = age["min_duration"];
    const bool complete = modes.contains("followers") && modes.contains("subscribers") &&
                          modes.contains("emotes") && modes.contains("slow");
    if (!modes.isEmpty()) updateChatModes(modes, chatModesKnown || complete);
}

void Source::kickRefreshChatModes() {
    if (closed || platform != "kick" || hub->testMode || kickModesPending) return;
    kickModesPending = true;
    const auto epoch = generation;
    QPointer<Source> self(this);
    hub->net->json(QUrl("https://kick.com/api/v2/channels/" + id + "/chatroom"),
                   [self, epoch](QJsonObject data, QString error) {
        if (!self || self->closed || self->generation != epoch) return;
        self->kickModesPending = false;
        if (error.isEmpty() && data.contains("followers_mode") && data.contains("subscribers_mode") &&
            data.contains("emotes_mode") && data.contains("slow_mode")) {
            self->kickUpdateChatModes(data);
            self->kickModesRetryMs = 60000;
        } else {
            self->updateChatModes({}, false);
            self->kickModesRetryMs = qMin(300000, self->kickModesRetryMs * 2);
        }
        self->kickModesTimer.start(self->kickModesRetryMs);
    });
}

void Source::kickConnect() {
    if (closed || hub->testMode || kickConnecting) return;
    kickConnecting = true;
    const auto epoch = generation;
    QPointer<Source> self(this);
    hub->net->json(QUrl("https://kick.com/api/v2/channels/" + id), [self, epoch](QJsonObject data, QString error) {
        if (!self || self->closed || self->generation != epoch) return;
        self->kickConnecting = false;
        const auto channelId = str(data["id"]);
        const auto user = data["user"].toObject();
        if (!error.isEmpty() || channelId.isEmpty() || str(user["id"]).isEmpty()) {
            if (self->kickChannelId.isEmpty()) self->kickError(error);
            return;
        }
        const bool firstConnection = self->kickChannelId.isEmpty();
        self->kickChannelId = channelId;
        self->kickRoomId = str(at(data, {"chatroom", "id"}));
        self->kickUpdateChatModes(data["chatroom"].toObject());
        if (!self->kickModesPending && !self->kickModesTimer.isActive()) self->kickRefreshChatModes();
        if (firstConnection) self->kickLoadEmotes();
        if (!self->kickRoomId.isEmpty() && !self->kickSocketActive && !self->kickSocketStarting &&
            !self->kickSocketRetry.isActive()) self->kickStartSocket();
        self->kickCheckRole();
        const auto stream = data["livestream"].toObject();
        const auto categories = stream["categories"].toArray();
        const auto recent = data["recent_categories"].toArray();
        const auto category = !categories.isEmpty() ? categories.first().toObject() : firstItem(recent).toObject();
        self->kickSubscriberBadges.clear();
        for (const auto &item : data["subscriber_badges"].toArray()) {
            const auto badge = item.toObject();
            const auto url = str(at(badge, {"badge_image", "src"}));
            const int months = badge["months"].toInt();
            if (months > 0 && !url.isEmpty()) self->kickSubscriberBadges[months] = url;
        }
        self->header["channel_id"] = user["id"];
        self->header["channel"] = user["username"];
        self->header["avatar"] = user["profile_pic"];
        self->header["live"] = !stream.isEmpty();
        self->header["viewers"] = stream["viewer_count"];
        self->header["title"] = stream["session_title"];
        self->header["category"] = category["name"];
        auto started = str(stream["start_time"]);
        if (started.isEmpty()) started = str(stream["created_at"]);
        if (started.size() == 19 && started[10] == ' ') {
            started[10] = 'T';
            started += 'Z';
        }
        if (stream.isEmpty() || (!str(self->header["started_at"]).isEmpty() &&
                                 !started.isEmpty() && str(self->header["started_at"]) != started)) {
            self->header["thumbnail"] = QString();
            self->kickThumbnailRetryAt = 0;
        }
        if (!stream.isEmpty() && str(self->header["thumbnail"]).isEmpty())
            self->header["thumbnail"] = str(at(stream, {"thumbnail", "url"}));
        self->header["started_at"] = started;
        self->hub->liveChange(self, !stream.isEmpty());
        for (auto i = self->hub->globals.cbegin(); i != self->hub->globals.cend(); ++i) {
            if (!self->channelEmotes.contains(i.key())) self->emotes[i.key()] = i.value();
        }
        emit self->changed();
        emit self->hub->sourceChanged(self);
        if (!self->requesting && !self->poll.isActive()) self->poll.start(0);
    });
}

void Source::kickLoadThumbnail() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (closed || platform != "kick" || !header["live"].toBool() || kickThumbnailPending ||
        now < kickThumbnailRetryAt) return;
    kickThumbnailPending = true;
    kickThumbnailRetryAt = now + 300000;
    const auto epoch = generation;
    const auto start = str(header["started_at"]);
    QPointer<Source> self(this);
    hub->net->json(QUrl("https://api.kick.com/private/v1/channels/" + id + "/livestream"),
                   [self, epoch, start](QJsonObject data, QString error) {
        if (!self || self->closed || self->generation != epoch) return;
        self->kickThumbnailPending = false;
        if (!error.isEmpty()) {
            if (error.contains("429")) self->kickThumbnailRetryAt = QDateTime::currentMSecsSinceEpoch() + 120000;
            return;
        }
        const auto url = str(at(data, {"data", "livestream", "thumbnail_url"}));
        if (!self->header["live"].toBool() || str(self->header["started_at"]) != start ||
            !Images::allowed(QUrl(url))) return;
        if (str(self->header["thumbnail"]) != url) {
            self->header["thumbnail"] = url;
            emit self->headerChanged();
        }
    });
}

void Source::kickPoll() {
    if (closed || hub->testMode || requesting) return;
    const auto remaining = kickPollCooldownUntil - QDateTime::currentMSecsSinceEpoch();
    if (remaining > 0) { poll.start(int(qMin<qint64>(remaining, 300000))); return; }
    if (kickChannelId.isEmpty()) { kickConnect(); return; }
    requesting = true;
    kickPollHead.clear();
    kickFetchPage({}, 0, QSharedPointer<QVector<Message>>::create(), kickInitial);
}

void Source::kickFetchPage(QString cursor, int page, QSharedPointer<QVector<Message>> batch, bool historical) {
    QMap<QString, QString> query;
    if (!cursor.isEmpty()) query["cursor"] = cursor;
    const auto epoch = generation;
    QPointer<Source> self(this);
    hub->net->json(Network::query("https://kick.com/api/v2/channels/" + kickChannelId + "/messages", query),
                   [self, epoch, page, batch, historical](QJsonObject response, QString error) {
        if (!self || self->closed || self->generation != epoch) return;
        const auto data = response["data"].toObject();
        if (!error.isEmpty() || !data["messages"].isArray()) {
            self->kickError(error);
            return;
        }
        if (page == 0) {
            const auto pinObject = data["pinned_message"].toObject();
            auto pin = pinObject.contains("message") ? pinObject["message"].toObject() : pinObject;
            auto pinned = parseKick(pin, true);
            self->setDecorations(pinned.text.isEmpty() ? QJsonObject() :
                QJsonObject{{"name", pinned.name}, {"text", pinned.text}}, self->activePoll);
        }
        bool seen = false;
        for (const auto &item : data["messages"].toArray()) {
            auto m = parseKick(item.toObject(), historical);
            if (m.id.isEmpty()) continue;
            if (page == 0 && self->kickPollHead.isEmpty()) self->kickPollHead = m.id;
            if (!self->kickLastPollId.isEmpty() && m.id == self->kickLastPollId) { seen = true; break; }
            if (self->seen.contains(m.id)) { self->publish(m); continue; }
            batch->append(std::move(m));
        }
        const auto next = str(data["cursor"]);
        if (!historical && !seen && !next.isEmpty() && page < 4 && !data["messages"].toArray().isEmpty()) {
            self->kickFetchPage(next, page + 1, batch, historical);
            return;
        }
        std::reverse(batch->begin(), batch->end());
        std::stable_sort(batch->begin(), batch->end(), [](const Message &a, const Message &b) {
            if (a.stamp.isValid() != b.stamp.isValid()) return a.stamp.isValid();
            return a.stamp.isValid() && a.stamp < b.stamp;
        });
        for (auto &message : *batch) self->publish(message);
        if (!self->kickPollHead.isEmpty()) self->kickLastPollId = self->kickPollHead;
        if (!historical && !seen && !next.isEmpty() && page >= 4)
            if (!self->kickSocketActive) self->connectionStatus(self->hub->cfg->T("Kick chat is busy; some messages may be missing."));
        self->kickInitial = false;
        self->requesting = false;
        self->kickRetryMs = 5000;
        self->kickPollCooldownUntil = 0;
        if (historical) self->connectionStatus(self->hub->cfg->T("Connected to Kick"));
        self->poll.start(self->kickSocketActive ? 30000 : 5000);
    });
}

void Source::kickLoadEmotes() {
    if (closed || kickChannelId.isEmpty()) return;
    QPointer<Source> self(this);
    const auto epoch = generation;
    hub->net->json(QUrl("https://kick.com/emotes/" + id), [self, epoch](QJsonObject data, QString error) {
        if (!self || self->closed || self->generation != epoch || !error.isEmpty()) return;
        QHash<QString, Span> channel, global;
        for (const auto &setValue : data["items"].toArray()) {
            const auto set = setValue.toObject();
            const bool isChannel = str(set["id"]) == self->kickChannelId;
            for (const auto &item : set["emotes"].toArray()) {
                const auto e = item.toObject();
                const QString emoteId = str(e["id"]), code = str(e["name"]);
                if (emoteId.isEmpty() || code.isEmpty() || code.size() > 100) continue;
                Span span; span.code = code; span.urls = {"https://files.kick.com/emotes/" + emoteId + "/fullsize"};
                (isChannel ? channel : global)[code] = span;
            }
        }
        self->kickGlobalEmotes = global;
        for (auto i = global.cbegin(); i != global.cend(); ++i)
            if (!self->channelEmotes.contains(i.key())) self->emotes[i.key()] = i.value();
        for (auto i = channel.cbegin(); i != channel.cend(); ++i) {
            self->channelEmotes[i.key()] = i.value(); self->emotes[i.key()] = i.value();
        }
        emit self->changed();
    });
}

void Source::kickCheckRole() {
    const auto username = str(hub->accounts->kick["name"]);
    const auto uid = str(hub->accounts->kick["user_id"]);
    if (username.isEmpty() || uid.isEmpty() || kickChannelId.isEmpty() || closed) return;
    if (uid == room()) { header["kick_moderator"] = true; header["kick_role_uid"] = uid; emit permissionsChanged(); return; }
    QPointer<Source> self(this); const auto epoch = generation;
    const auto url = QUrl("https://kick.com/api/v2/channels/" + id + "/users/" + QString::fromLatin1(QUrl::toPercentEncoding(username)));
    hub->net->json(url, [self, epoch, uid](QJsonObject data, QString error) {
        if (!self || self->closed || self->generation != epoch || uid != str(self->hub->accounts->kick["user_id"])) return;
        const bool allowed = error.isEmpty() && str(data["id"]) == uid &&
            (data["is_moderator"].toBool() || data["is_channel_owner"].toBool());
        self->header["kick_moderator"] = allowed; self->header["kick_role_uid"] = uid; emit self->permissionsChanged();
    });
}

void Source::kickStartSocket() {
    if (closed || hub->testMode || kickSocketStarting || kickSocketActive || kickRoomId.isEmpty()) return;
    kickSocketStarting = true;
    const auto epoch = generation;
    const auto client = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QPointer<Source> self(this);
    QJsonObject body{{"client", QJsonObject{{"id", client}, {"type", "web"}}},
                     {"capabilities", QJsonObject{{"accepted_providers", QJsonArray{QJsonObject{{"provider", "centrifugo"}}}}}}};
    hub->net->json(QUrl("https://web.kick.com/api/v1/realtime/channels/" + kickChannelId + "/chat/connection"),
        [self, epoch, client](QJsonObject data, QString error) {
        if (!self || self->closed || self->generation != epoch) return;
        QUrl url;
        for (const auto &value : at(data, {"data", "connections"}).toArray()) {
            auto o = value.toObject();
            if (str(o["provider"]) == "centrifugo") url = QUrl(str(at(o, {"credentials", "url"})));
        }
        if (!error.isEmpty() || url.scheme() != "wss" || !url.host().endsWith(".platform.kick.com") ||
            !url.userInfo().isEmpty() || (url.port(-1) != -1 && url.port() != 443)) { self->kickSocketFailed(); return; }
        self->hub->net->json(QUrl("https://web.kick.com/api/v1/realtime/auth/connection"),
            [self, epoch, url](QJsonObject auth, QString e) {
                if (!self || self->closed || self->generation != epoch) return;
                self->kickSocketToken = str(at(auth, {"data", "token"}));
                if (!e.isEmpty() || self->kickSocketToken.isEmpty()) { self->kickSocketFailed(); return; }
                self->kickSocket.open(url);
                self->kickSocketWatchdog.start(20000);
            }, "POST", {{"client_id", client}});
    }, "POST", body);
}

void Source::kickSocketFailed() {
    kickSocketStarting = false; kickSocketActive = false; kickSocketToken.clear();
    kickSocketWatchdog.stop(); kickSocketRenew.stop();
    if (closed || hub->testMode || platform != "kick") return;
    if (!requesting && kickPollCooldownUntil <= QDateTime::currentMSecsSinceEpoch() &&
        (!poll.isActive() || poll.remainingTime() > 1000)) poll.start(0);
    if (!kickSocketRetry.isActive()) {
        kickSocketRetry.start(kickSocketRetryMs);
        kickSocketRetryMs = qMin(120000, kickSocketRetryMs * 2);
    }
}

void Source::kickSocketMessage(const QString &text) {
    if (closed) return;
    kickSocketWatchdog.start(65000);
    for (const auto &line : text.split('\n', Qt::SkipEmptyParts)) {
        const auto frame = QJsonDocument::fromJson(line.toUtf8()).object();
        if (frame.isEmpty()) { kickSocket.sendTextMessage("{}"); continue; }
        if (frame.contains("error")) {
            if (frame["id"].toInt() <= 2) { kickSocket.abort(); return; }
            continue; // An optional alert feed failing must not stop live chat.
        }
        if (frame.contains("connect")) {
            int command = 2;
            for (const auto &channel : QStringList{"chatrooms." + kickRoomId + ".v2", "chatroom_" + kickRoomId, "channel_" + kickChannelId}) {
                const QJsonObject request{{"id", command++}, {"subscribe", QJsonObject{{"channel", channel}}}};
                kickSocket.sendTextMessage(QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));
            }
            kickSocketRenew.start(40 * 60 * 1000);
        } else if (frame["id"].toInt() == 2 && frame.contains("subscribe")) {
            kickSocketStarting = false; kickSocketActive = true; kickSocketRetryMs = 3000;
            if (!requesting && kickPollCooldownUntil <= QDateTime::currentMSecsSinceEpoch()) poll.start(0);
        }
        const auto push = frame["push"].toObject();
        if (push.contains("disconnect") || push.contains("unsubscribe")) { kickSocket.abort(); return; }
        const auto envelope = at(push, {"pub", "data"}).toObject();
        const auto event = str(envelope["event"]);
        QJsonObject payload = envelope["data"].toObject();
        if (envelope["data"].isString()) payload = QJsonDocument::fromJson(str(envelope["data"]).toUtf8()).object();
        if (!event.isEmpty()) kickEvent(event, payload);
    }
}

void Source::kickEvent(QString event, QJsonObject data) {
    if (event.endsWith("ChatroomUpdatedEvent") || event.endsWith("ChatroomSettingsUpdatedEvent")) {
        const auto room = data["chatroom"].isObject() ? data["chatroom"].toObject() : data;
        kickUpdateChatModes(room); return;
    }
    if (event.endsWith("ChatMessageEvent")) {
        auto message = parseKick(data);
        if (!message.id.isEmpty()) publish(message);
        return;
    }
    if (event.endsWith("UserBannedEvent")) {
        const auto uid = str(at(data, {"user", "id"}));
        if (!uid.isEmpty()) redact({{"user_id", uid}});
        return;
    }
    if (event.endsWith("ChatroomClearEvent")) { redact({{"all", true}}); return; }
    if (event.endsWith("MessageDeletedEvent")) {
        const auto id = str(at(data, {"message", "id"}));
        if (!id.isEmpty()) redact({{"message_id", id}});
        return;
    }
    if (event.endsWith("PinnedMessageCreatedEvent")) {
        const auto m = parseKick(data["message"].toObject());
        setDecorations({{"name", m.name}, {"text", m.text}}, activePoll); return;
    }
    if (event.endsWith("PinnedMessageDeletedEvent")) { setDecorations({}, activePoll); return; }
    Message m; m.special = "alert"; m.stamp = QDateTime::currentDateTime();
    m.id = str(data["id"]);
    if (event.endsWith("SubscriptionEvent")) {
        m.name = str(data["username"]);
        const int months = data["months"].toInt(1);
        m.text = hub->cfg->T("Subscribed for {months} months!", {{"months", QString::number(months)}});
    } else if (event.endsWith("GiftedSubscriptionsEvent")) {
        m.name = str(data["gifter_username"]);
        m.text = hub->cfg->T("Gifted {count} subscriptions!", {{"count", QString::number(data["gifted_usernames"].toArray().size())}});
    } else if (event.endsWith("RewardRedeemedEvent")) {
        m.name = str(at(data, {"user", "username"}));
        if (m.name.isEmpty()) m.name = str(data["username"]);
        m.text = str(data["reward_title"]);
        if (m.text.isEmpty()) m.text = str(at(data, {"reward", "title"}));
        const auto input = str(data["user_input"]);
        if (!input.isEmpty()) m.text += " — " + input;
    } else if (event.endsWith("KicksGifted")) {
        m.id = str(data["gift_transaction_id"]); m.name = str(at(data, {"sender", "username"}));
        m.text = str(at(data, {"gift", "name"}));
        const auto body = str(data["message"]); if (!body.isEmpty()) m.text += " — " + body;
    } else return;
    m.name = m.name.left(100); m.text = m.text.left(2000);
    if (m.name.isEmpty()) m.name = "Kick";
    m.alertText = m.text;
    m.alertBodyStart = m.text.size();
    if (!m.text.isEmpty()) publish(m);
}
} // namespace yc
