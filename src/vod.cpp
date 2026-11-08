#include "vod.hpp"
#include <cmath>
#include <algorithm>
#include <QtWebSockets/QWebSocketCorsAuthenticator>

namespace yc {
namespace {
const QByteArray browserAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36";
const QString twitchWebClient = "kd1unb4b3q4t58fwlpcbzcbnm76a8fp";
const QString twitchQuery = "b70a3591ff0f4e0313d126c6a1502d79a1c02baebb288227c582044aa76adf6a";
QJsonObject youtubeConfig(const QString &html) {
    QJsonObject result;
    qsizetype offset = 0;
    while ((offset = html.indexOf("ytcfg.set(", offset)) >= 0) {
        auto part = embeddedJson(html.mid(offset), "ytcfg.set(");
        for (auto i = part.begin(); i != part.end(); ++i) result[i.key()] = i.value();
        offset += 10;
    }
    return result;
}
QString continuation(const QJsonObject &root, const QString &type) {
    QString found;
    walk(root, [&](const QJsonObject &o) {
        const auto entry = o[type].toObject();
        if (found.isEmpty()) found = str(entry["continuation"]);
    });
    return found;
}

// Twitch's public replay query omits USERNOTICE tags. Recognize only its known,
// complete English system-message forms; arbitrary chatter must remain ordinary text.
void classifyTwitchReplayAlert(Message &m) {
    if (m.name.isEmpty() || !m.emotes.isEmpty()) return;
    const QString text = m.text.trimmed();
    const QString prefix = m.name + ' ';
    if (!text.startsWith(prefix, Qt::CaseInsensitive)) return;
    const QString notice = text.mid(prefix.size());
    static const QRegularExpression subscription(
        R"(^subscribed (with Prime|at Tier [123])\.(?: They've subscribed for \d+ months?(?:, currently on a \d+ month streak)?!)?$)",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression gift(
        R"(^(?:is gifting \d+ Tier [123] Subs to .+ community!|gifted a Tier [123] sub to [^\s!]+!)(?: They've gifted a total of \d+ in the channel!)?$)",
        QRegularExpression::CaseInsensitiveOption);
    const auto sub = subscription.match(notice);
    if (sub.hasMatch()) m.alertKind = sub.captured(1).compare("with Prime", Qt::CaseInsensitive) == 0 ? "prime" : "subscription";
    else if (gift.match(notice).hasMatch()) m.alertKind = "gift";
    else return;
    m.special = "alert"; m.alertText = notice; m.alertBodyStart = m.text.size();
}
QDateTime iso(const QJsonValue &value) {
    auto d = QDateTime::fromString(str(value), Qt::ISODateWithMs);
    if (!d.isValid()) d = QDateTime::fromString(str(value), Qt::ISODate);
    return d;
}
} // namespace

VodReplay::VodReplay(Hub *h, QObject *p) : QObject(p), hub(h) {
    retry.setSingleShot(true);
    connect(&retry, &QTimer::timeout, this, [this] { if (!ready) loadMetadata(); else fetch(); });
}

bool VodReplay::identify(const QUrl &u, QString *p, QString *v, QString *c) {
    if (u.scheme() != "https" || !u.userInfo().isEmpty() || (u.port(-1) != -1 && u.port() != 443)) return false;
    const QString host = u.host().toLower();
    QString platform, video, channel;
    if ((host == "www.youtube.com" || host == "youtube.com" || host == "m.youtube.com") && u.path() == "/watch") {
        video = QUrlQuery(u).queryItemValue("v");
        if (!QRegularExpression("^[A-Za-z0-9_-]{11}$").match(video).hasMatch()) return false;
        platform = "youtube";
    } else if (host == "www.twitch.tv" || host == "twitch.tv" || host == "m.twitch.tv") {
        auto match = QRegularExpression("^/videos/([0-9]{1,20})/?$").match(u.path());
        if (!match.hasMatch()) return false;
        platform = "twitch"; video = match.captured(1);
    } else if (host == "kick.com" || host == "www.kick.com") {
        auto match = QRegularExpression("^/([A-Za-z0-9_-]{1,80})/videos/([A-Fa-f0-9-]{36})/?$").match(u.path());
        if (!match.hasMatch() || QUuid(match.captured(2)).isNull()) return false;
        platform = "kick"; channel = match.captured(1).toLower(); video = match.captured(2).toLower();
    } else return false;
    if (p) *p = platform;
    if (v) *v = video;
    if (c) *c = channel;
    return true;
}

QString VodReplay::sourceKey(const QUrl &url) {
    QString platform, video, channel;
    if (!identify(url, &platform, &video, &channel)) return {};
    return "vod:" + platform + ':' + video;
}

void VodReplay::playback(const QUrl &url, qint64 offsetMs, bool isPaused, bool seek) {
    QString p, v, c;
    if (!identify(url, &p, &v, &c) || offsetMs < 0 || offsetMs > 7LL * 24 * 3600 * 1000) {
        emit status("Open a supported VOD in the selected browser tab.");
        return;
    }
    const bool changed = p != platform || v != video || c != channel;
    const qint64 elapsed = wall.isValid() ? wall.elapsed() : 0;
    // Background browser tabs may throttle their reports; allow the elapsed real time before treating a jump as a seek.
    const qint64 drift = qAbs(offsetMs - position);
    const bool jumped = initialized && (seek || drift > qMax<qint64>(3500, elapsed * 4 + 2000));
    if (changed || jumped || !initialized) {
        ++epoch; retry.stop(); loading = false; ended = false; failures = 0;
        pending.clear(); seen.clear(); cursor.clear(); lastCursor.clear();
        nextOffset = qMax<qint64>(0, offsetMs - 10000);
        if (changed) {
            platform = p; video = v; channel = c; ready = false;
            youtubeToken.clear(); youtubeContext = {}; kickStart = {}; kickChannel = kickDuration = 0;
        }
        initialized = true;
        emit cleared();
    }
    position = offsetMs; paused = isPaused; wall.restart();
    deliver();
    if (!loading && !retry.isActive()) {
        if (!ready) loadMetadata(); else fetch();
    }
}

void VodReplay::disconnectPlayback() {
    ++epoch;
    retry.stop();
    loading = false;
    initialized = false;
}

void VodReplay::fail(const QString &error) {
    loading = false;
    ++failures;
    emit status("Replay unavailable or temporarily limited: " + error.left(220));
    // No tight retry loop when the provider is rejecting requests.
    retry.start(qMin(60000, 3000 * (1 << qMin(4, failures - 1))));
}

void VodReplay::loadMetadata() {
    if (loading || !initialized) return;
    if (platform == "twitch") {
        ready = true; failures = 0; emit status("Connected to Twitch VOD replay."); fetch(); return;
    }
    loading = true;
    const quint64 request = epoch;
    QPointer<VodReplay> self(this);
    const QMap<QByteArray, QByteArray> headers{{"User-Agent", browserAgent}};
    if (platform == "youtube") {
        hub->net->bytes(Network::query("https://www.youtube.com/watch", {{"v", video}}),
          [self, request, headers](QByteArray bytes, QString error) {
            if (!self || self->epoch != request) return;
            if (!error.isEmpty()) { self->fail(error); return; }
            const auto html = QString::fromUtf8(bytes);
            const auto data = embeddedJson(html, "ytInitialData");
            QString token;
            bool replay = false;
            walk(data, [&](const QJsonObject &node) {
                auto chat = node["liveChatRenderer"].toObject();
                if (chat.isEmpty() || !chat["isReplay"].toBool()) return;
                replay = true;
                if (token.isEmpty()) token = continuation(chat, "reloadContinuationData");
            });
            if (!replay || token.isEmpty()) {
                self->loading = false; self->ended = true;
                emit self->status("YouTube chat replay is not available for this video.");
                emit self->unavailable(); return;
            }
            const auto videoDetails = embeddedJson(html, "ytInitialPlayerResponse")["videoDetails"].toObject();
            QJsonObject metadata{{"channel", videoDetails["author"]}, {"channel_id", videoDetails["channelId"]},
                                 {"title", videoDetails["title"]}};
            walk(data, [&](const QJsonObject &node) {
                const auto owner = node["videoOwnerRenderer"].toObject();
                if (!owner.isEmpty() && str(metadata["avatar"]).isEmpty()) metadata["avatar"] = imageUrls(owner["thumbnail"]).value(0);
            });
            emit self->metadata(metadata);
            const auto page = Network::query("https://www.youtube.com/live_chat_replay", {{"continuation", token}});
            self->hub->net->bytes(page, [self, request](QByteArray raw, QString pageError) {
                if (!self || self->epoch != request) return;
                if (!pageError.isEmpty()) { self->fail(pageError); return; }
                const auto htmlPage = QString::fromUtf8(raw);
                const auto initial = embeddedJson(htmlPage, "ytInitialData");
                self->youtubeToken = continuation(initial, "liveChatReplayContinuationData");
                self->youtubeContext = youtubeConfig(htmlPage)["INNERTUBE_CONTEXT"].toObject();
                if (self->youtubeToken.isEmpty() || self->youtubeContext.isEmpty()) {
                    self->fail("YouTube did not provide a replay continuation."); return;
                }
                self->loading = false; self->ready = true; self->failures = 0;
                emit self->status("Connected to YouTube VOD replay.");
                if (!self->announced) { self->announced = true; emit self->available(); }
                self->fetch();
            }, "GET", {}, headers);
          }, "GET", {}, headers);
    } else if (platform == "kick") {
        const QUrl url("https://kick.com/api/v2/channels/" + channel);
        hub->net->json(url, [self, request, headers](QJsonObject data, QString error) {
            if (!self || self->epoch != request) return;
            if (!error.isEmpty()) { self->fail(error); return; }
            self->kickChannel = str(data["id"]).toLongLong();
            const auto user = data["user"].toObject();
            emit self->metadata(QJsonObject{{"channel_id", user["id"]}, {"channel", user["username"]},
                                            {"avatar", user["profile_pic"]}, {"kick_badges", data["subscriber_badges"]}});
            if (self->kickChannel <= 0) { self->fail("Kick channel was not found."); return; }
            const QUrl videos(QString("https://web.kick.com/api/v1/channels/%1/videos/%2").arg(self->kickChannel).arg(self->video));
            self->hub->net->json(videos, [self, request](QJsonObject result, QString videoError) {
                if (!self || self->epoch != request) return;
                if (!videoError.isEmpty()) { self->fail(videoError); return; }
                const auto item = result["data"].toObject();
                if (str(item["id"]).compare(self->video, Qt::CaseInsensitive) == 0) {
                    self->kickStart = iso(item["start_time"]);
                    self->kickDuration = qint64(item["duration"].toDouble()) * 1000;
                    emit self->metadata(QJsonObject{{"title", item["title"]}});
                }
                if (!self->kickStart.isValid()) { self->fail("Kick did not return this VOD's start time."); return; }
                self->loading = false; self->ready = true; self->failures = 0;
                emit self->status("Connected to Kick VOD replay.");
                if (!self->announced) { self->announced = true; emit self->available(); }
                self->fetch();
            }, "GET", {}, headers);
        }, "GET", {}, headers);
    }
}

void VodReplay::fetch() {
    if (loading || !ready || ended || !initialized || pending.size() >= 5000 || nextOffset > position + 10000) return;
    loading = true;
    const quint64 request = epoch;
    QPointer<VodReplay> self(this);
    auto finished = [self, request](QVector<Item> items, QString error) {
        if (!self || self->epoch != request) return;
        if (!error.isEmpty()) { self->fail(error); return; }
        self->loading = false; self->failures = 0;
        self->process(std::move(items));
        if (!self->ended && self->nextOffset <= self->position + 10000) self->retry.start(300);
    };
    if (platform == "twitch") {
        QJsonObject vars{{"videoID", video}};
        if (cursor.isEmpty()) vars["contentOffsetSeconds"] = int(nextOffset / 1000);
        else vars["cursor"] = cursor;
        QJsonObject body{{"operationName", "VideoCommentsByOffsetOrCursor"}, {"variables", vars},
                         {"extensions", QJsonObject{{"persistedQuery", QJsonObject{{"version", 1}, {"sha256Hash", twitchQuery}}}}}};
        hub->net->json(QUrl("https://gql.twitch.tv/gql"), [self, request, finished](QJsonObject data, QString error) {
            if (!self || self->epoch != request) return;
            if (!error.isEmpty()) { finished({}, error); return; }
            if (!data["errors"].toArray().isEmpty()) { finished({}, "Twitch did not provide this replay."); return; }
            const auto vod = at(data, {"data", "video"}).toObject();
            if (vod.isEmpty()) { finished({}, "Twitch VOD or chat replay not found."); return; }
            const auto creator = vod["creator"].toObject();
            if (!str(creator["id"]).isEmpty()) emit self->metadata(QJsonObject{{"channel_id", creator["id"]}});
            if (!self->announced) { self->announced = true; emit self->available(); }
            const auto comments = vod["comments"].toObject();
            QVector<Item> items;
            qint64 last = self->nextOffset;
            QString nextCursor;
            for (const auto &edgeValue : comments["edges"].toArray()) {
                const auto edge = edgeValue.toObject(), node = edge["node"].toObject();
                if (node.isEmpty()) continue;
                nextCursor = str(edge["cursor"]);
                const auto author = node["commenter"].toObject(), body = node["message"].toObject();
                Message m; m.id = str(node["id"]); m.name = str(author["displayName"]);
                m.login = str(author["login"]); m.uid = str(author["id"]); m.color = str(body["userColor"]);
                m.source = "twitch:" + self->channel; m.history = true; m.stamp = iso(node["createdAt"]);
                for (const auto &fragmentValue : body["fragments"].toArray()) {
                    auto fragment = fragmentValue.toObject();
                    QString text = str(fragment["text"]); auto e = fragment["emote"].toObject();
                    if (!e.isEmpty()) {
                        Span s; s.start = m.text.size(); s.end = s.start + text.size(); s.code = text;
                        s.urls << "https://static-cdn.jtvnw.net/emoticons/v2/" + str(e["emoteID"]) + "/default/dark/2.0";
                        m.emotes << s;
                    }
                    m.text += text;
                }
                for (const auto &badgeValue : body["userBadges"].toArray()) {
                    const auto b = badgeValue.toObject(); Badge badge;
                    badge.id = str(b["setID"]); badge.version = str(b["version"]); badge.role = badge.id;
                    if (badge.id == "broadcaster") badge.role = "owner";
                    if (badge.id == "moderator") badge.role = "moderator";
                    if (!badge.id.isEmpty() && !badge.version.isEmpty()) m.badges << badge;
                }
                classifyTwitchReplayAlert(m);
                const qint64 offset = qMax<qint64>(0, qint64(node["contentOffsetSeconds"].toDouble() * 1000));
                m.replayOffsetMs = offset; last = qMax(last, offset);
                if (!m.id.isEmpty() && !m.name.isEmpty()) items << Item{offset, m};
            }
            const bool more = comments["pageInfo"].toObject()["hasNextPage"].toBool();
            if (!more) self->ended = true;
            else if (nextCursor.isEmpty() || nextCursor == self->cursor) { finished({}, "Twitch replay cursor did not advance."); return; }
            self->cursor = nextCursor; self->nextOffset = last;
            finished(items, {});
        }, "POST", body, {{"Client-ID", twitchWebClient.toUtf8()}, {"Origin", "https://www.twitch.tv"}, {"User-Agent", browserAgent}});
    } else if (platform == "youtube") {
        const QJsonObject body{{"context", youtubeContext}, {"continuation", youtubeToken},
                               {"currentPlayerState", QJsonObject{{"playerOffsetMs", QString::number(nextOffset)}}}};
        hub->net->json(QUrl("https://www.youtube.com/youtubei/v1/live_chat/get_live_chat_replay?prettyPrint=false"),
          [self, request, finished](QJsonObject data, QString error) {
            if (!self || self->epoch != request) return;
            if (!error.isEmpty()) { finished({}, error); return; }
            QJsonObject live;
            walk(data, [&](const QJsonObject &o) { if (live.isEmpty() && o.contains("liveChatContinuation")) live = o["liveChatContinuation"].toObject(); });
            if (live.isEmpty()) { finished({}, "YouTube did not return a chat replay."); return; }
            QVector<Item> items;
            qint64 last = self->nextOffset - 1;
            for (const auto &actionValue : live["actions"].toArray()) {
                const auto replay = actionValue.toObject()["replayChatItemAction"].toObject();
                bool ok = false; const qint64 offset = str(replay["videoOffsetTimeMsec"]).toLongLong(&ok);
                if (!ok) continue;
                last = qMax(last, offset);
                for (const auto &inner : replay["actions"].toArray()) {
                    const auto item = at(inner.toObject(), {"addChatItemAction", "item"}).toObject();
                    for (auto it = item.begin(); it != item.end(); ++it) {
                        if (!it.key().endsWith("Renderer")) continue;
                        auto m = parseYouTube(it.value().toObject(), it.key(), true);
                        m.source = self->video; m.replayOffsetMs = offset;
                        if (!m.id.isEmpty()) items << Item{offset, m};
                    }
                }
            }
            self->nextOffset = last >= self->nextOffset ? last + 1 : self->nextOffset + 5000;
            finished(items, {});
        }, "POST", body, {{"User-Agent", browserAgent}, {"Origin", "https://www.youtube.com"}});
    } else if (platform == "kick") {
        const qint64 absolute = kickStart.toMSecsSinceEpoch() + nextOffset;
        const qint64 aligned = absolute - absolute % 5000;
        const QString start = QDateTime::fromMSecsSinceEpoch(aligned).toUTC().toString(Qt::ISODate);
        const auto url = Network::query(QString("https://web.kick.com/api/v1/chat/%1/history").arg(kickChannel), {{"start_time", start}});
        hub->net->json(url, [self, request, finished, aligned](QJsonObject data, QString error) {
            if (!self || self->epoch != request) return;
            if (!error.isEmpty()) { finished({}, error); return; }
            QVector<Item> items;
            for (const auto &entry : data["data"].toObject()["messages"].toArray()) {
                auto m = parseKick(entry.toObject(), true);
                if (!m.stamp.isValid()) continue;
                const qint64 offset = self->kickStart.msecsTo(m.stamp);
                if (offset < 0 || (self->kickDuration > 0 && offset > self->kickDuration)) continue;
                m.source = "kick:" + self->channel; m.replayOffsetMs = offset;
                if (!m.id.isEmpty()) items << Item{offset, m};
            }
            self->nextOffset = qMax(self->nextOffset + 1, aligned + 5000 - self->kickStart.toMSecsSinceEpoch());
            if (self->kickDuration > 0 && self->nextOffset > self->kickDuration) self->ended = true;
            finished(items, {});
        }, "GET", {}, {{"User-Agent", browserAgent}, {"Accept", "application/json"}});
    }
}

void VodReplay::process(QVector<Item> items) {
    for (auto &item : items) {
        if (seen.contains(item.message.id)) continue;
        seen.insert(item.message.id);
        pending << item;
    }
    std::stable_sort(pending.begin(), pending.end(), [](const Item &a, const Item &b) { return a.offset < b.offset; });
    // Stop requesting more pages when the future-message buffer reaches its limit.
    if (seen.size() > 30000) {
        seen.clear();
        for (const auto &item : pending) seen.insert(item.message.id);
    }
    deliver();
}

void VodReplay::deliver() {
    int count = 0;
    while (count < pending.size() && pending[count].offset <= position) {
        emit message(pending[count].message);
        ++count;
    }
    if (count) pending.remove(0, count);
}


namespace {
QHash<QString, Badge> replayBadgeMap(const QJsonArray &items) {
    QHash<QString, Badge> result;
    for (const auto &value : items) {
        const auto data = value.toObject();
        Badge badge; badge.id = str(data["setID"]); badge.version = str(data["version"]);
        badge.label = str(data["title"]); badge.role = badge.id == "broadcaster" ? "owner" : badge.id;
        const QString url = str(data["imageURL"]);
        if (!badge.id.isEmpty() && !badge.version.isEmpty() && Images::allowed(QUrl(url))) {
            badge.urls << url;
            result.insert(badge.id + '/' + badge.version, badge);
        }
    }
    return result;
}
QMap<QByteArray,QByteArray> twitchReplayHeaders() {
    return {{"Client-ID", twitchWebClient.toUtf8()}, {"Origin", "https://www.twitch.tv"}, {"User-Agent", browserAgent}};
}
} // namespace

void Hub::applyVodBadges(Source *source) {
    if (!source || source->closed || !source->isVod || source->platform != "twitch") return;
    const QString room = source->room();
    source->badges = vodGlobalBadges;
    const auto channelBadges = vodChannelBadges.value(room);
    for (auto it = channelBadges.cbegin(); it != channelBadges.cend(); ++it) source->badges[it.key()] = it.value();
    const auto profile = vodBadgeProfiles.value(room);
    if (!str(profile["displayName"]).isEmpty()) source->header["channel"] = profile["displayName"];
    if (!str(profile["profileImageURL"]).isEmpty()) source->header["avatar"] = profile["profileImageURL"];
    emit source->changed();
}

void Hub::loadVodBadges(Source *source) {
    if (!source || source->closed || !source->isVod || source->platform != "twitch") return;
    QPointer<Hub> self(this); QPointer<Source> safe(source);
    auto retry = [self, safe] {
        if (self && safe && !safe->closed) QTimer::singleShot(30000, safe, [self, safe] {
            if (self && safe && !safe->closed) self->loadVodBadges(safe);
        });
    };
    if (!vodGlobalBadgesReady && !vodGlobalBadgesPending) {
        vodGlobalBadgesPending = true;
        QJsonObject body{{"query", "query{badges{imageURL(size:DOUBLE),title,setID,version}}"}};
        net->json(QUrl("https://gql.twitch.tv/gql"), [self, retry](QJsonObject data, QString error) {
            if (!self) return;
            self->vodGlobalBadgesPending = false;
            const auto values = at(data, {"data", "badges"});
            if (!error.isEmpty() || !values.isArray()) { retry(); return; }
            self->vodGlobalBadges = replayBadgeMap(values.toArray()); self->vodGlobalBadgesReady = true;
            for (auto *candidate : self->sources) self->applyVodBadges(candidate);
        }, "POST", body, twitchReplayHeaders());
    }
    const QString room = source->room();
    if (room.isEmpty() || vodChannelBadges.contains(room) || vodBadgePending.contains(room)) {
        applyVodBadges(source); return;
    }
    vodBadgePending.insert(room);
    QJsonObject body{{"query", "query($id:ID!){user(id:$id){id,login,displayName,profileImageURL(width:70),broadcastBadges{imageURL(size:DOUBLE),title,setID,version}}}"},
                     {"variables", QJsonObject{{"id", room}}}};
    net->json(QUrl("https://gql.twitch.tv/gql"), [self, room, retry](QJsonObject data, QString error) {
        if (!self) return;
        self->vodBadgePending.remove(room);
        const auto user = at(data, {"data", "user"}).toObject();
        const auto badges = user["broadcastBadges"];
        if (!error.isEmpty() || !badges.isArray()) { retry(); return; }
        self->vodChannelBadges.insert(room, replayBadgeMap(badges.toArray()));
        self->vodBadgeProfiles.insert(room, user);
        for (auto *candidate : self->sources)
            if (candidate->isVod && candidate->room() == room) self->applyVodBadges(candidate);
    }, "POST", body, twitchReplayHeaders());
}

Source *Hub::vodSource(const QUrl &url) {
    QString platform, video, channel;
    if (!VodReplay::identify(url, &platform, &video, &channel)) return nullptr;
    const QString key = VodReplay::sourceKey(url);
    if (sources.contains(key)) return sources.value(key);
    auto *source = new Source(this, platform, video, true);
    source->vodUrl = platform == "youtube" ? Network::query("https://www.youtube.com/watch", {{"v", video}})
                      : platform == "twitch" ? QUrl("https://www.twitch.tv/videos/" + video)
                      : QUrl("https://kick.com/" + channel + "/videos/" + video);
    knownVodUrls.insert(key, source->vodUrl);
    source->header = {{"channel", channel.isEmpty() ? video : channel}, {"title", "VOD"},
                      {"vod_connected", false}};
    source->vodReplay = new VodReplay(this, source);
    sources.insert(key, source);
    connect(source, &Source::changed, this, [this, source] { emit sourceChanged(source); });
    connect(source, &Source::headerChanged, this, [this, source] { emit sourceChanged(source); });
    connect(source->vodReplay, &VodReplay::cleared, source, [source] {
        source->history.clear(); source->seen.clear(); source->seenOrder.clear(); ++source->vodRevision; emit source->changed();
    });
    connect(source->vodReplay, &VodReplay::message, source, [source](Message message) {
        message.history = true;
        // Mixed replays from different dates should appear in playback-arrival order.
        message.stamp = QDateTime::currentDateTime();
        source->publish(message);
    });
    connect(source->vodReplay, &VodReplay::status, source, [source](const QString &message) {
        if (str(source->header["vod_status"]) == message) return;
        source->header["vod_status"] = message; emit source->headerChanged();
    });
    connect(source->vodReplay, &VodReplay::metadata, source, [this, source](const QJsonObject &data) {
        const QString oldRoom = source->room();
        bool changed = false;
        for (const auto &key : QStringList{"channel", "channel_id", "title", "avatar"}) {
            const auto value = str(data[key]);
            if (!value.isEmpty() && str(source->header[key]) != value) { source->header[key] = value; changed = true; }
        }
        if (data["kick_badges"].isArray()) {
            for (const auto &value : data["kick_badges"].toArray()) {
                const auto badge = value.toObject(); const int months = badge["months"].toInt();
                const auto image = str(at(badge, {"badge_image", "src"}));
                if (months > 0 && Images::allowed(QUrl(image))) source->kickSubscriberBadges[months] = image;
            }
        }
        if (changed) emit source->headerChanged();
        if (source->platform == "twitch" && !source->room().isEmpty() && oldRoom != source->room()) {
            loadVodBadges(source);
            source->loadEmotes(source->room());
            const auto video = source->id; QPointer<Source> safe(source);
            const QJsonObject body{{"query", "query($id:ID!){video(id:$id){title}}"}, {"variables", QJsonObject{{"id", video}}}};
            net->json(QUrl("https://gql.twitch.tv/gql"), [safe](QJsonObject result, QString error) {
                if (!safe || !error.isEmpty()) return;
                const auto title = str(at(result, {"data", "video", "title"}));
                if (!title.isEmpty()) { safe->header["title"] = title; emit safe->headerChanged(); }
            }, "POST", body, twitchReplayHeaders());
        }
    });
    return source;
}

VodBridge::VodBridge(Hub *h, QObject *parent, quint16 port, bool listen)
    : QObject(parent), hub(h), server("YT-Chatterino VOD Sync", QWebSocketServer::NonSecureMode, this) {
    server.setMaxPendingConnections(8);
    connect(&server, &QWebSocketServer::originAuthenticationRequired, this, [](QWebSocketCorsAuthenticator *auth) {
        auth->setAllowed(allowedOrigin(auth->origin()));
    });
    connect(&server, &QWebSocketServer::newConnection, this, &VodBridge::acceptConnection);
    if (listen) server.listen(QHostAddress::LocalHost, port);
}

bool VodBridge::allowedOrigin(const QString &origin) {
    const QUrl url(origin);
    if (!url.isValid() || !url.userInfo().isEmpty() || url.port(-1) != -1 ||
        (!url.path().isEmpty() && url.path() != "/") || url.hasQuery() || url.hasFragment()) return false;
    if (url.scheme() == "chrome-extension") return QRegularExpression("^[a-p]{32}$").match(url.host()).hasMatch();
    if (url.scheme() == "moz-extension") return !QUuid(url.host()).isNull();
    return false;
}

void VodBridge::hook(Source *source) {
    if (!source || hooked.contains(source->key)) return;
    const QString key = source->key; hooked.insert(key);
    connect(source, &QObject::destroyed, this, [this, key] { hooked.remove(key); });
    connect(source->vodReplay, &VodReplay::available, source, [this, source] {
        if (!source->closed && !suppressed.contains(source->key)) {
            if (!source->header["vod_shown"].toBool()) source->vodReplay->disconnectPlayback();
            emit discovered(source);
        }
    });
    connect(source->vodReplay, &VodReplay::unavailable, source, [this, source] {
        if (source->viewCaps.isEmpty()) { suppressed.insert(source->key); hub->remove(source); }
    });
}

void VodBridge::suppress(const QString &key) { suppressed.insert(key); }
void VodBridge::allow(const QString &key) { suppressed.remove(key); }

void VodBridge::removePlayer(const QString &player, bool navigation) {
    const auto key = players.take(player);
    if (key.isEmpty()) return;
    if (owners.value(key) == player) {
        owners.remove(key);
        if (auto *source = hub->sources.value(key); source && source->isVod) {
            source->header["vod_connected"] = false;
            source->header["vod_status"] = "Waiting for the VOD in your browser…";
            if (source->vodReplay) source->vodReplay->disconnectPlayback();
            emit source->headerChanged();
        }
    }
    if (!players.values().contains(key)) {
        auto *source = hub->sources.value(key);
        if (source && source->isVod && !source->header["vod_shown"].toBool()) hub->remove(source);
    }
    if (navigation && !players.values().contains(key)) suppressed.remove(key);
}

void VodBridge::disconnectClient(const QString &client) {
    const auto prefix = client + ':';
    const auto keys = players.keys();
    for (const auto &key : keys) if (key.startsWith(prefix)) removePlayer(key, false);
}

void VodBridge::processState(const QString &client, const QJsonObject &data) {
    if (!data["tab"].isDouble() || !data["seconds"].isDouble() || !data["paused"].isBool()) return;
    const double tabValue = data["tab"].toDouble(-1), seconds = data["seconds"].toDouble(-1);
    if (!std::isfinite(tabValue) || tabValue < 0 || tabValue > 2147483647 || std::floor(tabValue) != tabValue ||
        !std::isfinite(seconds) || seconds < 0 || seconds > 7 * 24 * 3600) return;
    const QString player = client + ':' + QString::number(int(tabValue));
    const QUrl url(str(data["url"])); const QString key = VodReplay::sourceKey(url);
    if (key.isEmpty()) { removePlayer(player, true); return; }
    if (!players.contains(player) && players.size() >= 32) return;
    if (players.contains(player) && players.value(player) != key) removePlayer(player, true);
    players[player] = key;
    if (suppressed.contains(key)) return;
    if (owners.contains(key) && owners.value(key) != player) return;
    auto *source = hub->vodSource(url);
    if (!source || !source->vodReplay) return;
    owners[key] = player; hook(source);
    if (!source->header["vod_connected"].toBool()) {
        source->header["vod_connected"] = true; emit source->headerChanged();
    }
    // Discover a replay once, then fetch messages only while a pane displays it.
    if (source->vodReplay->announced && !source->header["vod_shown"].toBool()) return;
    source->vodReplay->playback(url, qint64(seconds * 1000), data["paused"].toBool(), data["seek"].toBool());
}

void VodBridge::acceptConnection() {
    while (server.hasPendingConnections()) {
        auto *socket = server.nextPendingConnection(); if (!socket) continue;
        socket->setParent(this);
        if (!socket->peerAddress().isLoopback() || !allowedOrigin(socket->origin()) || clients.size() >= 8) {
            socket->close(); socket->deleteLater(); continue;
        }
        socket->setMaxAllowedIncomingFrameSize(8192); socket->setMaxAllowedIncomingMessageSize(8192);
        QTimer::singleShot(5000, socket, [this, socket] { if (!clients.contains(socket)) socket->close(); });
        connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &text) {
            if (text.size() > 8192) { socket->close(); return; }
            const auto data = QJsonDocument::fromJson(text.toUtf8()).object();
            if (!clients.contains(socket)) {
                const QString id = str(data["client"]);
                if (str(data["type"]) != "hello" || QUuid(id).isNull() || clients.values().contains(id)) { socket->close(); return; }
                clients.insert(socket, id); socket->sendTextMessage(QStringLiteral("{\"type\":\"ready\"}")); return;
            }
            const auto client = clients.value(socket), type = str(data["type"]);
            if (type == "state") processState(client, data);
            else if (type == "closed" && data["tab"].isDouble())
                removePlayer(client + ':' + QString::number(data["tab"].toInt(-1)), true);
        });
        connect(socket, &QWebSocket::disconnected, this, [this, socket] {
            const auto client = clients.take(socket); if (!client.isEmpty()) disconnectClient(client);
            socket->deleteLater();
        });
    }
}
} // namespace yc
