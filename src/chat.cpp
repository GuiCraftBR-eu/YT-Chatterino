#include "chat.hpp"
#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif
namespace yc {
static QJsonValue first(const QJsonArray &a) {
    return a.isEmpty() ? QJsonValue() : a[0];
}

static QJsonObject configOf(const QString &html) {
    QJsonObject config;
    qsizetype offset = 0;
    while ((offset = html.indexOf("ytcfg.set(", offset)) >= 0) {
        auto d = embeddedJson(html.mid(offset), "ytcfg.set(");
        for (auto i = d.begin(); i != d.end(); ++i) {
            config[i.key()] = i.value();
        }
        offset += 10;
    }
    return config;
}

static QString continuationOf(const QJsonObject &chat, int *wait = nullptr) {
    for (const auto &v : chat["continuations"].toArray()) {
        auto o = v.toObject();
        for (auto i = o.begin(); i != o.end(); ++i) {
            auto d = i.value().toObject();
            auto c = str(d["continuation"]);
            if (!c.isEmpty()) {
                if (wait) {
                    *wait = qBound(4000, d["timeoutMs"].toInt(5000), 120000);
                }
                return c;
            }
        }
    }
    return {};
}

static QJsonObject metadata(const QString &html, const QString &id, QString *error) {
    auto p = embeddedJson(html, "ytInitialPlayerResponse"), d = p["videoDetails"].toObject();
    if (d.isEmpty() || str(d["videoId"]) != id) {
        *error = textOf(at(p, {"playabilityStatus", "reason"}));
        if (error->isEmpty()) {
            *error = "YouTube video is unavailable to anonymous viewers.";
        }
        return {};
    }
    bool live = d["isLive"].toBool();
    qint64 viewers = -1;
    auto data = embeddedJson(html, "ytInitialData");
    walk(data, [&](const QJsonObject &node) {
        auto v = node["videoViewCountRenderer"].toObject();
        if (live && v["isLive"].toBool()) {
            auto digits = textOf(v["viewCount"]);
            digits.remove(QRegularExpression("[^0-9]"));
            if (!digits.isEmpty()) {
                viewers = digits.toLongLong();
            }
        }
    });
    if (live && viewers < 0) {
        viewers = str(d["viewCount"]).toLongLong();
    }
    return {{"channel", d["author"]},
            {"channel_id", d["channelId"]},
            {"title", d["title"]},
            {"live", live},
            {"viewers", double(viewers)}};
}

Source::Source(Hub *h, QString p, QString value)
    : QObject(h), hub(h), platform(std::move(p)), id(std::move(value)) {
    key = platform == "twitch" ? "twitch:" + id : id;
    auto settings = hub->cfg->data["chat_settings"].toObject()[key].toObject();
    interval = qMax(4.0, settings["web_poll_interval_seconds"].toDouble(4));
    poll.setSingleShot(true);
    metadataTimer.setInterval(300000);
    connect(&poll, &QTimer::timeout, this, [this] {
        if (continuation.isEmpty()) {
            youtubeConnect();
        } else {
            youtubePoll();
        }
    });
    connect(&metadataTimer, &QTimer::timeout, this, &Source::refreshHeader);
}

QString Source::name() const {
    QString result = platform == "twitch" ? str(header["channel"]) : str(header["channel"]);
    return result.isEmpty() ? id : result;
}

QString Source::room() const {
    return str(header["channel_id"]);
}

QJsonObject Source::spec() const {
    return {{"platform", platform}, {"id", id}, {"paused", paused}};
}

void Source::start() {
    if (closed || hub->testMode) {
        return;
    }
    if (platform == "twitch") {
        hub->twitch.join(this);
        refreshHeader();
        loadHistory();
        loadEmotes();
        loadBadges();
    } else {
        youtubeConnect();
    }
    metadataTimer.start();
}

void Source::stop() {
    ++generation;
    closed = true;
    poll.stop();
    metadataTimer.stop();
    if (platform == "twitch") {
        hub->twitch.part(this);
    }
}

void Source::reconnect() {
    if (closed) {
        return;
    }
    ++generation;
    requesting = false;
    emoteLoaded.clear();
    badgesLoaded = 0;
    continuation.clear();
    retryMs = 5000;
    if (platform == "twitch") {
        hub->twitch.connectServer();
        refreshHeader();
    } else {
        poll.stop();
        youtubeConnect();
    }
}

void Source::setPaused(bool value) {
    if (platform == "twitch") {
        return;
    }
    paused = value;
    if (paused) {
        poll.stop();
    } else if (!requesting) {
        poll.start(0);
    }
    status(hub->cfg->T(paused ? "Chat paused" : "Chat resumed"));
}

void Source::trim() {
    const int cap = retentionCap();
    if (history.size() > cap) {
        history.remove(0, history.size() - cap);
    }
    while (seenOrder.size() > cap * 2) {
        seen.remove(seenOrder.dequeue());
    }
}

int Source::retentionCap() const {
    int cap = hub->cfg->cap(key);
    for (int limit : viewCaps) {
        cap = qMax(cap, limit);
    }
    return qBound(100, cap, 20000);
}

void Source::publish(Message m) {
    if (closed) {
        return;
    }
    if (!m.system && !m.id.isEmpty() && seen.contains(m.id)) {
        for (auto &old : history) {
            if (old.id == m.id && old.own) {
                m.seq = old.seq;
                m.own = true;
                m.deleted = m.deleted || old.deleted;
                old = m;
                emit changed();
                break;
            }
        }
        return;
    }
    if (!m.id.isEmpty()) {
        seen.insert(m.id);
        seenOrder.enqueue(m.id);
    }
    if (!m.system && platform == "youtube" && !m.own) {
        for (int i = history.size() - 1; i >= qMax(0, history.size() - 128); --i) {
            auto &old = history[i];
            if (old.own && old.webId.isEmpty() && old.uid == m.uid && old.text == m.text &&
                qAbs(old.stamp.secsTo(m.stamp)) < 60) {
                old.webId = m.id;
                old.badges = m.badges;
                old.emotes = m.emotes;
                emit changed();
                return;
            }
        }
    }
    m.source = key;
    m.seq = ++hub->sequence;
    history << m;
    trim();
    emit message(m);
    emit hub->message(this, m);
    hub->messageAlert(this, m);
}

void Source::status(const QString &s) {
    if (!history.isEmpty() && history.last().system && history.last().text == s) {
        return;
    }
    Message m;
    m.system = true;
    m.text = s;
    m.stamp = QDateTime::currentDateTime();
    publish(m);
}

void Source::redact(const QJsonObject &target) {
    for (auto &m : history) {
        bool match = target["all"].toBool() ||
                     (!str(target["message_id"]).isEmpty() &&
                      (str(target["message_id"]) == m.id || str(target["message_id"]) == m.apiId ||
                       str(target["message_id"]) == m.webId)) ||
                     (!str(target["user_id"]).isEmpty() && str(target["user_id"]) == m.uid);
        if (match) {
            m.deleted = true;
            m.text = hub->cfg->T("[message deleted]");
            m.emotes.clear();
        }
        if (!m.reply.isEmpty() &&
            ((!str(target["message_id"]).isEmpty() && str(m.reply["id"]) == str(target["message_id"])) ||
             (!str(target["user_id"]).isEmpty() && str(m.reply["user_id"]) == str(target["user_id"])) ||
             target["all"].toBool())) {
            m.reply["text"] = hub->cfg->T("[message deleted]");
        }
    }
    emit redacted(target);
    emit changed();
}

void Source::refreshHeader() {
    if (closed || hub->testMode) {
        return;
    }
    if (platform == "twitch" && !hub->accounts->loaded) {
        return;
    }
    QPointer<Source> self(this);
    if (platform == "youtube") {
        hub->net->bytes(Network::query("https://www.youtube.com/watch", {{"v", id}}),
                        [self](QByteArray b, QString e) {
                            if (!self || self->closed || !e.isEmpty()) {
                                return;
                            }
                            QString error;
                            auto d = metadata(QString::fromUtf8(b), self->id, &error);
                            if (error.isEmpty()) {
                                self->header = d;
                                emit self->changed();
                                emit self->hub->sourceChanged(self);
                            }
                        });
        return;
    }
    hub->accounts->helix(
        "users",
        [self](QJsonObject d, QString error) {
            if (!self || self->closed) {
                return;
            }
            if (!error.isEmpty()) {
                self->status(error);
                return;
            }
            auto user = first(d["data"].toArray()).toObject();
            auto uid = str(user["id"]);
            if (uid.isEmpty()) {
                return;
            }
            self->header["channel_id"] = uid;
            self->header["channel"] = user["display_name"];
            self->header["avatar"] = user["profile_image_url"];
            self->hub->accounts->helix("channels",
                                       [self](QJsonObject data, QString e) {
                                           if (!self || !e.isEmpty()) {
                                               return;
                                           }
                                           auto channel = first(data["data"].toArray()).toObject();
                                           self->header["title"] = channel["title"];
                                           self->header["category"] = channel["game_name"];
                                           self->header["category_id"] = channel["game_id"];
                                           emit self->changed();
                                           emit self->hub->sourceChanged(self);
                                       },
                                       "GET", {{"broadcaster_id", uid}});
            self->hub->accounts->helix("streams",
                                       [self](QJsonObject streams, QString e) {
                                           if (!self || !e.isEmpty()) {
                                               return;
                                           }
                                           auto items = streams["data"].toArray();
                                           bool live = !items.isEmpty();
                                           self->header["live"] = live;
                                           self->header["viewers"] =
                                               live ? items[0].toObject()["viewer_count"] : QJsonValue(0);
                                           if (live) {
                                               self->header["title"] = items[0].toObject()["title"];
                                               self->header["category"] = items[0].toObject()["game_name"];
                                           }
                                           self->hub->liveChange(self, live);
                                           emit self->changed();
                                           emit self->hub->sourceChanged(self);
                                       },
                                       "GET", {{"user_id", uid}});
            self->loadEmotes(uid);
            self->loadBadges();
        },
        "GET", {{"login", id}});
}

void Source::loadHistory() {
    if (historyLoaded || historyLoading || !hub->cfg->data["load_twitch_history"].toBool(true) ||
        hub->testMode) {
        return;
    }
    historyLoading = true;
    QPointer<Source> self(this);
    hub->net->json(
        Network::query(
            "https://recent-messages.robotty.de/api/v2/recent-messages/" + id,
            {{"limit", "200"}, {"hide_moderated_messages", "true"}, {"hide_moderation_messages", "true"}}),
        [self](QJsonObject d, QString error) {
            if (!self) {
                return;
            }
            self->historyLoading = false;
            self->historyLoaded = true;
            for (const auto &v : d["messages"].toArray()) {
                auto p = parseIrc(v.toString());
                if (p.command == "PRIVMSG" && p.channel == self->id) {
                    p.message.history = true;
                    self->publish(p.message);
                }
            }
            for (const auto &m : std::exchange(self->liveBuffer, {})) {
                self->publish(m);
            }
            if (!error.isEmpty()) {
                self->status(self->hub->cfg->T(
                    "Recent Twitch history unavailable; live chat is connected independently."));
            }
        });
}

void Source::loadEmotes(QString channelId) {
    if (hub->testMode) {
        return;
    }
    hub->globalEmotes();
    for (auto i = hub->globals.begin(); i != hub->globals.end(); ++i) {
        if (!emotes.contains(i.key())) {
            emotes[i.key()] = i.value();
        }
    }
    if (channelId.isEmpty()) {
        return;
    }
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (now - emoteLoaded.value(channelId) > 21600) {
        emoteLoaded[channelId] = now;
    } else {
        return;
    }
    QPointer<Source> self(this);
    hub->net->json(QUrl("https://api.betterttv.net/3/cached/users/" + platform + '/' + channelId),
                   [self](QJsonObject d, QString e) {
                       if (!self || !e.isEmpty()) {
                           return;
                       }
                       self->hub->provider(self, d["channelEmotes"], "bttv", "channel");
                       self->hub->provider(self, d["sharedEmotes"], "bttv", "channel");
                   });
    hub->net->json(QUrl("https://7tv.io/v3/users/" + platform + '/' + channelId),
                   [self](QJsonObject d, QString e) {
                       if (!self || !e.isEmpty()) {
                           return;
                       }
                       self->hub->provider(self, at(d, {"emote_set", "emotes"}), "7tv", "channel");
                   });
    if (platform == "twitch") {
        hub->accounts->helix("chat/emotes",
                             [self](QJsonObject d, QString e) {
                                 if (!self || !e.isEmpty()) {
                                     return;
                                 }
                                 self->hub->provider(self, d["data"], "twitch", "channel");
                             },
                             "GET", {{"broadcaster_id", channelId}});
    }
}

void Source::loadBadges() {
    if (platform != "twitch" || hub->testMode) {
        return;
    }
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (now - badgesLoaded < 21600) {
        return;
    }
    if (!room().isEmpty()) {
        badgesLoaded = now;
    }
    QPointer<Source> self(this);
    auto ready = [self](QJsonObject d, QString e) {
        if (!self || !e.isEmpty()) {
            return;
        }
        for (const auto &set : d["data"].toArray()) {
            auto o = set.toObject();
            for (const auto &v : o["versions"].toArray()) {
                auto b = v.toObject();
                Badge badge;
                badge.id = str(o["set_id"]);
                badge.version = str(b["id"]);
                badge.label = str(b["title"]);
                badge.urls = {str(b["image_url_4x"]), str(b["image_url_2x"]), str(b["image_url_1x"])};
                badge.urls.removeAll({});
                badge.role = badge.id == "broadcaster" ? "owner" : badge.id;
                self->badges[badge.id + '/' + badge.version] = badge;
            }
        }
        emit self->changed();
    };
    hub->accounts->helix("chat/badges/global", ready);
    if (!room().isEmpty()) {
        hub->accounts->helix("chat/badges", ready, "GET", {{"broadcaster_id", room()}});
    }
}

void Source::youtubeConnect() {
    if (closed || paused || requesting || hub->testMode) {
        return;
    }
    requesting = true;
    const auto epoch = generation;
    QPointer<Source> self(this);
    hub->net->bytes(
        Network::query("https://www.youtube.com/watch", {{"v", id}}),
        [self, epoch](QByteArray bytes, QString error) {
            if (!self || self->closed || self->generation != epoch) {
                return;
            }
            if (!error.isEmpty()) {
                self->youtubeError(error);
                return;
            }
            const auto html = QString::fromUtf8(bytes);
            QString metadataError;
            auto info = metadata(html, self->id, &metadataError);
            if (!metadataError.isEmpty()) {
                self->youtubeError(metadataError, 60000);
                return;
            }
            self->header = info;
            emit self->changed();
            emit self->hub->sourceChanged(self);
            self->loadEmotes(str(info["channel_id"]));
            QJsonObject chat;
            walk(embeddedJson(html, "ytInitialData"), [&](const QJsonObject &n) {
                if (n.contains("liveChatRenderer")) {
                    chat = n["liveChatRenderer"].toObject();
                }
            });
            if (chat.isEmpty()) {
                self->youtubeError(
                    "No active live chat. The broadcast is offline, ended, or chat is disabled.", 60000);
                return;
            }
            self->youtubePopout(chat, configOf(html));
        },
        "GET", {},
        {{"User-Agent",
          "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/130.0.0.0 Safari/537.36"}},
        16 * 1024 * 1024);
}

void Source::youtubePopout(QJsonObject chat, QJsonObject config) {
    QPointer<Source> self(this);
    const auto epoch = generation;
    hub->net->bytes(
        Network::query("https://www.youtube.com/live_chat", {{"v", id}, {"is_popout", "1"}}),
        [self, chat, config, epoch](QByteArray bytes, QString error) mutable {
            if (!self || self->closed || self->generation != epoch) {
                return;
            }
            if (!error.isEmpty()) {
                self->youtubeError(error);
                return;
            }
            auto html = QString::fromUtf8(bytes);
            auto full = at(embeddedJson(html, "ytInitialData"), {"contents", "liveChatRenderer"}).toObject();
            if (!full.isEmpty()) {
                chat = full;
            }
            auto c = configOf(html);
            for (auto i = c.begin(); i != c.end(); ++i) {
                config[i.key()] = i.value();
            }
            self->youtubeContinue(chat, config);
        },
        "GET", {},
        {{"User-Agent",
          "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/130.0.0.0 Safari/537.36"}},
        16 * 1024 * 1024);
}

void Source::youtubeContinue(QJsonObject chat, QJsonObject config) {
    context = config["INNERTUBE_CONTEXT"].toObject();
    continuation = continuationOf(chat, &recommendedMs);
    auto selectors = at(chat, {"header", "liveChatHeaderRenderer", "viewSelector",
                               "sortFilterSubMenuRenderer", "subMenuItems"})
                         .toArray();
    for (const auto &v : selectors) {
        auto item = v.toObject();
        auto token = str(at(item, {"continuation", "reloadContinuationData", "continuation"}));
        if (str(item["title"]).compare("Live chat", Qt::CaseInsensitive) == 0 && token.size() > 100) {
            continuation = token;
        }
    }
    if (str(at(context, {"client", "clientVersion"})).isEmpty() || continuation.isEmpty()) {
        youtubeError("YouTube changed its chat page. Use Open chat in browser until the app is updated.",
                     120000);
        return;
    }
    requesting = false;
    initial = true;
    retryMs = 5000;
    youtubeActions(chat["actions"].toArray(), true);
    for (const auto &v : chat["items"].toArray()) {
        auto item = v.toObject();
        for (auto i = item.begin(); i != item.end(); ++i) {
            auto m = parseYouTube(i.value().toObject(), i.key(), true);
            if (!m.id.isEmpty() && !m.name.isEmpty() && !m.text.isEmpty()) {
                publish(m);
            }
        }
    }
    status("Connected to YouTube · no API key · 0 reading units");
    if (!paused) {
        poll.start(0);
    }
}

void Source::youtubeActions(const QJsonArray &actions, bool historical) {
    for (const auto &v : actions) {
        auto a = v.toObject();
        auto item = at(a, {"addChatItemAction", "item"}).toObject();
        for (auto i = item.begin(); i != item.end(); ++i) {
            if (!i.key().contains("Renderer")) {
                continue;
            }
            auto m = parseYouTube(i.value().toObject(), i.key(), historical);
            if (!m.id.isEmpty() && !m.name.isEmpty() && !m.text.isEmpty()) {
                for (const auto &e : m.emotes) {
                    if (!e.code.isEmpty()) {
                        emotes[e.code] = e;
                    }
                }
                publish(m);
            }
        }
        for (const auto &key : QStringList{"markChatItemAsDeletedAction", "removeChatItemAction"}) {
            auto target = str(at(a, {key, "targetItemId"}));
            if (!target.isEmpty()) {
                redact({{"message_id", target}});
            }
        }
        auto uid = str(at(a, {"markChatItemsByAuthorAsDeletedAction", "externalChannelId"}));
        if (!uid.isEmpty()) {
            redact({{"user_id", uid}});
        }
    }
}

void Source::youtubePoll() {
    if (closed || paused || requesting) {
        return;
    }
    if (continuation.isEmpty()) {
        youtubeConnect();
        return;
    }
    requesting = true;
    const auto epoch = generation;
    ++polls;
    QPointer<Source> self(this);
    QMap<QByteArray, QByteArray> headers{
        {"Origin", "https://www.youtube.com"},
        {"Referer", ("https://www.youtube.com/live_chat?v=" + id).toUtf8()},
        {"X-Youtube-Client-Name", "1"},
        {"X-Youtube-Client-Version", str(at(context, {"client", "clientVersion"})).toUtf8()}};
    if (!visitor.isEmpty()) {
        headers["X-Goog-Visitor-Id"] = visitor.toUtf8();
    }
    hub->net->json(
        QUrl("https://www.youtube.com/youtubei/v1/live_chat/get_live_chat?prettyPrint=false"),
        [self, epoch](QJsonObject d, QString error) {
            if (!self || self->closed || self->generation != epoch) {
                return;
            }
            if (!error.isEmpty()) {
                self->youtubeError(error, error.contains("429") ? 120000 : 0);
                return;
            }
            auto chat = at(d, {"continuationContents", "liveChatContinuation"}).toObject();
            if (chat.isEmpty()) {
                self->continuation.clear();
                self->youtubeError("The live chat connection expired. Reconnecting…", 15000);
                return;
            }
            self->continuation = continuationOf(chat, &self->recommendedMs);
            self->visitor = str(at(d, {"responseContext", "visitorData"}));
            self->youtubeActions(chat["actions"].toArray(), self->initial);
            self->initial = false;
            self->requesting = false;
            self->retryMs = 5000;
            if (self->continuation.isEmpty()) {
                self->status("The live chat has ended.");
                return;
            }
            if (!self->paused) {
                self->poll.start(qMax(int(self->interval * 1000), self->recommendedMs));
            }
        },
        "POST", {{"context", context}, {"continuation", continuation}}, headers);
}

void Source::youtubeError(const QString &error, int delay) {
    requesting = false;
    const bool rateLimited = error.startsWith("HTTP 429:");
    const int wait = delay ? delay : (rateLimited ? 120000 : retryMs);
    const auto message = rateLimited
                             ? hub->cfg->T("YouTube temporarily limited requests (HTTP 429). Try again later "
                                           "or open the chat in your browser.")
                             : error;
    status(message + " " + hub->cfg->T("Retrying in {seconds}s.", {{"seconds", QString::number(wait / 1000)}}));
    retryMs = qMin(120000, retryMs * 2);
    if (!closed && !paused) {
        poll.start(wait);
    }
}

TwitchConnection::TwitchConnection(Hub *h) : QObject(h), hub(h) {
    retry.setSingleShot(true);
    retry.setInterval(5000);
    heartbeat.setInterval(120000);
    connect(&retry, &QTimer::timeout, this, &TwitchConnection::connectServer);
    connect(&socket, &QSslSocket::encrypted, this, [this] {
        registered = false;
        socket.write("CAP REQ :twitch.tv/tags twitch.tv/commands twitch.tv/membership\r\n");
        socket.write("PASS oauth:" + str(hub->accounts->twitch["access_token"]).toUtf8() + "\r\n");
        socket.write("NICK " + str(hub->accounts->twitch["login"]).toUtf8() + "\r\n");
        heartbeat.start();
    });
    connect(&socket, &QSslSocket::readyRead, this, [this] {
        buffer += socket.readAll();
        if (buffer.size() > 1024 * 1024) {
            socket.abort();
            buffer.clear();
            return;
        }
        int pos;
        while ((pos = buffer.indexOf("\r\n")) >= 0) {
            auto raw = buffer.left(pos);
            buffer.remove(0, pos + 2);
            line(QString::fromUtf8(raw));
        }
    });
    connect(&socket, &QSslSocket::disconnected, this, [this] {
        registered = false;
        connecting = false;
        heartbeat.stop();
        if (!closing && !authRejected && !channels.isEmpty()) {
            retry.start();
        }
    });
    connect(&socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (closing || authRejected) {
            return;
        }
        connecting = false;
        for (auto source : channels) {
            if (source) {
                source->status("Twitch connection interrupted. Reconnecting…");
            }
        }
        retry.start();
    });
    connect(&heartbeat, &QTimer::timeout, this, [this] {
        if (socket.state() == QAbstractSocket::ConnectedState) {
            socket.write("PING :YT-Chatterino\r\n");
        }
    });
    connect(hub->accounts, &Accounts::ready, this, [this] {
        authRejected = false;
        if (!channels.isEmpty()) {
            connectServer();
        }
    });
}

void TwitchConnection::join(Source *s) {
    channels[s->id] = s;
    if (registered) {
        socket.write("JOIN #" + s->id.toUtf8() + "\r\n");
        s->status("Connected to Twitch");
    } else {
        connectServer();
    }
}

void TwitchConnection::part(Source *s) {
    channels.remove(s->id);
    if (registered) {
        socket.write("PART #" + s->id.toUtf8() + "\r\n");
    }
}

void TwitchConnection::connectServer() {
    if (hub->testMode || !hub->accounts->loaded || closing || authRejected || channels.isEmpty() ||
        connecting) {
        return;
    }
    connecting = true;
    retry.stop();
    socket.abort();
    hub->accounts->twitchToken([this](QString token, QString error) {
        connecting = false;
        if (closing) {
            return;
        }
        if (!error.isEmpty()) {
            for (auto s : channels) {
                if (s) {
                    s->status(error);
                }
            }
            return;
        }
        if (token.isEmpty()) {
            return;
        }
        connecting = true;
        socket.connectToHostEncrypted("irc.chat.twitch.tv", 6697);
    });
}

void TwitchConnection::line(const QString &raw) {
    if (raw.startsWith("PING ")) {
        socket.write("PONG " + raw.mid(5).toUtf8() + "\r\n");
        return;
    }
    auto p = parseIrc(raw);
    if (p.command == "NOTICE" && (raw.contains("Login authentication failed", Qt::CaseInsensitive) ||
                                  raw.contains("Improperly formatted auth", Qt::CaseInsensitive))) {
        authRejected = true;
        retry.stop();
        heartbeat.stop();
        socket.abort();
        for (auto s : channels) {
            if (s) {
                s->status(hub->cfg->T("Twitch sign-in expired. Open Settings → Accounts and sign in again."));
            }
        }
        return;
    }
    if (p.command == "001") {
        registered = true;
        connecting = false;
        for (auto i = channels.begin(); i != channels.end(); ++i) {
            if (i.value()) {
                socket.write("JOIN #" + i.key().toUtf8() + "\r\n");
                i.value()->status("Connected to Twitch");
            }
        }
        return;
    }
    if (p.command == "RECONNECT") {
        socket.abort();
        retry.start(1000);
        return;
    }
    auto source = channels.value(p.channel);
    if (!source) {
        return;
    }
    if (p.command == "PRIVMSG") {
        if (source->historyLoading) {
            source->liveBuffer << p.message;
            if (source->liveBuffer.size() > source->retentionCap()) {
                source->liveBuffer.removeFirst();
            }
        } else {
            source->publish(p.message);
        }
    } else if (p.command == "ROOMSTATE") {
        QString room = str(p.tags["room-id"]);
        if (!room.isEmpty() && source->room() != room) {
            source->header["channel_id"] = room;
            source->loadEmotes(room);
            source->loadBadges();
        }
    } else if (p.command == "CLEARMSG") {
        source->redact({{"message_id", p.tags["target-msg-id"]}});
    } else if (p.command == "CLEARCHAT") {
        QString user = str(p.tags["target-user-id"]);
        source->redact(user.isEmpty() ? QJsonObject{{"all", true}} : QJsonObject{{"user_id", user}});
    } else if (p.command == "NOTICE") {
        source->status(p.params.section(" :", 1));
    }
}

void TwitchConnection::close() {
    closing = true;
    retry.stop();
    heartbeat.stop();
    socket.abort();
}

Hub::Hub(Config *c, Network *n, Accounts *a, Images *i, Quota *q, QObject *p)
    : QObject(p), cfg(c), net(n), accounts(a), images(i), quota(q), twitch(this) {
    liveTimer.setInterval(60000);
    connect(&liveTimer, &QTimer::timeout, this, &Hub::pollLive);
    connect(accounts, &Accounts::ready, this, [this] {
        globalsLoading = false;
        globalsLoaded = 0;
        globalEmotes();
        if (!testMode) {
            liveTimer.start();
            pollLive();
        }
    });
}

Source *Hub::source(QString platform, QString id) {
    if (platform == "twitch") {
        id = id.toLower();
    }
    QString key = platform == "twitch" ? "twitch:" + id : id;
    if (sources.contains(key)) {
        return sources[key];
    }
    auto *s = new Source(this, platform, id);
    sources[key] = s;
    connect(s, &Source::changed, this, [this, s] { emit sourceChanged(s); });
    if (!testMode) {
        QTimer::singleShot(0, s, &Source::start);
    }
    if (!liveTimer.isActive() && !testMode) {
        liveTimer.start();
    }
    return s;
}

void Hub::remove(Source *s) {
    if (!s) {
        return;
    }
    sources.remove(s->key);
    s->stop();
    s->deleteLater();
}

void Hub::globalEmotes() {
    if (globalsLoading || testMode || QDateTime::currentSecsSinceEpoch() - globalsLoaded < 21600) {
        return;
    }
    globalsLoading = true;
    globalsLoaded = QDateTime::currentSecsSinceEpoch();
    QTimer::singleShot(30000, this, [this] { globalsLoading = false; });
    net->json(QUrl("https://api.betterttv.net/3/cached/emotes/global"), [this](QJsonObject d, QString e) {
        if (e.isEmpty()) {
            provider(nullptr, d["items"], "bttv", "global");
        }
    });
    net->json(QUrl("https://7tv.io/v3/emote-sets/global"), [this](QJsonObject d, QString e) {
        if (e.isEmpty()) {
            provider(nullptr, d["emotes"], "7tv", "global");
        }
    });
    accounts->helix("chat/emotes/global", [this](QJsonObject d, QString e) {
        if (e.isEmpty()) {
            provider(nullptr, d["data"], "twitch", "global");
        }
    });
}

void Hub::provider(Source *s, const QJsonValue &data, QString provider, QString scope) {
    Q_UNUSED(scope)
    QHash<QString, Span> found;
    for (const auto &v : data.toArray()) {
        auto o = v.toObject();
        Span e;
        e.code = provider == "7tv" ? str(o["name"]) : provider == "bttv" ? str(o["code"]) : str(o["name"]);
        if (provider == "twitch") {
            e.code = emoteName(e.code);
        }
        QString id = str(o["id"]);
        if (e.code.isEmpty() || id.isEmpty()) {
            continue;
        }
        if (provider == "bttv") {
            e.urls = {"https://cdn.betterttv.net/emote/" + id + "/3x",
                      "https://cdn.betterttv.net/emote/" + id + "/2x"};
        } else if (provider == "twitch") {
            e.urls = {"https://static-cdn.jtvnw.net/emoticons/v2/" + id + "/default/dark/3.0"};
        } else {
            auto host = at(o, {"data", "host"}).toObject();
            QString base = str(host["url"]);
            if (base.startsWith("//")) {
                base = "https:" + base;
            }
            auto files = host["files"].toArray();
            QVector<QJsonObject> sorted;
            for (const auto &f : files) {
                sorted << f.toObject();
            }
            std::sort(sorted.begin(), sorted.end(),
                      [](auto a, auto b) { return a["width"].toInt() > b["width"].toInt(); });
            for (const auto &f : sorted) {
                QString format = str(f["format"]).toUpper();
                if (format == "WEBP" || format == "GIF" || format == "PNG") {
                    e.urls << base + '/' + str(f["name"]);
                }
            }
        }
        found[e.code] = e;
    }
    if (s) {
        for (auto i = found.begin(); i != found.end(); ++i) {
            s->channelEmotes[i.key()] = i.value();
            s->emotes[i.key()] = i.value();
        }
        emit s->changed();
    } else {
        for (auto i = found.begin(); i != found.end(); ++i) {
            globals[i.key()] = i.value();
        }
        for (auto source : sources) {
            for (auto i = found.begin(); i != found.end(); ++i) {
                if (!source->channelEmotes.contains(i.key())) {
                    source->emotes[i.key()] = i.value();
                }
            }
            emit source->changed();
        }
    }
}

void Hub::youtubeChatId(Source *s, std::function<void(QString, QString)> done) {
    QString cached = str(s->header["live_chat_id"]);
    if (!cached.isEmpty()) {
        done(cached, {});
        return;
    }
    QPointer<Source> self(s);
    accounts->youtube("videos",
                      [self, done](QJsonObject d, QString e) {
                          if (!self) {
                              return;
                          }
                          auto item = first(d["items"].toArray()).toObject();
                          QString id = str(at(item, {"liveStreamingDetails", "activeLiveChatId"}));
                          if (e.isEmpty() && id.isEmpty()) {
                              e = "This broadcast has no active live chat.";
                          }
                          if (e.isEmpty()) {
                              self->header["live_chat_id"] = id;
                          }
                          done(id, e);
                      },
                      "GET", {{"part", "liveStreamingDetails"}, {"id", s->id}});
}

void Hub::send(Source *s, QString text, Message reply, JsonCallback done) {
    if (!reply.id.isEmpty()) {
        bool deleted = reply.deleted;
        for (const auto &m : s->history) {
            if (m.id == reply.id && m.deleted) {
                deleted = true;
            }
        }
        if (deleted) {
            done({}, "Cannot reply to a deleted message. Choose another reply target.");
            return;
        }
    }
    if (text.trimmed().isEmpty()) {
        done({}, "Enter a message first.");
        return;
    }
    int limit = s->platform == "twitch" ? 500 : 200;
    if (characterCount(text) > limit) {
        done({}, "This message exceeds the channel's character limit.");
        return;
    }
    QPointer<Source> self(s);
    if (s->platform == "twitch") {
        accounts->requireTwitch({"user:write:chat"}, [this, self, text, reply, done](QString, QString e) {
            if (!self) {
                return;
            }
            if (!e.isEmpty()) {
                done({}, e);
                return;
            }
            if (self->room().isEmpty()) {
                done({}, "Channel information is still loading. Try again shortly.");
                self->refreshHeader();
                return;
            }
            QJsonObject body{{"broadcaster_id", self->room()},
                             {"sender_id", accounts->twitch["user_id"]},
                             {"message", text}};
            if (!reply.id.isEmpty() && !reply.deleted) {
                body["reply_parent_message_id"] = reply.id;
            }
            accounts->helix(
                "chat/messages",
                [this, self, text, reply, done](QJsonObject d, QString error) {
                    if (!self) {
                        return;
                    }
                    auto result = first(d["data"].toArray()).toObject();
                    if (error.isEmpty() && !result["is_sent"].toBool()) {
                        error = str(at(result, {"drop_reason", "message"}));
                        if (error.isEmpty()) {
                            error = "Twitch did not accept the message.";
                        }
                    }
                    if (error.isEmpty()) {
                        Message m;
                        m.id = str(result["message_id"]);
                        m.name = str(accounts->twitch["login"]);
                        m.login = m.name;
                        m.uid = str(accounts->twitch["user_id"]);
                        m.text = text;
                        m.own = true;
                        m.stamp = QDateTime::currentDateTime();
                        if (!reply.id.isEmpty()) {
                            m.reply = {{"id", reply.id},
                                       {"name", reply.name},
                                       {"login", reply.login},
                                       {"text", reply.text},
                                       {"thread_id", reply.reply["thread_id"].toString(reply.id)}};
                        }
                        self->publish(m);
                    }
                    done(d, error);
                },
                "POST", {}, body);
        });
        return;
    }
    youtubeChatId(s, [this, self, text, done](QString chat, QString e) {
        if (!self) {
            return;
        }
        if (!e.isEmpty()) {
            done({}, e);
            return;
        }
        accounts->youtube(
            "liveChat/messages",
            [this, self, text, done](QJsonObject d, QString error) {
                if (!self) {
                    return;
                }
                if (error.isEmpty()) {
                    Message m;
                    m.id = str(d["id"]);
                    m.apiId = m.id;
                    m.name = str(accounts->google["channel_title"]);
                    m.login = m.name;
                    m.uid = str(accounts->google["channel_id"]);
                    m.text = text;
                    m.own = true;
                    m.stamp = QDateTime::currentDateTime();
                    self->publish(m);
                }
                done(d, error);
            },
            "POST", {{"part", "snippet"}},
            {{"snippet", QJsonObject{{"liveChatId", chat},
                                     {"type", "textMessageEvent"},
                                     {"textMessageDetails", QJsonObject{{"messageText", text}}}}}},
            20);
    });
}

void Hub::canModerate(Source *s, std::function<void(bool, QString)> done) {
    QString uid = str(accounts->twitch["user_id"]);
    if (s->room() == uid) {
        done(true, {});
        return;
    }
    QPointer<Source> safe(s);
    accounts->helix("moderation/channels",
                    [safe, done](QJsonObject d, QString e) {
                        if (!safe) {
                            done(false, "The channel was closed.");
                            return;
                        }
                        bool found = false;
                        for (const auto &v : d["data"].toArray()) {
                            if (str(v.toObject()["broadcaster_id"]) == safe->room()) {
                                found = true;
                            }
                        }
                        done(found, e);
                    },
                    "GET", {{"user_id", uid}, {"first", "100"}});
}

void Hub::youtubeMessageId(Source *s, Message m, std::function<void(QString, QString)> done) {
    if (!m.apiId.isEmpty() || m.own) {
        done(m.apiId.isEmpty() ? m.id : m.apiId, {});
        return;
    }
    youtubeChatId(s, [this, s, m, done](QString chat, QString e) {
        if (!e.isEmpty()) {
            done({}, e);
            return;
        }
        accounts->youtube(
            "liveChat/messages",
            [m, done](QJsonObject d, QString error) {
                QString id;
                for (const auto &v : d["items"].toArray()) {
                    auto o = v.toObject();
                    auto sn = o["snippet"].toObject();
                    auto t = QDateTime::fromString(str(sn["publishedAt"]), Qt::ISODate);
                    if (str(at(o, {"authorDetails", "channelId"})) == m.uid &&
                        str(at(sn, {"textMessageDetails", "messageText"})) == m.text &&
                        (!m.stamp.isValid() || qAbs(t.secsTo(m.stamp)) < 30)) {
                        id = str(o["id"]);
                        break;
                    }
                }
                if (error.isEmpty() && id.isEmpty()) {
                    error = "This message is no longer in YouTube's recent moderation window. Use YouTube "
                            "Studio.";
                }
                done(id, error);
            },
            "GET", {{"liveChatId", chat}, {"part", "snippet,authorDetails"}, {"maxResults", "200"}}, {}, 5);
    });
}

void Hub::moderate(Source *s, const Message &m, QString action, int duration, JsonCallback done) {
    QPointer<Source> self(s);
    auto complete = [self, m, action, done](QJsonObject d, QString e) {
        if (self && e.isEmpty() && action != "unban") {
            self->redact(action == "delete" ? QJsonObject{{"message_id", m.id}}
                                            : QJsonObject{{"user_id", m.uid}});
        }
        done(d, e);
    };
    if (s->platform == "twitch") {
        QSet<QString> scopes{"user:read:moderated_channels", action == "delete"
                                                                 ? "moderator:manage:chat_messages"
                                                                 : "moderator:manage:banned_users"};
        accounts->requireTwitch(scopes, [=, this](QString, QString e) {
            if (!self) {
                return;
            }
            if (!e.isEmpty()) {
                done({}, e);
                return;
            }
            canModerate(self, [=, this](bool allowed, QString error) {
                if (!self) {
                    return;
                }
                if (!error.isEmpty() || !allowed) {
                    done({}, error.isEmpty()
                                 ? "Your Twitch account is not the owner or a moderator of this channel."
                                 : error);
                    return;
                }
                QMap<QString, QString> params{{"broadcaster_id", self->room()},
                                              {"moderator_id", str(accounts->twitch["user_id"])}};
                if (action == "delete") {
                    if (m.uid == self->room()) {
                        done({}, "Twitch's API cannot delete broadcaster messages. Remove it on Twitch.");
                        return;
                    }
                    params["message_id"] = m.id;
                    accounts->helix("moderation/chat", complete, "DELETE", params);
                } else if (action == "unban") {
                    params["user_id"] = m.uid;
                    accounts->helix("moderation/bans", complete, "DELETE", params);
                } else {
                    QJsonObject data{{"user_id", m.uid}};
                    if (action == "timeout") {
                        data["duration"] = duration;
                    }
                    accounts->helix("moderation/bans", complete, "POST", params, {{"data", data}});
                }
            });
        });
        return;
    }
    if (action == "delete") {
        youtubeMessageId(s, m, [this, complete](QString id, QString e) {
            if (!e.isEmpty()) {
                complete({}, e);
                return;
            }
            accounts->youtube("liveChat/messages", complete, "DELETE", {{"id", id}}, {}, 20);
        });
        return;
    }
    if (action == "unban") {
        done({}, "Use YouTube Studio to remove an existing ban.");
        return;
    }
    youtubeChatId(s, [=, this](QString chat, QString e) {
        if (!e.isEmpty()) {
            done({}, e);
            return;
        }
        QJsonObject snippet{{"liveChatId", chat},
                            {"type", action == "timeout" ? "temporary" : "permanent"},
                            {"bannedUserDetails", QJsonObject{{"channelId", m.uid}}}};
        if (action == "timeout") {
            snippet["banDurationSeconds"] = duration;
        }
        accounts->youtube("liveChat/bans", complete, "POST", {{"part", "snippet"}}, {{"snippet", snippet}},
                          200);
    });
}

void Hub::editChannel(Source *s, QString title, QString categoryId, JsonCallback done) {
    if (s->platform == "twitch") {
        accounts->requireTwitch({"channel:manage:broadcast"}, [=, this](QString, QString e) {
            if (!e.isEmpty()) {
                done({}, e);
                return;
            }
            if (s->room() != str(accounts->twitch["user_id"])) {
                done({}, "Twitch requires the broadcaster's account to edit this channel through the API.");
                return;
            }
            QJsonObject body{{"title", title}};
            if (!categoryId.isEmpty()) {
                body["game_id"] = categoryId;
            }
            accounts->helix("channels", done, "PATCH", {{"broadcaster_id", s->room()}}, body);
        });
        return;
    }
    accounts->youtube("videos",
                      [=, this](QJsonObject d, QString e) {
                          if (!e.isEmpty()) {
                              done({}, e);
                              return;
                          }
                          auto sn = first(d["items"].toArray()).toObject()["snippet"].toObject();
                          if (sn.isEmpty()) {
                              done({}, "YouTube did not return this video's editable information.");
                              return;
                          }
                          sn["title"] = title;
                          accounts->youtube("videos", done, "PUT", {{"part", "snippet"}},
                                            {{"id", s->id}, {"snippet", sn}}, 50);
                      },
                      "GET", {{"part", "snippet"}, {"id", s->id}});
}

void Hub::profile(Source *s, Message m, JsonCallback done) {
    if (s->platform == "twitch") {
        accounts->helix(
            "users",
            [this, done](QJsonObject d, QString e) {
                if (!e.isEmpty()) {
                    done({}, e);
                    return;
                }
                auto user = first(d["data"].toArray()).toObject();
                accounts->helix("channels/followers",
                                [user, done](QJsonObject data, QString) mutable {
                                    if (data.contains("total")) {
                                        user["followers"] = data["total"];
                                    }
                                    done(user, {});
                                },
                                "GET", {{"broadcaster_id", str(user["id"])}});
            },
            "GET",
            m.uid.isEmpty() ? QMap<QString, QString>{{"login", m.login}}
                            : QMap<QString, QString>{{"id", m.uid}});
        return;
    }
    accounts->youtube("channels",
                      [done](QJsonObject d, QString e) { done(first(d["items"].toArray()).toObject(), e); },
                      "GET", {{"part", "snippet,statistics"}, {"id", m.uid}});
}

void Hub::savedStreams(QJsonObject saved, JsonCallback done) {
    QString cid = str(saved["channel_id"]);
    if (!cid.startsWith("UC")) {
        done({}, "This saved YouTube channel has no channel ID. Save it again from an open chat.");
        return;
    }
    net->bytes(
        QUrl("https://www.youtube.com/channel/" + cid + "/streams"),
        [=, this](QByteArray bytes, QString error) {
            if (!error.isEmpty()) {
                done({}, error);
                return;
            }
            auto initial = embeddedJson(QString::fromUtf8(bytes), "ytInitialData");
            QJsonArray streams;
            QSet<QString> seen;
            walk(initial, [&](const QJsonObject &node) {
                for (const auto &kind :
                     QStringList{"videoRenderer", "gridVideoRenderer", "lockupViewModel"}) {
                    auto item = node[kind].toObject();
                    QString id = str(item["videoId"]);
                    if (id.isEmpty()) {
                        id = str(item["contentId"]);
                    }
                    bool live = false;
                    walk(item, [&](const auto &child) {
                        live |= str(child["style"]) == "LIVE" || str(child["text"]) == "LIVE" ||
                                str(child["label"]) == "LIVE";
                    });
                    if (live && videoId(id) == id && !seen.contains(id)) {
                        seen.insert(id);
                        QString title = textOf(item["title"]);
                        streams.append(QJsonObject{{"id", id}, {"title", title.isEmpty() ? id : title}});
                    }
                }
            });
            if (!streams.isEmpty()) {
                done({{"streams", streams}}, {});
                return;
            }
            net->bytes(
                QUrl("https://www.youtube.com/channel/" + cid + "/live"),
                [done](QByteArray raw, QString e) {
                    auto d = embeddedJson(QString::fromUtf8(raw), "ytInitialPlayerResponse")["videoDetails"]
                                 .toObject();
                    QJsonArray items;
                    if (d["isLive"].toBool()) {
                        items.append(QJsonObject{{"id", d["videoId"]}, {"title", d["title"]}});
                    }
                    done({{"streams", items}}, e);
                },
                "GET", {}, {}, 16 * 1024 * 1024);
        },
        "GET", {}, {}, 16 * 1024 * 1024);
}

void Hub::sound(bool live, bool force) {
    auto alerts = cfg->feature("alerts");
    if (!force && !alerts["enabled"].toBool(true)) {
        return;
    }
    emit alertPlayed(live);
    if (testMode) {
        return;
    }
    QString file = alerts[live ? "live_sound_file" : "sound_file"].toString();
    if (file.isEmpty()) {
        file = soundFiles.path() + "/ChatternoPing.wav";
        if (!QFileInfo::exists(file)) {
            QFile::copy(":/ChatternoPing.wav", file);
        }
    }
#ifdef Q_OS_WIN
    PlaySoundW(reinterpret_cast<LPCWSTR>(file.utf16()), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
#else
    for (const auto &tool : QStringList{"pw-play", "paplay", "aplay"}) {
        auto path = QStandardPaths::findExecutable(tool);
        if (!path.isEmpty()) {
            QProcess::startDetached(path, {file});
            return;
        }
    }
    QApplication::beep();
#endif
}

void Hub::messageAlert(Source *s, const Message &m) {
    Q_UNUSED(s)
    if (m.history || m.system || m.own) {
        return;
    }
    auto alerts = cfg->feature("alerts");
    if (!alerts["mention_sound"].toBool(true)) {
        return;
    }
    bool mention = false;
    highlight(m, *cfg, accounts->names(), &mention);
    if (mention) {
        sound();
    }
}

void Hub::liveChange(Source *s, bool live) {
    if (s->platform != "twitch") {
        return;
    }
    liveChange(s->id, s->name(), live, true);
}

void Hub::liveChange(QString login, QString name, bool live, bool opened) {
    login = login.toLower();
    bool first = !liveState.contains(login), old = liveState.value(login);
    liveState[login] = live;
    if (first || !live || old) {
        return;
    }
    auto a = cfg->feature("alerts");
    bool watch = opened && a["all_twitch"].toBool(true);
    for (const auto &v : a["excluded"].toArray()) {
        if (v.toString().compare(login, Qt::CaseInsensitive) == 0) {
            watch = false;
        }
    }
    for (const auto &v : a["watch"].toArray()) {
        auto item = v.toObject();
        if (item["platform"].toString("twitch") == "twitch" &&
            str(item["id"]).compare(login, Qt::CaseInsensitive) == 0) {
            watch = true;
        }
    }
    if (watch) {
        sound(true);
        emit notice(name + " · " + cfg->T("Live"));
    }
}

void Hub::pollLive() {
    if (liveRequest || str(accounts->twitch["access_token"]).isEmpty()) {
        return;
    }
    QSet<QString> logins;
    for (auto s : sources) {
        if (s->platform == "twitch") {
            logins.insert(s->id);
        }
    }
    for (auto v : cfg->feature("alerts")["watch"].toArray()) {
        auto item = v.toObject();
        auto login = twitchLogin('@' + str(item["id"]));
        if (item["platform"].toString("twitch") == "twitch" && !login.isEmpty()) {
            logins.insert(login);
        }
    }
    if (logins.isEmpty()) {
        return;
    }
    liveRequest = true;
    QStringList list = logins.values();
    list.sort();
    auto remaining = QSharedPointer<int>::create((list.size() + 99) / 100);
    for (int begin = 0; begin < list.size(); begin += 100) {
        auto batch = list.mid(begin, 100);
        QUrlQuery query;
        for (auto login : batch) {
            query.addQueryItem("user_login", login);
        }
        accounts->helix("streams?" + query.query(QUrl::FullyEncoded), [this, batch, remaining](
                                                                          QJsonObject data, QString error) {
            if (error.isEmpty()) {
                QHash<QString, QJsonObject> streams;
                for (auto value : data["data"].toArray()) {
                    auto row = value.toObject();
                    streams[str(row["user_login"]).toLower()] = row;
                }
                for (auto login : batch) {
                    auto source = sources.value("twitch:" + login);
                    auto stream = streams.value(login);
                    bool live = !stream.isEmpty();
                    if (source) {
                        source->header["live"] = live;
                        source->header["viewers"] = stream["viewer_count"].toInt();
                        if (live) {
                            source->header["title"] = stream["title"];
                            source->header["category"] = stream["game_name"];
                        }
                        emit source->changed();
                    }
                    liveChange(login, source ? source->name() : login, live, source != nullptr);
                }
            }
            if (--*remaining == 0) {
                liveRequest = false;
            }
        });
    }
}
} // namespace yc
