#include "chat.hpp"
#include <algorithm>
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
    QString avatar;
    auto data = embeddedJson(html, "ytInitialData");
    walk(data, [&](const QJsonObject &node) {
        const auto owner = node["videoOwnerRenderer"].toObject();
        if (!owner.isEmpty() && avatar.isEmpty()) {
            const auto ownerId = str(at(owner, {"navigationEndpoint", "browseEndpoint", "browseId"}));
            if (ownerId.isEmpty() || ownerId == str(d["channelId"]))
                avatar = imageUrls(owner["thumbnail"]).value(0);
        }
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
            {"avatar", avatar},
            {"thumbnail", imageUrls(d["thumbnail"]).value(0, "https://i.ytimg.com/vi/" + id + "/hqdefault.jpg")},
            {"title", d["title"]},
            {"live", live},
            {"started_at", at(p, {"microformat", "playerMicroformatRenderer", "liveBroadcastDetails", "startTimestamp"})},
            {"viewers", double(viewers)}};
}

Source::Source(Hub *h, QString p, QString value, bool vod)
    : QObject(h), hub(h), platform(std::move(p)), id(std::move(value)), isVod(vod) {
    key = isVod ? "vod:" + platform + ':' + id : (platform == "youtube" ? id : platform + ":" + id);
    auto settings = hub->cfg->data["chat_settings"].toObject()[key].toObject();
    interval = qMax(4.0, settings["web_poll_interval_seconds"].toDouble(4));
    apiFallbackEnabled = settings["youtube_api_fallback"].toBool(false);
    apiInterval = qBound(18.25, settings["youtube_api_interval_seconds"].toDouble(18.25), 600.0);
    poll.setSingleShot(true);
    youtubeDisplayTimer.setTimerType(Qt::PreciseTimer);
    connect(&youtubeDisplayTimer, &QTimer::timeout, this, [this] {
        if (youtubePending.isEmpty() || closed) { youtubeDisplayTimer.stop(); return; }
        for (int i = 0; i < youtubeDisplayChunk && !youtubePending.isEmpty(); ++i)
            publish(youtubePending.dequeue());
        if (youtubePending.isEmpty()) youtubeDisplayTimer.stop();
    });
    badgeRetry.setSingleShot(true);
    badgeRetry.setInterval(30000);
    connect(&badgeRetry, &QTimer::timeout, this, &Source::loadBadges);
    historyRetry.setSingleShot(true);
    connect(&historyRetry, &QTimer::timeout, this, &Source::loadHistory);
    metadataTimer.setInterval(300000);
    decorationTimer.setInterval(60000);
    connect(&decorationTimer, &QTimer::timeout, this, &Source::refreshDecorations);
    pollResultTimer.setSingleShot(true);
    connect(&pollResultTimer, &QTimer::timeout, this, [this] {
        if (activePoll.value("ended").toBool()) setDecorations(pinned, {});
    });
    connect(hub->accounts, &Accounts::changed, this, [this] {
        if (isVod) return;
        if (platform == "twitch" && !ownTwitchUserId.isEmpty() &&
            ownTwitchUserId != str(hub->accounts->twitch["user_id"])) resetTwitchUserState();
        if (platform == "youtube" && !youtubeRoleUserId.isEmpty() &&
            youtubeRoleUserId != str(hub->accounts->google["channel_id"])) {
            youtubeRoleUserId.clear();
            youtubeRoleStamp = {};
            youtubeModeratorKnown = youtubeModerator = false;
            emit permissionsChanged();
        }
        if (platform == "twitch" && !hub->testMode && !closed) refreshDecorations();
        if (platform == "kick" && !hub->testMode && !closed) kickCheckRole();
    });
    connect(&poll, &QTimer::timeout, this, [this] {
        if (platform == "kick") {
            kickPoll();
        } else if (apiActive) {
            youtubeApiPoll();
        } else if (continuation.isEmpty()) {
            youtubeConnect();
        } else {
            youtubePoll();
        }
    });
    connect(&metadataTimer, &QTimer::timeout, this, &Source::refreshHeader);
    kickSocket.setMaxAllowedIncomingFrameSize(2 * 1024 * 1024);
    kickSocket.setMaxAllowedIncomingMessageSize(2 * 1024 * 1024);
    kickSocketRetry.setSingleShot(true);
    kickSocketWatchdog.setSingleShot(true);
    kickSocketRenew.setSingleShot(true);
    kickModesTimer.setSingleShot(true);
    connect(&kickModesTimer, &QTimer::timeout, this, &Source::kickRefreshChatModes);
    connect(&kickSocketRetry, &QTimer::timeout, this, &Source::kickStartSocket);
    connect(&kickSocketWatchdog, &QTimer::timeout, &kickSocket, &QWebSocket::abort);
    connect(&kickSocketRenew, &QTimer::timeout, &kickSocket, &QWebSocket::abort);
    connect(&kickSocket, &QWebSocket::connected, this, [this] {
        QJsonObject command{{"id", 1}, {"connect", QJsonObject{{"token", kickSocketToken}, {"name", "yt-chatterino"}}}};
        kickSocketToken.clear();
        kickSocket.sendTextMessage(QString::fromUtf8(QJsonDocument(command).toJson(QJsonDocument::Compact)));
        kickSocketWatchdog.start(65000);
    });
    connect(&kickSocket, &QWebSocket::textMessageReceived, this, &Source::kickSocketMessage);
    connect(&kickSocket, &QWebSocket::disconnected, this, &Source::kickSocketFailed);
    connect(&kickSocket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { kickSocketFailed(); });
}

QString Source::name() const {
    QString result = platform == "twitch" ? str(header["channel"]) : str(header["channel"]);
    return result.isEmpty() ? id : result;
}

QString Source::room() const {
    return str(header["channel_id"]);
}

QJsonObject Source::spec() const {
    if (isVod) return {{"platform", platform}, {"id", id}, {"vod", true},
                       {"url", vodUrl.toString()}, {"channel", header["channel"]}, {"title", header["title"]}};
    return {{"platform", platform}, {"id", id}, {"paused", paused}};
}

void Source::start() {
    if (isVod) return;
    if (closed || hub->testMode) {
        return;
    }
    if (platform == "twitch") {
        hub->twitch.join(this);
        refreshHeader();
        loadHistory();
        loadEmotes();
        loadBadges();
    } else if (platform == "kick") {
        kickConnect();
    } else {
        connectionStatus("Connecting to YouTube…");
        youtubeConnect();
    }
    metadataTimer.start();
    if (platform == "twitch") decorationTimer.start();
}

void Source::stop() {
    ++generation;
    closed = true;
    youtubeDisplayTimer.stop();
    youtubePending.clear();
    poll.stop();
    metadataTimer.stop();
    badgeRetry.stop();
    historyRetry.stop();
    decorationTimer.stop();
    pollResultTimer.stop();
    kickSocketRetry.stop(); kickSocketWatchdog.stop(); kickSocketRenew.stop();
    kickModesTimer.stop(); kickModesPending = false;
    kickSocket.abort();
    if (platform == "twitch" && !isVod) {
        hub->twitch.part(this);
    }
}

void Source::reconnect() {
    if (isVod) return;
    if (closed) {
        return;
    }
    flushYoutubeMessages();
    ++generation;
    chatModes = {}; chatModesKnown = false; youtubeModeNotice.clear();
    emit chatModesChanged();
    requesting = false;
    historyLoading = false;
    historyRetry.stop();
    decorationPending = pollDecorationPending = predictionDecorationPending = false;
    setDecorations({}, {});
    setPrediction({});
    emoteLoaded.clear();
    globalBadgesLoaded = channelBadgesLoaded = 0;
    globalBadgesPending = channelBadgesPending = false;
    badgeRetry.stop();
    continuation.clear();
    retryMs = 5000;
    if (platform == "twitch") {
        hub->twitch.reconnect();
        refreshHeader();
        if (!historyLoaded) loadHistory();
    } else if (platform == "kick") {
        poll.stop();
        kickModesTimer.stop(); kickModesPending = false; kickModesRetryMs = 60000;
        kickSocketRetry.stop(); kickSocketWatchdog.stop(); kickSocketRenew.stop();
        kickSocketStarting = false; kickSocketActive = false; kickSocket.abort();
        kickConnecting = false;
        kickThumbnailPending = false;
        kickThumbnailRetryAt = 0;
        kickRetryMs = 5000;
        kickConnect();
    } else {
        poll.stop();
        apiPageToken.clear();
        if (apiActive && apiFallbackEnabled) {
            youtubeApiConnect();
        } else {
            youtubeConnect();
        }
    }
}

void Source::setPaused(bool value) {
    if (platform != "youtube") {
        return;
    }
    paused = value;
    if (paused) {
        flushYoutubeMessages();
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
    m.source = key;
    if (platform == "kick" && !kickSubscriberBadges.isEmpty()) {
        for (auto &badge : m.badges) {
            if (badge.role != "subscriber" || !badge.urls.isEmpty()) continue;
            const int months = badge.version.toInt();
            auto tier = kickSubscriberBadges.upperBound(months);
            if (tier != kickSubscriberBadges.cbegin()) { --tier; badge.urls << tier.value(); }
        }
    }
    const auto googleId = str(hub->accounts->google["channel_id"]);
    if (!isVod && platform == "youtube" && !googleId.isEmpty() && m.uid == googleId && !m.system &&
        (!m.own || !m.badges.isEmpty()) &&
        (!youtubeRoleStamp.isValid() || !m.stamp.isValid() || m.stamp >= youtubeRoleStamp)) {
        const auto userRoles = roles(m);
        const bool moderator = userRoles.contains("owner") || userRoles.contains("moderator");
        const bool changed = !youtubeModeratorKnown || youtubeModerator != moderator ||
                             youtubeRoleUserId != googleId;
        youtubeRoleUserId = googleId;
        youtubeRoleStamp = m.stamp;
        youtubeModeratorKnown = true;
        youtubeModerator = moderator;
        if (changed) emit permissionsChanged();
    }
    if (platform == "youtube" && !m.uid.isEmpty() && m.uid == room() &&
        str(header["avatar"]).isEmpty() && !m.avatar.isEmpty()) {
        header["avatar"] = m.avatar;
        emit changed();
    }
    if (!m.system && !m.id.isEmpty() && seen.contains(m.id)) {
        for (auto &old : history) {
            if (old.id == m.id) {
                if (old.own && !m.own) {
                    m.seq = old.seq;
                    m.own = true;
                    m.deleted = m.deleted || old.deleted;
                    if (old.deleted) {
                        m.deletionReason = old.deletionReason;
                        m.deletionDuration = old.deletionDuration;
                    }
                    old = m;
                    emit changed();
                } else if (m.own && !old.own) {
                    old.own = true;
                    emit changed();
                }
                break;
            }
        }
        return;
    }
    if (!m.id.isEmpty()) {
        seen.insert(m.id);
        seenOrder.enqueue(m.id);
    }
    if (!isVod && !m.system && platform == "youtube" && !m.own) {
        for (int i = history.size() - 1; i >= qMax(0, history.size() - 128); --i) {
            auto &old = history[i];
            if (old.own && old.webId.isEmpty() && old.uid == m.uid && old.text == m.text &&
                qAbs(old.stamp.secsTo(m.stamp)) < 60) {
                old.webId = m.id;
                old.badges = m.badges;
                old.emotes = m.emotes;
                old.mentions = m.mentions;
                emit changed();
                return;
            }
        }
    }
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

void Source::connectionStatus(const QString &text) {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (connectionNotices.contains(text) && now - connectionNotices.value(text) < 60000) return;
    connectionNotices[text] = now;
    status(text);
}

void Source::resetTwitchUserState() {
    ownTwitchBadges.clear();
    ownTwitchUserId.clear();
    ownTwitchDisplayName.clear();
    ownTwitchColor.clear();
    ownTwitchBadgeInfo.clear();
    const bool changed = twitchModeratorKnown || twitchModerator;
    twitchModeratorKnown = twitchModerator = false;
    if (changed) emit permissionsChanged();
}

void Source::updateTwitchUserState(const QJsonObject &tags) {
    ownTwitchUserId = str(hub->accounts->twitch["user_id"]);
    if (tags.contains("display-name")) ownTwitchDisplayName = str(tags["display-name"]);
    if (tags.contains("color")) ownTwitchColor = str(tags["color"]);
    if (tags.contains("badge-info")) ownTwitchBadgeInfo = str(tags["badge-info"]);
    if (tags.contains("badges")) ownTwitchBadges = parseTwitchBadges(str(tags["badges"]));
    const bool known = tags.contains("mod") || tags.contains("badges");
    bool moderator = str(tags["mod"]) == "1";
    for (const auto &badge : ownTwitchBadges)
        moderator |= badge.role == "moderator" || badge.role == "owner";
    if (known != twitchModeratorKnown || moderator != twitchModerator) {
        twitchModeratorKnown = known;
        twitchModerator = moderator;
        emit permissionsChanged();
    }
    bool updated = false;
    if (!ownTwitchUserId.isEmpty()) for (auto &message : history) {
        if (message.own && message.uid == ownTwitchUserId && message.badges.isEmpty()) {
            const bool differs = !ownTwitchBadges.isEmpty() ||
                                 (!ownTwitchBadgeInfo.isEmpty() && message.badgeInfo != ownTwitchBadgeInfo) ||
                                 (!ownTwitchColor.isEmpty() && message.color != ownTwitchColor) ||
                                 (!ownTwitchDisplayName.isEmpty() && message.name != ownTwitchDisplayName);
            if (!differs) continue;
            message.badges = ownTwitchBadges;
            message.badgeInfo = ownTwitchBadgeInfo;
            if (!ownTwitchColor.isEmpty()) message.color = ownTwitchColor;
            if (!ownTwitchDisplayName.isEmpty()) message.name = ownTwitchDisplayName;
            updated = true;
        }
    }
    if (updated) emit changed();
}

void Source::updateChatModes(const QJsonObject &patch, bool known) {
    auto next = chatModes;
    for (auto it = patch.begin(); it != patch.end(); ++it) next[it.key()] = it.value();
    if (next == chatModes && chatModesKnown == known) return;
    chatModes = next;
    chatModesKnown = known;
    emit chatModesChanged();
}

static QString modeDuration(int minutes, const Config *cfg) {
    QStringList duration;
    if (minutes >= 1440) { duration << QString::number(minutes / 1440) + " d"; minutes %= 1440; }
    if (minutes >= 60) { duration << QString::number(minutes / 60) + " h"; minutes %= 60; }
    if (minutes) duration << QString::number(minutes) + " " + cfg->T("min");
    return duration.join(" ");
}

QString Source::chatModeSummary() const {
    if (platform == "youtube") {
        return youtubeModeNotice.isEmpty() ? hub->cfg->T("Chat modes unavailable")
            : hub->cfg->T("Last YouTube mode notice") + ": " + youtubeModeNotice;
    }
    if (!chatModesKnown) return hub->cfg->T("Chat modes unavailable");
    QStringList active;
    if (chatModes["subscribers"].toBool()) active << hub->cfg->T("Subscribers only");
    if (chatModes["followers"].toBool()) {
        const int minutes = chatModes["follow_minutes"].toInt();
        auto label = hub->cfg->T("Followers only");
        if (minutes > 0) label += " (" + modeDuration(minutes, hub->cfg) + ")";
        active << label;
    }
    if (chatModes["emotes"].toBool()) active << hub->cfg->T("Emotes only");
    if (chatModes["account_age"].toBool()) {
        const int minutes = chatModes["account_age_minutes"].toInt();
        auto label = hub->cfg->T("Account age restriction");
        if (minutes > 0) label += " (" + modeDuration(minutes, hub->cfg) + ")";
        active << label;
    }
    if (chatModes["unique"].toBool()) active << hub->cfg->T("Unique messages");
    if (chatModes["slow"].toBool()) {
        const int seconds = chatModes["slow_seconds"].toInt();
        auto label = hub->cfg->T("Slow mode");
        if (seconds > 0) label += QString(" (%1 %2)").arg(seconds).arg(hub->cfg->T("sec"));
        active << label;
    }
    return active.isEmpty() ? hub->cfg->T("No known restrictions") : active.join(" · ");
}

void Source::youtubeModeNotices(const QJsonValue &data) {
    QString latest;
    walk(data, [&](const QJsonObject &o) {
        const auto notice = o.value("liveChatModeChangeMessageRenderer").toObject();
        if (notice.isEmpty()) return;
        const auto icon = str(at(notice, {"icon", "iconType"}));
        if (icon != "SLOW_MODE" && icon != "TAB_SUBSCRIPTIONS" && icon != "SPONSORSHIP" &&
            icon != "MEMBERSHIP") return;
        const auto title = textOf(notice["text"]), detail = textOf(notice["subtext"]);
        if (!title.isEmpty()) latest = title + (detail.isEmpty() ? QString() : " · " + detail);
    });
    if (!latest.isEmpty() && latest != youtubeModeNotice) {
        youtubeModeNotice = latest.left(400);
        emit chatModesChanged();
    }
}

void Source::redact(const QJsonObject &target) {
    if (platform == "youtube" && !youtubePending.isEmpty()) flushYoutubeMessages();
    const auto userLogin = stripAt(str(target["user_login"]));
    QString reason = str(target["reason"]);
    if (reason.isEmpty()) reason = target["all"].toBool() ? "clear"
                                   : !str(target["message_id"]).isEmpty() ? "deleted" : "removed";
    const qint64 duration = qMax<qint64>(0, target["duration"].toInteger());
    for (auto &m : history) {
        bool match = target["all"].toBool() ||
                     (!str(target["message_id"]).isEmpty() &&
                      (str(target["message_id"]) == m.id || str(target["message_id"]) == m.apiId ||
                       str(target["message_id"]) == m.webId)) ||
                     (!str(target["user_id"]).isEmpty() && str(target["user_id"]) == m.uid) ||
                     (!userLogin.isEmpty() && stripAt(m.login) == userLogin);
        if (match && !m.system) {
            if (!m.deleted || m.deletionReason.isEmpty()) {
                m.deletionReason = reason;
                m.deletionDuration = duration;
            }
            m.deleted = true;
        }
        if (!m.reply.isEmpty() &&
            ((!str(target["message_id"]).isEmpty() && str(m.reply["id"]) == str(target["message_id"])) ||
             (!str(target["user_id"]).isEmpty() && str(m.reply["user_id"]) == str(target["user_id"])) ||
             (!userLogin.isEmpty() && stripAt(str(m.reply["login"])) == userLogin) ||
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
    if (platform == "kick") {
        kickConnect();
        return;
    }
    if (platform == "youtube") {
        if (apiActive || requesting || hub->youtubeCooldownRemaining() > 0) {
            return;
        }
        hub->net->bytes(Network::query("https://www.youtube.com/watch", {{"v", id}}),
                        [self](QByteArray b, QString e) {
                            if (!self || self->closed || !e.isEmpty()) {
                                return;
                            }
                            QString error;
                            auto d = metadata(QString::fromUtf8(b), self->id, &error);
                            if (error.isEmpty()) {
                                if (str(d["avatar"]).isEmpty()) d["avatar"] = self->header["avatar"];
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
            const bool roomChanged = self->room() != uid;
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
                                           self->header["started_at"] =
                                               live ? items[0].toObject()["started_at"] : QJsonValue();
                                           QString thumbnail = live ? str(items[0].toObject()["thumbnail_url"]) : QString();
                                           thumbnail.replace("{width}", "320").replace("{height}", "180");
                                           self->header["thumbnail"] = thumbnail;
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
            if (roomChanged) self->refreshDecorations();
        },
        "GET", {{"login", id}});
}

void Source::setDecorations(QJsonObject pin, QJsonObject pollState) {
    if (pinned == pin && activePoll == pollState) return;
    pinned = std::move(pin);
    activePoll = std::move(pollState);
    emit decorationsChanged();
}

void Source::setPrediction(QJsonObject prediction) {
    if (activePrediction == prediction) return;
    activePrediction = std::move(prediction);
    emit decorationsChanged();
}

static bool recentlyEnded(const QJsonObject &item, int seconds = 180) {
    const auto end = QDateTime::fromString(str(item["ended_at"]), Qt::ISODateWithMs);
    const auto fallback = QDateTime::fromString(str(item["ended_at"]), Qt::ISODate);
    const auto when = end.isValid() ? end : fallback;
    const auto age = when.isValid() ? when.secsTo(QDateTime::currentDateTimeUtc()) : -1;
    return age >= 0 && age <= seconds;
}

void Source::refreshDecorations() {
    if (closed || hub->testMode || platform != "twitch" || room().isEmpty()) return;
    auto *accounts = hub->accounts;
    const QString uid = str(accounts->twitch["user_id"]), broadcaster = room();
    const bool pinsAllowed = accounts->twitchAllows({"moderator:manage:chat_messages"}) ||
                             accounts->twitchAllows({"moderator:read:chat_messages"});
    const bool pollsAllowed = uid == broadcaster && (accounts->twitchAllows({"channel:read:polls"}) ||
                                                       accounts->twitchAllows({"channel:manage:polls"}));
    const bool predictionsAllowed = uid == broadcaster && (accounts->twitchAllows({"channel:read:predictions"}) ||
                                                             accounts->twitchAllows({"channel:manage:predictions"}));
    decorationTimer.setInterval((pollsAllowed || predictionsAllowed) ? 15000 : 60000);
    const QString signature = uid + ":" + broadcaster + ":" + QString::number(pinsAllowed) + ":" +
                              QString::number(pollsAllowed) + ":" + QString::number(predictionsAllowed);
    if (signature != decorationSignature) {
        decorationSignature = signature;
        ++decorationGeneration;
        decorationPending = pollDecorationPending = predictionDecorationPending = false;
        pinRetryAfter = 0;
        setDecorations({}, {});
        setPrediction({});
    }
    if (uid.isEmpty()) return;
    if (!pinsAllowed && !pinned.isEmpty()) setDecorations({}, activePoll);
    if (!pollsAllowed && !activePoll.isEmpty()) setDecorations(pinned, {});
    if (!predictionsAllowed && !activePrediction.isEmpty()) setPrediction({});
    const auto epoch = generation;
    const auto decorationEpoch = decorationGeneration;
    QPointer<Source> self(this);
    if (pinsAllowed && !decorationPending && QDateTime::currentSecsSinceEpoch() >= pinRetryAfter) {
        decorationPending = true;
        accounts->helix("chat/pins", [self, epoch, decorationEpoch](QJsonObject data, QString error) {
            if (!self || self->closed || self->generation != epoch ||
                self->decorationGeneration != decorationEpoch) return;
            self->decorationPending = false;
            if (!error.isEmpty()) {
                if (error.contains("403")) self->pinRetryAfter = QDateTime::currentSecsSinceEpoch() + 300;
                self->setDecorations({}, self->activePoll);
                return;
            }
            const auto item = first(data["data"].toArray()).toObject();
            QJsonObject pin;
            const QString body = str(at(item, {"message", "text"}));
            if (!body.isEmpty()) pin = {{"id", item["message_id"]}, {"name", item["sender_user_name"]},
                                       {"text", body}};
            self->setDecorations(pin, self->activePoll);
        }, "GET", {{"broadcaster_id", broadcaster}, {"moderator_id", uid}});
    }
    if (pollsAllowed && !pollDecorationPending) {
        pollDecorationPending = true;
        accounts->helix("polls", [self, epoch, decorationEpoch](QJsonObject data, QString error) {
            if (!self || self->closed || self->generation != epoch ||
                self->decorationGeneration != decorationEpoch) return;
            self->pollDecorationPending = false;
            QJsonObject state;
            if (error.isEmpty()) for (const auto &value : data["data"].toArray()) {
                const auto item = value.toObject();
                const auto status = str(item["status"]);
                if (status != "ACTIVE" && !((status == "COMPLETED" || status == "TERMINATED") && recentlyEnded(item))) continue;
                QStringList choices;
                for (const auto &choice : item["choices"].toArray()) {
                    auto option = choice.toObject();
                    choices << str(option["title"]) + " (" + QString::number(option["votes"].toInt()) + ")";
                }
                state = {{"text", item["title"]}, {"options", choices.join(" · ")}, {"ended", status != "ACTIVE"}};
                break;
            }
            self->setDecorations(self->pinned, state);
        }, "GET", {{"broadcaster_id", broadcaster}, {"first", "20"}});
    }
    if (predictionsAllowed && !predictionDecorationPending) {
        predictionDecorationPending = true;
        accounts->helix("predictions", [self, epoch, decorationEpoch](QJsonObject data, QString error) {
            if (!self || self->closed || self->generation != epoch || self->decorationGeneration != decorationEpoch) return;
            self->predictionDecorationPending = false;
            QJsonObject state;
            if (error.isEmpty()) for (const auto &value : data["data"].toArray()) {
                const auto item = value.toObject();
                const auto status = str(item["status"]);
                if (status != "ACTIVE" && status != "LOCKED" &&
                    !((status == "RESOLVED" || status == "CANCELED") && recentlyEnded(item))) continue;
                const auto winner = str(item["winning_outcome_id"]);
                QStringList choices;
                for (const auto &value : item["outcomes"].toArray()) {
                    const auto outcome = value.toObject();
                    QString choice = str(outcome["title"]);
                    if (!winner.isEmpty() && winner == str(outcome["id"])) choice = "✓ " + choice;
                    if (outcome.contains("channel_points"))
                        choice += " (" + QString::number(outcome["channel_points"].toVariant().toLongLong()) + " " +
                                  self->hub->cfg->T("points") + ")";
                    if (!choice.isEmpty()) choices << choice;
                }
                state = {{"text", item["title"]}, {"options", choices.join(" · ")},
                         {"ended", status == "RESOLVED" || status == "CANCELED"}, {"status", status}};
                break;
            }
            self->setPrediction(state);
        }, "GET", {{"broadcaster_id", broadcaster}, {"first", "20"}});
    }
}

void Source::youtubeApiPollState(const QJsonObject &data) {
    const auto metadata = at(data, {"activePollItem", "snippet", "pollDetails", "metadata"}).toObject();
    QJsonObject state;
    const QString question = str(metadata["questionText"]);
    if (question.isEmpty() && !activePoll.isEmpty()) {
        if (activePoll.value("ended").toBool() && pollResultTimer.isActive()) state = activePoll;
        else if (!activePoll.value("ended").toBool()) {
            state = activePoll;
            state["ended"] = true;
            state["last_observed"] = true;
            pollResultTimer.start(180000);
        }
    }
    if (!question.isEmpty()) {
        QStringList choices;
        for (const auto &value : metadata["options"].toArray()) {
            const auto option = value.toObject();
            QString choice = str(option["optionText"]);
            const QString tally = str(option["tally"]);
            if (!tally.isEmpty()) choice += " (" + tally + ")";
            if (!choice.isEmpty()) choices << choice;
        }
        const bool ended = str(metadata["status"]).compare("closed", Qt::CaseInsensitive) == 0;
        state = {{"text", question}, {"options", choices.join(" · ")}, {"ended", ended}};
        if (ended) pollResultTimer.start(180000); else pollResultTimer.stop();
    }
    setDecorations(pinned, state);
}

void Source::youtubeBannerActions(const QJsonArray &actions) {
    auto pin = pinned, pollState = activePoll;
    for (const auto &value : actions) {
        const auto action = value.toObject();
        const auto add = action["addBannerToLiveChatCommand"].toObject();
        const auto banner = at(add, {"bannerRenderer", "liveChatBannerRenderer"}).toObject();
        if (!banner.isEmpty()) {
            const auto content = banner["contents"].toObject();
            for (auto i = content.begin(); i != content.end(); ++i) {
                if (!i.key().contains("MessageRenderer")) continue;
                const auto parsed = parseYouTube(i.value().toObject(), i.key());
                if (!parsed.text.isEmpty()) {
                    pin = {{"id", str(banner["actionId"])}, {"name", parsed.name}, {"text", parsed.text}};
                    break;
                }
            }
        }
        const auto remove = action["removeBannerForLiveChatCommand"].toObject();
        if (!remove.isEmpty() && !str(pin["id"]).isEmpty() &&
            str(remove["targetActionId"]) == str(pin["id"])) pin = {};
        const auto panel = action["showLiveChatActionPanelAction"].toObject();
        if (!panel.isEmpty()) {
            const auto poll = at(panel, {"panelToShow", "liveChatActionPanelRenderer", "contents", "pollRenderer"}).toObject();
            QString question = textOf(poll["question"]);
            if (question.isEmpty()) question = textOf(at(poll, {"header", "pollHeaderRenderer", "title"}));
            QStringList choices;
            QJsonArray choiceNames;
            for (const auto &choice : poll["choices"].toArray()) {
                auto text = textOf(choice.toObject()["text"]);
                if (!text.isEmpty()) { choices << text; choiceNames.append(text); }
            }
            if (!question.isEmpty() || !choices.isEmpty()) {
                pollResultTimer.stop();
                pollState = {{"text", question}, {"options", choices.join(" · ")}, {"choices", choiceNames}};
            }
        }
        if (action.contains("closeLiveChatActionPanelAction") && !pollState.isEmpty()) {
            pollState["ended"] = true;
            pollState["last_observed"] = true;
            pollResultTimer.start(180000);
        }
        const auto engagement = at(action, {"addChatItemAction", "item", "liveChatViewerEngagementMessageRenderer"}).toObject();
        if (str(at(engagement, {"icon", "iconType"})) == "POLL") {
            const QString summary = textOf(engagement["message"]);
            if (!summary.isEmpty()) {
                const QString question = str(pollState["text"]);
                pollState = {{"text", question.isEmpty() ? hub->cfg->T("Poll result") : question},
                             {"options", summary}, {"ended", true}};
                pollResultTimer.start(180000);
            }
        }
    }
    setDecorations(pin, pollState);
}

void Source::youtubeWebPollState(const QJsonObject &response) {
    if (activePoll.isEmpty() || (activePoll.value("ended").toBool() && !activePoll.value("last_observed").toBool())) return;
    QJsonObject state = activePoll;
    walk(response["frameworkUpdates"], [&](const QJsonObject &node) {
        const auto entity = node["liveChatPollStateEntity"].toObject();
        if (entity.isEmpty()) return;
        const auto runs = at(entity, {"collapsedMetadataText", "runs"}).toArray();
        if (runs.size() >= 3) {
            const QString question = str(runs[2].toObject()["text"]);
            if (!question.isEmpty()) state["text"] = question;
        }
        const auto names = state["choices"].toArray();
        const auto ratios = entity["pollChoiceStates"].toArray();
        if (!names.isEmpty() && names.size() == ratios.size()) {
            QStringList choices;
            for (int i = 0; i < names.size(); ++i) {
                QString choice = str(names[i]);
                const auto ratio = ratios[i].toObject()["value"].toObject()["voteRatio"];
                if (ratio.isDouble()) {
                    double value = ratio.toDouble();
                    if (value >= 0 && value <= 1) value *= 100;
                    choice += QString(" (%1%)").arg(qRound(value));
                }
                choices << choice;
            }
            state["options"] = choices.join(" · ");
        }
    });
    setDecorations(pinned, state);
}

void Source::loadHistory() {
    if (closed || platform != "twitch" || historyLoaded || historyLoading ||
        !hub->cfg->data["load_twitch_history"].toBool(true) ||
        hub->testMode) {
        return;
    }
    historyLoading = true;
    QPointer<Source> self(this);
    const auto epoch = generation;
    hub->net->json(
        Network::query(
            "https://recent-messages.robotty.de/api/v2/recent-messages/" + id,
            {{"limit", "200"}, {"hide_moderated_messages", "true"}, {"hide_moderation_messages", "true"}}),
        [self, epoch](QJsonObject d, QString error) {
            if (!self || self->closed || self->generation != epoch) {
                return;
            }
            self->historyLoading = false;
            if (!error.isEmpty() || !d["messages"].isArray()) {
                self->connectionStatus(self->hub->cfg->T(
                    "Recent Twitch history unavailable; live chat continues independently."));
                self->historyRetry.start(self->historyRetryMs + int(qHash(self->key) % 5000));
                self->historyRetryMs = qMin(300000, self->historyRetryMs * 2);
                return;
            }
            self->historyLoaded = true;
            self->historyRetryMs = 30000;
            bool added = false;
            for (const auto &v : d["messages"].toArray()) {
                auto p = parseIrc(v.toString());
                if (p.command == "PRIVMSG" && p.channel == self->id) {
                    auto m = p.message;
                    if (!m.id.isEmpty() && self->seen.contains(m.id)) continue;
                    m.history = true;
                    m.source = self->key;
                    m.seq = ++self->hub->sequence;
                    if (!m.id.isEmpty()) {
                        self->seen.insert(m.id);
                        self->seenOrder.enqueue(m.id);
                    }
                    self->history << m;
                    added = true;
                }
            }
            if (added) {
                std::stable_sort(self->history.begin(), self->history.end(), messageOrder);
                self->trim();
                emit self->changed();
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
    if (platform != "twitch" || hub->testMode || closed) {
        return;
    }
    const auto now = QDateTime::currentSecsSinceEpoch();
    const QString channelRoom = room();
    if (badgeChannelRoom != channelRoom) {
        badgeChannelRoom = channelRoom;
        channelBadges.clear();
        channelBadgesLoaded = 0;
        channelBadgesPending = false;
        badges = globalBadges;
    }
    auto request = [this](bool channel, QString requestedRoom) {
        auto &pending = channel ? channelBadgesPending : globalBadgesPending;
        pending = true;
        QPointer<Source> self(this);
        const auto epoch = generation;
        auto ready = [self, epoch, channel, requestedRoom](QJsonObject data, QString error) {
            if (!self || self->closed || self->generation != epoch ||
                (channel && self->badgeChannelRoom != requestedRoom)) return;
            (channel ? self->channelBadgesPending : self->globalBadgesPending) = false;
            if (!error.isEmpty() || !data["data"].isArray()) {
                if (!self->badgeRetry.isActive()) self->badgeRetry.start();
                return;
            }
            QHash<QString, Badge> loaded;
            for (const auto &set : data["data"].toArray()) {
                const auto object = set.toObject();
                for (const auto &v : object["versions"].toArray()) {
                    const auto version = v.toObject();
                    Badge badge;
                    badge.id = str(object["set_id"]);
                    badge.version = str(version["id"]);
                    badge.label = str(version["title"]);
                    badge.urls = {str(version["image_url_4x"]), str(version["image_url_2x"]), str(version["image_url_1x"])};
                    badge.urls.removeAll({});
                    badge.role = badge.id == "broadcaster" ? "owner" : badge.id;
                    if (!badge.id.isEmpty() && !badge.version.isEmpty() && !badge.urls.isEmpty())
                        loaded[badge.id + '/' + badge.version] = badge;
                }
            }
            (channel ? self->channelBadges : self->globalBadges) = loaded;
            (channel ? self->channelBadgesLoaded : self->globalBadgesLoaded) = QDateTime::currentSecsSinceEpoch();
            self->badges = self->globalBadges;
            for (auto i = self->channelBadges.cbegin(); i != self->channelBadges.cend(); ++i)
                self->badges[i.key()] = i.value();
            emit self->changed();
        };
        if (channel) hub->accounts->helix("chat/badges", ready, "GET", {{"broadcaster_id", requestedRoom}});
        else hub->accounts->helix("chat/badges/global", ready);
    };
    if (!globalBadgesPending && now - globalBadgesLoaded >= 21600) request(false, {});
    if (!channelRoom.isEmpty() && !channelBadgesPending && now - channelBadgesLoaded >= 21600)
        request(true, channelRoom);
}

void Source::youtubeConnect() {
    if (closed || paused || requesting || hub->testMode) {
        return;
    }
    if (deferYouTubeForCooldown()) {
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
            if (str(info["avatar"]).isEmpty()) info["avatar"] = self->header["avatar"];
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
            const auto initialData = embeddedJson(html, "ytInitialData");
            auto full = at(initialData, {"contents", "liveChatRenderer"}).toObject();
            if (!full.isEmpty()) {
                chat = full;
            }
            auto c = configOf(html);
            for (auto i = c.begin(); i != c.end(); ++i) {
                config[i.key()] = i.value();
            }
            self->youtubeContinue(chat, config);
            self->youtubeModeNotices(initialData);
            self->youtubeWebPollState(initialData);
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
    if (hub->youtubeCooldownRemaining() == 0) {
        hub->youtubeRateDelay = 120000;
        hub->youtubeCooldownUntil = 0;
    }
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
    connectionStatus("Connected to YouTube · no API key · 0 reading units");
    if (!paused) {
        poll.start(0);
    }
}

void Source::queueYoutubeMessages(const QVector<Message> &messages) {
    if (messages.isEmpty() || closed) return;
    for (const auto &message : messages) youtubePending.enqueue(message);
    // Publish the first message immediately, then spread the remaining burst for at most 1.5 s.
    if (!youtubeDisplayTimer.isActive() && !youtubePending.isEmpty()) publish(youtubePending.dequeue());
    if (!youtubePending.isEmpty()) {
        youtubeDisplayChunk = qMax(1, (int(youtubePending.size()) + 299) / 300);
        youtubeDisplayTimer.start(qBound(5, 1500 * youtubeDisplayChunk / int(youtubePending.size()), 100));
    }
}

void Source::flushYoutubeMessages() {
    youtubeDisplayTimer.stop();
    while (!youtubePending.isEmpty() && !closed) publish(youtubePending.dequeue());
    if (closed) youtubePending.clear();
}

int Source::youtubeWebDelay() const {
    int delay = recommendedMs;
    // A shared cooldown switches all chats back to YouTube's suggested pace after a rate limit.
    if (recommendedMs > 5000 && recommendedMs <= 10000 &&
        hub->youtubeClock.elapsed() >= hub->youtubeConservativeUntil) delay = 5000;
    return qMax(qRound(interval * 1000), delay);
}

void Source::youtubeActions(const QJsonArray &actions, bool historical) {
    youtubeModeNotices(actions);
    youtubeBannerActions(actions);
    QVector<Message> batch;
    auto flushBatch = [this, historical, &batch] {
        if (historical) for (const auto &message : std::as_const(batch)) publish(message);
        else queueYoutubeMessages(batch);
        batch.clear();
    };
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
                batch.append(m);
            }
        }
        for (const auto &key : QStringList{"markChatItemAsDeletedAction", "removeChatItemAction"}) {
            auto target = str(at(a, {key, "targetItemId"}));
            if (!target.isEmpty()) {
                flushBatch();
                flushYoutubeMessages();
                redact({{"message_id", target}});
            }
        }
        auto uid = str(at(a, {"markChatItemsByAuthorAsDeletedAction", "externalChannelId"}));
        if (!uid.isEmpty()) {
            flushBatch();
            flushYoutubeMessages();
            redact({{"user_id", uid}});
        }
    }
    flushBatch();
}

void Source::youtubePoll() {
    if (closed || paused || requesting) {
        return;
    }
    if (deferYouTubeForCooldown()) {
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
            self->youtubeWebPollState(d);
            self->initial = false;
            self->requesting = false;
            if (self->hub->youtubeCooldownRemaining() == 0) {
                self->hub->youtubeRateDelay = 120000;
                self->hub->youtubeCooldownUntil = 0;
            }
            self->retryMs = 5000;
            if (self->continuation.isEmpty()) {
                self->status("The live chat has ended.");
                return;
            }
            if (!self->paused) {
                self->poll.start(self->youtubeWebDelay());
            }
        },
        "POST", {{"context", context}, {"continuation", continuation}}, headers);
}

void Source::youtubeError(const QString &error, int delay) {
    requesting = false;
    const bool rateLimited = error.startsWith("HTTP 429:");
    int wait = delay ? delay : retryMs;
    if (rateLimited) {
        wait = qMax(qMax(delay, hub->youtubeRateDelay), hub->youtubeCooldownRemaining());
        hub->youtubeCooldownUntil = hub->youtubeClock.elapsed() + wait;
        hub->youtubeConservativeUntil = hub->youtubeClock.elapsed() + 3600000;
        hub->youtubeRateDelay = qMin(1800000, hub->youtubeRateDelay * 2);
    }
    if (rateLimited && apiFallbackEnabled && !closed && !paused) {
        if (hub->accounts->googleCanReadChat()) {
            apiActive = true;
            apiPageToken.clear();
            status(hub->cfg->T("YouTube web chat was rate limited. Switching to the official API; "
                               "API reading uses quota."));
            youtubeApiConnect();
            return;
        }
        status(hub->cfg->T("Sign in to YouTube in Settings → Accounts to use the official API fallback."));
    }
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

bool Source::deferYouTubeForCooldown() {
    const int wait = hub->youtubeCooldownRemaining();
    if (wait <= 0) {
        return false;
    }
    if (!closed && !paused) {
        poll.start(wait);
    }
    return true;
}

void Source::setApiFallback(bool enabled) {
    apiFallbackEnabled = enabled;
    auto all = hub->cfg->data["chat_settings"].toObject();
    auto settings = all[key].toObject();
    settings["youtube_api_fallback"] = enabled;
    all[key] = settings;
    hub->cfg->data["chat_settings"] = all;
    hub->cfg->save();
    if (!enabled && apiActive) {
        apiActive = false;
        reconnect();
    } else if (enabled && hub->youtubeCooldownRemaining() > 0 && !paused) {
        if (!hub->accounts->googleCanReadChat()) {
            status(hub->cfg->T("Sign in to YouTube in Settings → Accounts to use the official API fallback."));
            return;
        }
        ++generation;
        requesting = false;
        poll.stop();
        apiActive = true;
        apiPageToken.clear();
        youtubeApiConnect();
    }
}

void Source::youtubeApiError(const QString &error) {
    requesting = false;
    if (closed || paused || !apiActive) {
        return;
    }
    if (error.startsWith("HTTP 400:") || error.startsWith("HTTP 401:") || error.startsWith("HTTP 403:") ||
        error.startsWith("HTTP 404:") || error.contains("sign-in", Qt::CaseInsensitive) ||
        error.contains("OAuth", Qt::CaseInsensitive) || error.contains("no active live chat", Qt::CaseInsensitive)) {
        status(hub->cfg->T("Official YouTube API stopped: {error}", {{"error", error}}));
        return;
    }
    const int wait = apiRetryMs;
    apiRetryMs = qMin(1800000, apiRetryMs * 2);
    status(hub->cfg->T("Official YouTube API connection interrupted. Retrying in {seconds}s.",
                       {{"seconds", QString::number(wait / 1000)}}));
    poll.start(wait);
}

void Source::youtubeApiConnect() {
    if (closed || paused || requesting || !apiActive) {
        return;
    }
    if (!hub->accounts->googleCanReadChat()) {
        status(hub->cfg->T("Sign in to YouTube in Settings → Accounts to use the official API fallback."));
        return;
    }
    if (hub->quota->remaining() < 6 && str(header["live_chat_id"]).isEmpty()) {
        status(hub->cfg->T("The estimated YouTube quota is too low to start API chat reading."));
        return;
    }
    requesting = true;
    const auto epoch = generation;
    QPointer<Source> self(this);
    hub->youtubeChatId(this, [self, epoch](QString chat, QString error) {
        if (!self || self->closed || self->generation != epoch || !self->apiActive) {
            return;
        }
        self->requesting = false;
        if (!error.isEmpty() || chat.isEmpty()) {
            self->youtubeApiError(error.isEmpty() ? "No active live chat." : error);
            return;
        }
        self->initial = true;
        self->connectionStatus(self->hub->cfg->T("Connected with the official YouTube API · 5 units per request."));
        self->youtubeApiPoll();
    });
}

void Source::youtubeApiPoll() {
    if (closed || paused || requesting || !apiActive) {
        return;
    }
    if (str(header["live_chat_id"]).isEmpty()) {
        youtubeApiConnect();
        return;
    }
    if (hub->quota->remaining() < 5) {
        status(hub->cfg->T("The estimated YouTube quota is exhausted. API chat reading stopped."));
        return;
    }
    requesting = true;
    ++polls;
    const auto epoch = generation;
    QPointer<Source> self(this);
    QMap<QString, QString> params{{"part", "id,snippet,authorDetails"},
                                  {"liveChatId", str(header["live_chat_id"])}, {"maxResults", "200"}};
    if (!apiPageToken.isEmpty()) {
        params["pageToken"] = apiPageToken;
    }
    hub->accounts->youtube("liveChat/messages", [self, epoch](QJsonObject data, QString error) {
        if (!self || self->closed || self->generation != epoch || !self->apiActive) {
            return;
        }
        self->requesting = false;
        if (!error.isEmpty()) {
            self->youtubeApiError(error);
            return;
        }
        self->apiRetryMs = 120000;
        self->youtubeApiPollState(data);
        for (const auto &value : data["items"].toArray()) {
            auto item = value.toObject();
            auto snippet = item["snippet"].toObject();
            auto author = item["authorDetails"].toObject();
            const QString type = str(snippet["type"]);
            if (type == "messageDeletedEvent") {
                auto target = str(at(snippet, {"messageDeletedDetails", "deletedMessageId"}));
                if (!target.isEmpty()) self->redact({{"message_id", target}});
                continue;
            }
            if (type == "chatEndedEvent") {
                self->status("The live chat has ended.");
                return;
            }
            Message m;
            m.id = m.apiId = str(item["id"]);
            m.uid = str(author["channelId"]);
            m.name = str(author["displayName"]);
            m.text = str(snippet["displayMessage"]);
            m.avatar = str(author["profileImageUrl"]);
            m.stamp = QDateTime::fromString(str(snippet["publishedAt"]), Qt::ISODateWithMs);
            if (!m.stamp.isValid()) m.stamp = QDateTime::fromString(str(snippet["publishedAt"]), Qt::ISODate);
            m.history = self->initial;
            if (QStringList{"newSponsorEvent", "memberMilestoneChatEvent", "membershipGiftingEvent",
                            "giftMembershipReceivedEvent", "superChatEvent", "superStickerEvent", "giftEvent"}
                    .contains(type)) {
                m.special = "alert";
                m.alertKind = type == "membershipGiftingEvent" || type == "giftMembershipReceivedEvent" || type == "giftEvent"
                                  ? "gift" : type == "superChatEvent" || type == "superStickerEvent"
                                  ? "money" : "subscription";
                if (m.name.isEmpty()) m.name = "YouTube";
                if (type == "giftEvent" && m.text.isEmpty()) {
                    auto gift = str(at(snippet, {"giftDetails", "giftName"}));
                    if (gift.isEmpty()) gift = str(at(snippet, {"giftDetails", "altText"}));
                    if (!gift.isEmpty()) m.text = self->hub->cfg->T("Sent a gift: {gift}", {{"gift", gift}});
                }
                if (!m.text.isEmpty()) { m.alertText = m.text; m.alertBodyStart = m.text.size(); }
            }
            for (const auto &role : QList<QPair<QString, QString>>{{"isChatOwner", "owner"},
                       {"isChatModerator", "moderator"}, {"isChatSponsor", "subscriber"}}) {
                if (author[role.first].toBool()) {
                    Badge badge;
                    badge.id = badge.role = role.second;
                    m.badges << badge;
                }
            }
            if (!m.id.isEmpty() && !m.name.isEmpty() && !m.text.isEmpty()) self->publish(m);
        }
        self->initial = false;
        self->apiPageToken = str(data["nextPageToken"]);
        if (self->apiPageToken.isEmpty()) {
            self->status("The live chat has ended.");
            return;
        }
        if (!self->paused) {
            self->poll.start(qMax(qRound(self->apiInterval * 1000), data["pollingIntervalMillis"].toInt(5000)));
        }
    }, "GET", params, {}, 5);
}

static QByteArray twitchConnectionIdentity(const Accounts *accounts) {
    const auto value = str(accounts->twitch["login"]).toUtf8() + '\0' +
                       str(accounts->twitch["access_token"]).toUtf8();
    return QCryptographicHash::hash(value, QCryptographicHash::Sha256);
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
        buffer.clear();
        heartbeat.stop();
        for (auto source : channels) {
            if (source) {
                source->twitchJoined = false;
                if (source->chatModesKnown) {
                    source->chatModes = {};
                    source->chatModesKnown = false;
                    emit source->chatModesChanged();
                }
                if (source->twitchModeratorKnown) {
                    source->twitchModeratorKnown = false;
                    emit source->permissionsChanged();
                }
            }
        }
        if (!closing && !intentionalDisconnect && !authRejected && !channels.isEmpty()) {
            retry.start();
        }
    });
    connect(&socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (closing || intentionalDisconnect || authRejected) {
            return;
        }
        connecting = false;
        for (auto source : channels) {
            if (source) {
                source->connectionStatus("Twitch connection interrupted. Reconnecting…");
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
            if (!connectionIdentity.isEmpty() && connectionIdentity != twitchConnectionIdentity(hub->accounts) &&
                (registered || connecting || socket.state() != QAbstractSocket::UnconnectedState)) {
                reconnect();
            } else {
                connectServer();
            }
        }
    });
}

void TwitchConnection::join(Source *s) {
    if (channels.value(s->id) == s) return;
    channels[s->id] = s;
    if (registered) {
        socket.write("JOIN #" + s->id.toUtf8() + "\r\n");
    } else {
        connectServer();
    }
}

void TwitchConnection::part(Source *s) {
    channels.remove(s->id);
    s->twitchJoined = false;
    if (registered) {
        socket.write("PART #" + s->id.toUtf8() + "\r\n");
    }
}

void TwitchConnection::connectServer() {
    if (hub->testMode || !hub->accounts->loaded || closing || authRejected || channels.isEmpty() ||
        connecting || registered || socket.state() != QAbstractSocket::UnconnectedState) {
        return;
    }
    connecting = true;
    retry.stop();
    const auto attempt = ++connectionAttempt;
    hub->accounts->twitchToken([this, attempt](QString token, QString error) {
        if (closing || attempt != connectionAttempt) {
            return;
        }
        if (!error.isEmpty()) {
            connecting = false;
            for (auto s : channels) {
                if (s) {
                    s->status(error);
                }
            }
            return;
        }
        if (token.isEmpty()) {
            connecting = false;
            return;
        }
        connectionIdentity = twitchConnectionIdentity(hub->accounts);
        socket.connectToHostEncrypted("irc.chat.twitch.tv", 6697);
    });
}

void TwitchConnection::reconnect() {
    if (closing || hub->testMode) return;
    ++connectionAttempt;
    for (auto source : channels) if (source) {
        source->twitchJoined = false;
        if (source->twitchModeratorKnown) {
            source->twitchModeratorKnown = false;
            emit source->permissionsChanged();
        }
    }
    retry.stop();
    intentionalDisconnect = true;
    socket.abort();
    intentionalDisconnect = false;
    registered = connecting = false;
    connectionIdentity.clear();
    buffer.clear();
    heartbeat.stop();
    connectServer();
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
        if (registered) return;
        registered = true;
        connecting = false;
        retry.stop();
        for (auto i = channels.begin(); i != channels.end(); ++i) {
            if (i.value()) {
                socket.write("JOIN #" + i.key().toUtf8() + "\r\n");
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
    if ((p.command == "PRIVMSG" || p.command == "USERNOTICE") && !p.message.text.isEmpty()) {
        source->publish(p.message);
    } else if (p.command == "USERSTATE") {
        source->updateTwitchUserState(p.tags);
        if (!source->twitchJoined) {
            source->twitchJoined = true;
            source->connectionStatus("Connected to Twitch");
        }
    } else if (p.command == "ROOMSTATE") {
        QJsonObject modes;
        const auto &tags = p.tags;
        if (tags.contains("subs-only")) modes["subscribers"] = str(tags["subs-only"]) == "1";
        if (tags.contains("emote-only")) modes["emotes"] = str(tags["emote-only"]) == "1";
        if (tags.contains("r9k")) modes["unique"] = str(tags["r9k"]) == "1";
        if (tags.contains("followers-only")) {
            const int minutes = str(tags["followers-only"]).toInt();
            modes["followers"] = minutes >= 0;
            modes["follow_minutes"] = qMax(0, minutes);
        }
        if (tags.contains("slow")) {
            const int seconds = str(tags["slow"]).toInt();
            modes["slow"] = seconds > 0;
            modes["slow_seconds"] = qMax(0, seconds);
        }
        if (!modes.isEmpty()) source->updateChatModes(modes);
        QString room = str(p.tags["room-id"]);
        if (!room.isEmpty() && source->room() != room) {
            source->header["channel_id"] = room;
            source->loadEmotes(room);
            source->loadBadges();
        }
    } else if (p.command == "CLEARMSG") {
        source->redact({{"message_id", p.tags["target-msg-id"]}});
    } else if (p.command == "CLEARCHAT") {
        const QString user = str(p.tags.value("target-user-id"));
        const QString login = p.params.contains(" :") ? p.params.section(" :", 1).trimmed() : QString();
        if (user.isEmpty() && login.isEmpty()) {
            source->redact({{"all", true}});
        } else {
            bool valid = false;
            const qint64 seconds = str(p.tags.value("ban-duration")).toLongLong(&valid);
            const QString reason = p.tags.contains("ban-duration") ? (valid && seconds > 0 ? "timeout" : "removed")
                                   : p.tags.isEmpty() ? "removed" : "ban";
            source->redact({{"user_id", user}, {"user_login", login}, {"reason", reason},
                            {"duration", double(valid ? qMax<qint64>(0, seconds) : 0)}});
        }
    } else if (p.command == "NOTICE") {
        source->status(p.params.section(" :", 1));
    }
}

void TwitchConnection::close() {
    closing = true;
    ++connectionAttempt;
    retry.stop();
    heartbeat.stop();
    socket.abort();
}

Hub::Hub(Config *c, Network *n, Accounts *a, Images *i, Quota *q, QObject *p)
    : QObject(p), cfg(c), net(n), accounts(a), images(i), quota(q), twitch(this) {
    youtubeClock.start();
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

int Hub::youtubeCooldownRemaining() const {
    return int(qBound(qint64(0), youtubeCooldownUntil - youtubeClock.elapsed(), qint64(1800000)));
}

Source *Hub::source(QString platform, QString id) {
    if (platform == "twitch" || platform == "kick") id = id.toLower();
    QString key = platform == "youtube" ? id : platform + ":" + id;
    if (sources.contains(key)) {
        return sources[key];
    }
    auto *s = new Source(this, platform, id);
    sources[key] = s;
    connect(s, &Source::changed, this, [this, s] { emit sourceChanged(s); });
    connect(s, &Source::headerChanged, this, [this, s] { emit sourceChanged(s); });
    connect(s, &Source::permissionsChanged, this, [this, s] { emit sourceChanged(s); });
    connect(s, &Source::chatModesChanged, this, [this, s] { emit sourceChanged(s); });
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

void Hub::requestTwitchAvatars(const QStringList &logins) {
    if (str(accounts->twitch["access_token"]).isEmpty()) return;
    QStringList requested;
    const auto now = QDateTime::currentSecsSinceEpoch();
    for (const auto &value : logins) {
        const auto login = twitchLogin('@' + stripAt(value)).toLower();
        if (login.isEmpty() || twitchAvatarUrls.contains(login) || twitchAvatarPending.contains(login) ||
            twitchAvatarRetry.value(login) > now) continue;
        twitchAvatarPending.insert(login);
        requested << login;
        if (requested.size() == 8) break;
    }
    if (requested.isEmpty()) return;
    QPointer<Hub> self(this);
    accounts->twitchToken([self, requested](QString token, QString error) {
        if (!self) return;
        if (!error.isEmpty() || token.isEmpty()) {
            for (const auto &login : requested) {
                self->twitchAvatarPending.remove(login);
                self->twitchAvatarRetry[login] = QDateTime::currentSecsSinceEpoch() + 300;
            }
            return;
        }
        QUrl url("https://api.twitch.tv/helix/users");
        QUrlQuery query;
        for (const auto &login : requested) query.addQueryItem("login", login);
        url.setQuery(query);
        self->net->json(url, [self, requested](QJsonObject data, QString problem) {
            if (!self) return;
            if (self->twitchAvatarUrls.size() > 2000) self->twitchAvatarUrls.clear();
            if (self->twitchAvatarRetry.size() > 2000) self->twitchAvatarRetry.clear();
            const auto retry = QDateTime::currentSecsSinceEpoch() + (problem.isEmpty() ? 3600 : 300);
            for (const auto &login : requested) {
                self->twitchAvatarPending.remove(login);
                self->twitchAvatarRetry[login] = retry;
            }
            if (problem.isEmpty()) {
                for (const auto &item : data["data"].toArray()) {
                    const auto user = item.toObject();
                    const auto login = str(user["login"]).toLower();
                    const auto avatar = str(user["profile_image_url"]);
                    if (requested.contains(login) && Images::allowed(QUrl(avatar))) {
                        self->twitchAvatarUrls[login] = avatar;
                    }
                }
                emit self->twitchAvatarsReady();
            }
        }, "GET", {}, {{"Client-Id", TwitchClient.toUtf8()}, {"Authorization", "Bearer " + token.toUtf8()}});
    });
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
                              auto snippet = item["snippet"].toObject();
                              const auto channelId = str(snippet["channelId"]);
                              if (!channelId.isEmpty()) {
                                  self->header["channel_id"] = channelId;
                                  self->header["channel"] = snippet["channelTitle"];
                                  self->header["title"] = snippet["title"];
                                  self->header["live"] = true;
                                  self->header["started_at"] = at(item, {"liveStreamingDetails", "actualStartTime"});
                                  self->loadEmotes(channelId);
                                  emit self->changed();
                              }
                          }
                          done(id, e);
                      },
                      "GET", {{"part", "snippet,liveStreamingDetails"}, {"id", s->id}});
}

void Hub::send(Source *s, QString text, Message reply, JsonCallback done) {
    if (s && s->isVod) { done({}, "VOD replay is read-only."); return; }
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
    int limit = s->platform == "youtube" ? 200 : 500;
    int characters = platformCharacterCount(text, s->platform);
    if (characters > limit || (s->platform == "kick" && text.toUtf8().size() > 2048)) {
        done({}, "This message exceeds the channel's character limit.");
        return;
    }
    QPointer<Source> self(s);
    if (s->platform == "kick") {
        if (s->room().isEmpty()) {
            done({}, "Channel information is still loading. Try again shortly.");
            return;
        }
        bool ok = false;
        const auto uid = s->room().toLongLong(&ok);
        if (!ok || uid <= 0) { done({}, "Invalid Kick channel information."); return; }
        QString kickText = text;
        const auto matches = QRegularExpression("\\S+").globalMatch(text);
        QVector<QRegularExpressionMatch> tokens;
        auto iter = matches;
        while (iter.hasNext()) tokens << iter.next();
        static const QRegularExpression nativeUrl(R"(^https://files\.kick\.com/emotes/([A-Za-z0-9_-]{1,80})/fullsize$)");
        for (auto i = tokens.crbegin(); i != tokens.crend(); ++i) {
            const QString code = i->captured();
            const auto span = s->emotes.value(code);
            const auto native = nativeUrl.match(span.urls.value(0));
            if (native.hasMatch() && !code.contains(']') && !code.contains('\n'))
                kickText.replace(i->capturedStart(), i->capturedLength(),
                                 "[emote:" + native.captured(1) + ':' + code + ']');
        }
        if (platformCharacterCount(kickText, "kick") > 500 || kickText.toUtf8().size() > 2048) {
            done({}, "This message exceeds the channel's character limit."); return;
        }
        QJsonObject body{{"broadcaster_user_id", double(uid)}, {"content", kickText}, {"type", "user"}};
        if (!reply.id.isEmpty()) body["reply_to_message_id"] = reply.id;
        accounts->kickApi("chat", [this, self, text, reply, done](QJsonObject data, QString error) {
            if (!self) return;
            const auto result = data["data"].toObject();
            if (error.isEmpty() && !result["is_sent"].toBool()) error = "Kick did not accept the message.";
            if (error.isEmpty()) {
                Message message;
                message.id = str(result["message_id"]);
                message.uid = str(accounts->kick["user_id"]);
                message.name = str(accounts->kick["name"]);
                message.login = message.name;
                message.text = text;
                message.own = true;
                message.stamp = QDateTime::currentDateTime();
                if (!reply.id.isEmpty()) {
                    message.reply = {{"id", reply.id}, {"name", reply.name}, {"login", reply.login},
                                     {"text", reply.text}, {"thread_id", reply.reply["thread_id"].toString(reply.id)}};
                }
                self->publish(message);
            }
            done(data, error);
        }, "POST", body);
        return;
    }
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
                        if (!m.uid.isEmpty() && self->ownTwitchUserId == m.uid) {
                            m.badges = self->ownTwitchBadges;
                            m.badgeInfo = self->ownTwitchBadgeInfo;
                            m.color = self->ownTwitchColor;
                            if (!self->ownTwitchDisplayName.isEmpty()) m.name = self->ownTwitchDisplayName;
                        }
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

bool Hub::canEditChannel(const Source *s) const {
    if (s && s->isVod) return false;
    if (!s) return false;
    if (s->platform == "kick") {
        const auto uid = str(accounts->kick["user_id"]);
        return !uid.isEmpty() && uid == s->room() && accounts->kickAllows({"channel:write"});
    }
    if (s->platform == "twitch") {
        const auto uid = str(accounts->twitch["user_id"]), login = str(accounts->twitch["login"]);
        return !str(accounts->twitch["access_token"]).isEmpty() &&
               ((!uid.isEmpty() && uid == s->room()) ||
                (!login.isEmpty() && login.compare(s->id, Qt::CaseInsensitive) == 0));
    }
    const auto uid = str(accounts->google["channel_id"]);
    return !uid.isEmpty() && uid == s->room() &&
           (!str(accounts->google["access_token"]).isEmpty() ||
            !str(accounts->google["refresh_token"]).isEmpty());
}

bool Hub::hasModerationRole(const Source *s) const {
    if (s && s->isVod) return false;
    if (!s) return false;
    if (s->platform == "kick") {
        const auto uid = str(accounts->kick["user_id"]);
        const bool role = !uid.isEmpty() && (uid == s->room() ||
            (uid == str(s->header["kick_role_uid"]) && s->header["kick_moderator"].toBool()));
        return role && (accounts->kickAllows({"moderation:ban"}) ||
                        accounts->kickAllows({"moderation:chat_message:manage"}));
    }
    if (canEditChannel(s)) return true;
    if (s->platform == "twitch") {
        const auto uid = str(accounts->twitch["user_id"]);
        return !uid.isEmpty() && !str(accounts->twitch["access_token"]).isEmpty() &&
               s->ownTwitchUserId == uid &&
               s->twitchModeratorKnown && s->twitchModerator;
    }
    const auto uid = str(accounts->google["channel_id"]);
    if (uid.isEmpty() || (str(accounts->google["access_token"]).isEmpty() &&
                          str(accounts->google["refresh_token"]).isEmpty())) return false;
    if (s->youtubeRoleUserId == uid && s->youtubeModeratorKnown) return s->youtubeModerator;
    for (auto i = s->history.crbegin(); i != s->history.crend(); ++i) {
        if (i->uid == uid && !i->system && (!i->own || !i->badges.isEmpty())) {
            const auto userRoles = roles(*i);
            return userRoles.contains("owner") || userRoles.contains("moderator");
        }
    }
    return false;
}

void Hub::canModerate(Source *s, std::function<void(bool, QString)> done) {
    if (s && s->isVod) { done(false, {}); return; }
    if (!s) { done(false, "The channel was closed."); return; }
    const QString uid = str(accounts->twitch["user_id"]);
    if (uid.isEmpty() || s->room().isEmpty()) {
        done(false, "Channel or account information is still loading.");
        return;
    }
    if (s->room() == uid) {
        done(true, {});
        return;
    }
    if (s->ownTwitchUserId == uid && s->twitchModeratorKnown) {
        done(s->twitchModerator, {});
        return;
    }
    moderatedPage(s, uid, {}, 0, std::move(done));
}

void Hub::moderatedPage(QPointer<Source> safe, QString uid, QString cursor, int page,
                        std::function<void(bool, QString)> done) {
    QMap<QString, QString> params{{"user_id", uid}, {"first", "100"}};
    if (!cursor.isEmpty()) params["after"] = cursor;
    accounts->helix("moderation/channels",
                    [this, safe, uid, cursor, page, done](QJsonObject d, QString e) {
                        if (!safe) {
                            done(false, "The channel was closed.");
                            return;
                        }
                        if (!e.isEmpty()) { done(false, e); return; }
                        if (uid != str(accounts->twitch["user_id"])) {
                            done(false, "The Twitch account changed.");
                            return;
                        }
                        for (const auto &v : d["data"].toArray()) {
                            if (str(v.toObject()["broadcaster_id"]) == safe->room()) {
                                done(true, {});
                                return;
                            }
                        }
                        const auto next = str(at(d, {"pagination", "cursor"}));
                        if (next.isEmpty()) { done(false, {}); return; }
                        if (next == cursor || page >= 99) {
                            done(false, "Twitch moderator list pagination failed.");
                            return;
                        }
                        moderatedPage(safe, uid, next, page + 1, done);
                    },
                    "GET", params);
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
    if (s && s->isVod) { done({}, "VOD replay is read-only."); return; }
    if (s->platform == "kick") {
        if (!hasModerationRole(s)) { done({}, "Kick moderation requires the channel owner or moderator role and permissions."); return; }
        const auto scope = action == "delete" ? QString("moderation:chat_message:manage") : QString("moderation:ban");
        if (!accounts->kickAllows({scope})) { done({}, "Enable the required Kick permissions and reconnect in Settings → Accounts."); return; }
        if (action == "delete") {
            if (m.id.isEmpty()) { done({}, "This message has no moderation ID."); return; }
            QPointer<Source> safe(s);
            accounts->kickApi("chat/" + QString::fromLatin1(QUrl::toPercentEncoding(m.id)),
                [safe, m, done](QJsonObject d, QString e) {
                    if (safe && e.isEmpty()) safe->redact({{"message_id", m.id}});
                    done(d, e);
                }, "DELETE");
            return;
        }
        bool validUser = false, validRoom = false;
        const qint64 userId = m.uid.toLongLong(&validUser), broadcaster = s->room().toLongLong(&validRoom);
        if (!validUser || !validRoom || userId <= 0 || broadcaster <= 0 || userId == broadcaster) {
            done({}, "Invalid Kick moderation target."); return;
        }
        QJsonObject body{{"broadcaster_user_id", double(broadcaster)}, {"user_id", double(userId)}};
        if (action == "timeout") body["duration"] = qBound(1, (duration + 59) / 60, 10080);
        if (action != "timeout" && action != "ban" && action != "unban") { done({}, "Unknown moderation action."); return; }
        QPointer<Source> safe(s);
        accounts->kickApi("moderation/bans", [safe, m, action, duration, done](QJsonObject d, QString e) {
            if (safe && e.isEmpty() && action != "unban")
                safe->redact({{"user_id", m.uid}, {"reason", action},
                              {"duration", action == "timeout" ? qBound(1, (duration + 59) / 60, 10080) * 60 : 0}});
            done(d, e);
        }, action == "unban" ? "DELETE" : "POST", body);
        return;
    }
    QPointer<Source> self(s);
    auto complete = [self, m, action, duration, done](QJsonObject d, QString e) {
        if (self && e.isEmpty() && action != "unban") {
            self->redact(action == "delete" ? QJsonObject{{"message_id", m.id}}
                                            : QJsonObject{{"user_id", m.uid}, {"reason", action}, {"duration", duration}});
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
    if (s->platform == "kick") {
        if (!canEditChannel(s)) { done({}, "Kick requires the broadcaster account and channel editing permission."); return; }
        QJsonObject body;
        if (!title.trimmed().isEmpty()) body["stream_title"] = title.trimmed();
        if (!categoryId.isEmpty()) {
            bool ok = false; const auto id = categoryId.toLongLong(&ok);
            if (!ok || id <= 0) { done({}, "Choose a valid Kick category."); return; }
            body["category_id"] = double(id);
        }
        if (body.isEmpty()) { done({}, "Enter a title or choose a category."); return; }
        accounts->kickApi("channels", done, "PATCH", body); return;
    }
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
    if (s->platform == "kick") { done({}, "Public details unavailable"); return; }
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
    if (m.uid.isEmpty()) {
        done({}, "Public details unavailable");
        return;
    }
    accounts->youtube("channels",
                      [done](QJsonObject d, QString e) { done(first(d["items"].toArray()).toObject(), e); },
                      "GET", {{"part", "snippet,statistics"}, {"id", m.uid}});
}

void Hub::followSince(Source *source, QString userId, JsonCallback done) {
    if (!source || userId.isEmpty() || source->room().isEmpty()) {
        done({{"status", "unavailable"}}, {}); return;
    }
    QPointer<Source> safe(source);
    const QString room = source->room();
    if (source->platform == "twitch") {
        const QString account = str(accounts->twitch["user_id"]);
        if (account.isEmpty() || !accounts->connected("twitch")) {
            done({{"status", "sign_in"}}, {}); return;
        }
        if (userId == room) { done({{"status", "broadcaster"}}, {}); return; }
        const bool own = userId == account;
        const QString scope = own ? "user:read:follows" : "moderator:read:followers";
        const QSet<QString> required = own ? QSet<QString>{scope}
                                          : QSet<QString>{scope, "user:read:moderated_channels"};
        if (!accounts->twitchAllows(required)) {
            done({{"status", "permission"}, {"scope", scope}, {"own", own}}, {}); return;
        }
        auto lookup = [this, safe, room, account, userId, own, done] {
            if (!safe || safe->room() != room || str(accounts->twitch["user_id"]) != account) return;
            const auto endpoint = own ? QString("channels/followed") : QString("channels/followers");
            const QMap<QString, QString> params = own
                ? QMap<QString, QString>{{"user_id", account}, {"broadcaster_id", room}}
                : QMap<QString, QString>{{"user_id", userId}, {"broadcaster_id", room}};
            accounts->helix(endpoint, [this, safe, room, account, done](QJsonObject data, QString error) {
                if (!safe || safe->room() != room || str(accounts->twitch["user_id"]) != account) return;
                if (!error.isEmpty() || !data["data"].isArray()) {
                    done({{"status", "unavailable"}}, {}); return;
                }
                const auto items = data["data"].toArray();
                if (items.isEmpty()) { done({{"status", "not_following"}}, {}); return; }
                const auto date = str(items[0].toObject()["followed_at"]);
                done({{"status", date.isEmpty() ? "unavailable" : "following"}, {"date", date}}, {});
            }, "GET", params);
        };
        if (own) { lookup(); return; }
        canModerate(source, [safe, lookup, done](bool allowed, QString error) {
            if (!safe) return;
            if (!error.isEmpty() || !allowed) { done({{"status", "moderator_only"}}, {}); return; }
            lookup();
        });
        return;
    }
    if (source->platform == "youtube") {
        if (!accounts->googleCanReadChat()) { done({{"status", "sign_in"}}, {}); return; }
        if (quota->remaining() < 1) { done({{"status", "unavailable"}}, {}); return; }
        const QString account = str(accounts->google["channel_id"]);
        accounts->youtube("subscriptions", [this, safe, room, account, done](QJsonObject data, QString error) {
            if (!safe || safe->room() != room || str(accounts->google["channel_id"]) != account) return;
            const auto items = data["items"].toArray();
            const QString date = items.isEmpty() ? QString() : str(at(items[0].toObject(), {"snippet", "publishedAt"}));
            if (!error.isEmpty() || date.isEmpty()) {
                done({{"status", "unavailable"}}, {}); return;
            }
            done({{"status", "following"}, {"date", date}}, {});
        }, "GET", {{"part", "snippet"}, {"channelId", userId}, {"forChannelId", room}}, {}, 1);
        return;
    }
    done({{"status", "unavailable"}}, {});
}

void Hub::youtubeChannelByName(QString name, JsonCallback done) {
    name = name.trimmed();
    if (name.isEmpty() || name.size() > 100 || name.contains('/') || name.contains('\n')) {
        done({}, "Enter a YouTube channel handle or channel URL.");
        return;
    }
    const auto url = Network::query("https://www.youtube.com/results", {{"search_query", name}, {"sp", "EgIQAg=="}});
    QPointer<Hub> self(this);
    net->bytes(url, [self, name, done](QByteArray bytes, QString error) {
        if (!self) return;
        if (!error.isEmpty()) { done({}, error); return; }
        const auto initial = embeddedJson(QString::fromUtf8(bytes), "ytInitialData");
        QSet<QString> seen;
        QJsonArray matches;
        static const QRegularExpression channelId("^UC[A-Za-z0-9_-]{22}$");
        walk(initial, [&](const QJsonObject &node) {
            const auto channel = node["channelRenderer"].toObject();
            const auto id = str(channel["channelId"]);
            if (!seen.contains(id) && channelId.match(id).hasMatch() &&
                textOf(channel["title"]).compare(name, Qt::CaseInsensitive) == 0) {
                seen.insert(id);
                QString handle = str(at(channel, {"navigationEndpoint", "browseEndpoint", "canonicalBaseUrl"}));
                if (handle.startsWith("/@")) handle.remove(0, 1);
                else handle.clear();
                matches.append(QJsonObject{{"id", id}, {"title", textOf(channel["title"])},
                                           {"handle", handle}, {"details", textOf(channel["videoCountText"])}});
            }
        });
        if (matches.size() > 1) {
            done({{"channels", matches}}, {});
        } else if (matches.isEmpty()) {
            done({}, videoId(name) == name ? "HTTP 404: Could not find that YouTube channel." :
                                            "Could not find that YouTube channel.");
        } else {
            self->savedStreams({{"channel_id", str(matches[0].toObject()["id"])}}, done);
        }
    }, "GET", {}, {}, 16 * 1024 * 1024);
}

void Hub::savedStreams(QJsonObject saved, JsonCallback done) {
    QString value = str(saved["channel_id"]).trimmed();
    QString path;
    static const QRegularExpression channelId("^UC[A-Za-z0-9_-]{22}$");
    static const QRegularExpression handle("^[\\p{L}\\p{N}_.-]{1,100}$", QRegularExpression::UseUnicodePropertiesOption);
    const bool plainName = !value.contains('/') && !value.startsWith('@') && !channelId.match(value).hasMatch();
    if (plainName && value.contains(' ')) { youtubeChannelByName(value, done); return; }
    const QString searchName = value;
    if (channelId.match(value).hasMatch()) {
        path = "/channel/" + value;
    } else if (!value.contains('/')) {
        if (value.startsWith('@')) value.remove(0, 1);
        if (handle.match(value).hasMatch()) path = "/@" + value;
    } else {
        QUrl url(value.contains("://") ? value : "https://" + value);
        const auto host = url.host().toLower();
        const auto parts = url.path().split('/', Qt::SkipEmptyParts);
        if (url.scheme() == "https" && url.userInfo().isEmpty() && (url.port(-1) == -1 || url.port() == 443) &&
            (host == "youtube.com" || host == "www.youtube.com" || host == "m.youtube.com")) {
            if (parts.size() >= 1 && parts[0].startsWith('@') && handle.match(parts[0].mid(1)).hasMatch())
                path = "/" + parts[0];
            else if (parts.size() >= 2 && parts[0] == "channel" && channelId.match(parts[1]).hasMatch())
                path = "/channel/" + parts[1];
            else if (parts.size() >= 2 && (parts[0] == "c" || parts[0] == "user") && handle.match(parts[1]).hasMatch())
                path = "/" + parts[0] + "/" + parts[1];
        }
    }
    if (path.isEmpty()) {
        done({}, "Enter a YouTube channel handle or channel URL.");
        return;
    }
    QUrl base("https://www.youtube.com");
    base.setPath(path);
    QUrl streamsUrl(base); streamsUrl.setPath(path + "/streams");
    QUrl liveUrl(base); liveUrl.setPath(path + "/live");
    net->bytes(
        streamsUrl,
        [=, this](QByteArray bytes, QString error) {
            if (!error.isEmpty()) {
                if (plainName && error.contains("HTTP 404")) youtubeChannelByName(searchName, done);
                else done({}, error);
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
                                str(child["label"]) == "LIVE" ||
                                str(child["badgeStyle"]) == "THUMBNAIL_OVERLAY_BADGE_STYLE_LIVE";
                    });
                    if (live && videoId(id) == id && !seen.contains(id)) {
                        seen.insert(id);
                        QString title = textOf(item["title"]);
                        if (title.isEmpty()) title = textOf(at(item, {"metadata", "lockupMetadataViewModel", "title"}));
                        streams.append(QJsonObject{{"id", id}, {"title", title.isEmpty() ? id : title}});
                    }
                }
            });
            if (!streams.isEmpty()) {
                done({{"streams", streams}}, {});
                return;
            }
            net->bytes(
                liveUrl,
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
    if (s->isVod) return;
    if (s->platform != "twitch" && s->platform != "kick") {
        return;
    }
    liveChange(s->platform, s->id, s->name(), live, true);
}

void Hub::liveChange(QString login, QString name, bool live, bool opened) {
    liveChange("twitch", login, name, live, opened);
}

void Hub::liveChange(QString platform, QString login, QString name, bool live, bool opened) {
    login = login.toLower();
    const QString key = platform + ':' + login;
    bool first = !liveState.contains(key), old = liveState.value(key);
    liveState[key] = live;
    if (first || !live || old) {
        return;
    }
    auto a = cfg->feature("alerts");
    bool watch = opened && a[platform == "kick" ? "all_kick" : "all_twitch"].toBool(true);
    for (const auto &v : a["excluded"].toArray()) {
        if ((platform == "twitch" && v.toString().compare(login, Qt::CaseInsensitive) == 0) ||
            v.toString().compare(key, Qt::CaseInsensitive) == 0) {
            watch = false;
        }
    }
    for (const auto &v : a["watch"].toArray()) {
        auto item = v.toObject();
        if (item["platform"].toString("twitch") == platform &&
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
    pollKickLive();
    if (liveRequest || str(accounts->twitch["access_token"]).isEmpty()) {
        return;
    }
    QSet<QString> logins;
    for (auto s : sources) {
        if (s->platform == "twitch" && !s->isVod) {
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
                        source->header["started_at"] = stream["started_at"];
                        QString thumbnail = str(stream["thumbnail_url"]);
                        thumbnail.replace("{width}", "320").replace("{height}", "180");
                        source->header["thumbnail"] = thumbnail;
                        emit source->headerChanged();
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

void Hub::pollKickLive() {
    const auto alerts = cfg->feature("alerts");
    if (!alerts["enabled"].toBool(true) || QDateTime::currentMSecsSinceEpoch() < kickLiveRetryAt) return;
    QSet<QString> logins;
    if (alerts["all_kick"].toBool(true)) {
        for (auto *source : sources)
            if (source->platform == "kick" && !source->isVod && !source->closed && !source->kickConnecting) logins.insert(source->id);
    }
    for (const auto &value : alerts["watch"].toArray()) {
        const auto item = value.toObject();
        if (str(item["platform"]) != "kick") continue;
        const auto login = kickLogin("kick:" + str(item["id"]));
        if (!login.isEmpty()) logins.insert(login);
    }
    for (const auto &login : logins)
        if (!kickLivePending.contains(login) && !kickLiveQueue.contains(login)) kickLiveQueue << login;
    pumpKickLive();
}

void Hub::pumpKickLive() {
    if (QDateTime::currentMSecsSinceEpoch() < kickLiveRetryAt) return;
    while (kickLiveActive < 3 && !kickLiveQueue.isEmpty()) {
        const auto login = kickLiveQueue.takeFirst();
        kickLivePending.insert(login);
        ++kickLiveActive;
        QPointer<Hub> safe(this);
        net->json(QUrl("https://kick.com/api/v2/channels/" + login), [safe, login](QJsonObject data, QString error) {
            if (!safe) return;
            safe->kickLivePending.remove(login);
            --safe->kickLiveActive;
            if (error.contains("429")) {
                safe->kickLiveRetryAt = QDateTime::currentMSecsSinceEpoch() + 120000;
                safe->kickLiveQueue.clear();
            } else if (error.isEmpty() && !str(data["id"]).isEmpty()) {
                const auto stream = data["livestream"].toObject();
                auto *source = safe->sources.value("kick:" + login);
                if (source && !source->closed) {
                    source->header["live"] = !stream.isEmpty();
                    source->header["viewers"] = stream["viewer_count"];
                    QString started = str(stream["start_time"]);
                    if (started.isEmpty()) started = str(stream["created_at"]);
                    if (started.size() == 19 && started[10] == ' ') { started[10] = 'T'; started += 'Z'; }
                    if (stream.isEmpty() || (!str(source->header["started_at"]).isEmpty() &&
                                             !started.isEmpty() && str(source->header["started_at"]) != started)) {
                        source->header["thumbnail"] = QString();
                        source->kickThumbnailRetryAt = 0;
                    }
                    if (!stream.isEmpty() && str(source->header["thumbnail"]).isEmpty()) {
                        source->header["thumbnail"] = str(at(stream, {"thumbnail", "url"}));
                    }
                    if (!stream.isEmpty()) {
                        source->header["title"] = stream["session_title"];
                        source->header["started_at"] = started;
                        const auto categories = stream["categories"].toArray();
                        if (!categories.isEmpty()) source->header["category"] = categories[0].toObject()["name"];
                    }
                    emit source->headerChanged();
                }
                const QString name = source ? source->name() : str(at(data, {"user", "username"}));
                safe->liveChange("kick", login, name.isEmpty() ? login : name, !stream.isEmpty(), source != nullptr);
            }
            safe->pumpKickLive();
        });
    }
}
} // namespace yc
