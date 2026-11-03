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
    const auto modern = identity["badges_v2"].toArray();
    if (!modern.isEmpty()) {
        for (const auto &entry : modern) {
            const auto b = entry.toObject();
            if (b.contains("selected") && !b["selected"].toBool()) continue;
            Badge badge;
            badge.id = str(b["badge_type"]);
            badge.role = badgeRole(badge.id);
            badge.label = str(b["name"]);
            const auto url = str(b["image_url"]);
            if (!url.isEmpty()) badge.urls << url;
            if (!badge.id.isEmpty()) message.badges << badge;
        }
    } else {
        for (const auto &entry : identity["badges"].toArray()) {
            const auto b = entry.toObject();
            Badge badge;
            badge.id = str(b["type"]);
            badge.role = badgeRole(badge.id);
            badge.label = str(b["text"]);
            if (!badge.id.isEmpty()) message.badges << badge;
        }
    }
    auto metadata = o["metadata"].toObject();
    if (o["metadata"].isString()) metadata = QJsonDocument::fromJson(str(o["metadata"]).toUtf8()).object();
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
    if (error.contains("429")) kickRetryMs = qMax(kickRetryMs, 120000);
    connectionStatus(hub->cfg->T("Kick chat is temporarily unavailable. Retrying…"));
    poll.start(kickRetryMs);
    kickRetryMs = qMin(300000, kickRetryMs * 2);
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
        self->kickChannelId = channelId;
        const auto stream = data["livestream"].toObject();
        const auto categories = stream["categories"].toArray();
        const auto recent = data["recent_categories"].toArray();
        const auto category = !categories.isEmpty() ? categories.first().toObject() : firstItem(recent).toObject();
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
        self->header["started_at"] = started;
        for (auto i = self->hub->globals.cbegin(); i != self->hub->globals.cend(); ++i) {
            if (!self->channelEmotes.contains(i.key())) self->emotes[i.key()] = i.value();
        }
        emit self->changed();
        emit self->hub->sourceChanged(self);
        if (!self->requesting && !self->poll.isActive()) self->poll.start(0);
    });
}

void Source::kickPoll() {
    if (closed || hub->testMode || requesting) return;
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
            self->connectionStatus(self->hub->cfg->T("Kick chat is busy; some messages may be missing."));
        self->kickInitial = false;
        self->requesting = false;
        self->kickRetryMs = 5000;
        if (historical) self->connectionStatus(self->hub->cfg->T("Connected to Kick"));
        self->poll.start(5000);
    });
}
} // namespace yc
