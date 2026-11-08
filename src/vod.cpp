#include "vod.hpp"
#include <cmath>
#include <algorithm>

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
            if (!replay || token.isEmpty()) { self->fail("YouTube chat replay is not available for this video."); return; }
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
                self->fetch();
            }, "GET", {}, headers);
          }, "GET", {}, headers);
    } else if (platform == "kick") {
        const QUrl url("https://kick.com/api/v2/channels/" + channel);
        hub->net->json(url, [self, request, headers](QJsonObject data, QString error) {
            if (!self || self->epoch != request) return;
            if (!error.isEmpty()) { self->fail(error); return; }
            self->kickChannel = str(data["id"]).toLongLong();
            if (self->kickChannel <= 0) { self->fail("Kick channel was not found."); return; }
            const QUrl videos(QString("https://web.kick.com/api/v1/channels/%1/videos/%2").arg(self->kickChannel).arg(self->video));
            self->hub->net->json(videos, [self, request](QJsonObject result, QString videoError) {
                if (!self || self->epoch != request) return;
                if (!videoError.isEmpty()) { self->fail(videoError); return; }
                const auto item = result["data"].toObject();
                if (str(item["id"]).compare(self->video, Qt::CaseInsensitive) == 0) {
                    self->kickStart = iso(item["start_time"]);
                    self->kickDuration = qint64(item["duration"].toDouble()) * 1000;
                }
                if (!self->kickStart.isValid()) { self->fail("Kick did not return this VOD's start time."); return; }
                self->loading = false; self->ready = true; self->failures = 0;
                emit self->status("Connected to Kick VOD replay.");
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
        if (cursor.isEmpty()) vars["contentOffsetSeconds"] = double(nextOffset) / 1000.0;
        else vars["cursor"] = cursor;
        QJsonObject body{{"operationName", "VideoCommentsByOffsetOrCursor"}, {"variables", vars},
                         {"extensions", QJsonObject{{"persistedQuery", QJsonObject{{"version", 1}, {"sha256Hash", twitchQuery}}}}}};
        hub->net->json(QUrl("https://gql.twitch.tv/gql"), [self, request, finished](QJsonObject data, QString error) {
            if (!self || self->epoch != request) return;
            if (!error.isEmpty()) { finished({}, error); return; }
            if (!data["errors"].toArray().isEmpty()) { finished({}, "Twitch did not provide this replay."); return; }
            const auto vod = at(data, {"data", "video"}).toObject();
            if (vod.isEmpty()) { finished({}, "Twitch VOD or chat replay not found."); return; }
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
                    m.badges << badge;
                }
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

VodWindow::VodWindow(Hub *h, QWidget *parent)
    : QDialog(parent), hub(h), replay(h, this), server("YT-Chatterino VOD", QWebSocketServer::NonSecureMode, this) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(hub->cfg->T("VOD chat replay (Beta)"));
    setObjectName("vod-replay-window");
    resize(520, 620);
    auto *layout = new QVBoxLayout(this);
    auto *instructions = new QLabel(hub->cfg->T("Install the browser extension, open a VOD, and paste this code into the extension."), this);
    instructions->setWordWrap(true);
    layout->addWidget(instructions);
    auto *pairRow = new QHBoxLayout;
    pairing = new QLineEdit(this); pairing->setObjectName("vod-pairing-code"); pairing->setReadOnly(true);
    auto *copy = new QPushButton(hub->cfg->T("Copy code"), this);
    auto *folder = new QPushButton(hub->cfg->T("Extension folder"), this);
    auto *help = new QPushButton(hub->cfg->T("Installation help"), this);
    pairRow->addWidget(pairing, 1); pairRow->addWidget(copy); pairRow->addWidget(folder); pairRow->addWidget(help);
    layout->addLayout(pairRow);
    state = new QLabel(hub->cfg->T("Waiting for the browser extension…"), this);
    state->setObjectName("vod-status"); state->setWordWrap(true); state->setTextFormat(Qt::PlainText);
    layout->addWidget(state);
    view = new ChatView(hub, this); view->setObjectName("vod-chat");
    view->messageCap = hub->cfg->cap();
    layout->addWidget(view, 1);
    auto *hint = new QLabel(hub->cfg->T("Read-only beta: replay availability depends on the platform and the VOD."), this);
    hint->setWordWrap(true); hint->setStyleSheet("color:#999;"); layout->addWidget(hint);
    connect(&replay, &VodReplay::cleared, this, [this] { view->setMessages({}); });
    connect(&replay, &VodReplay::message, view, &ChatView::append);
    connect(&replay, &VodReplay::status, this, [this](const QString &text) { state->setText(hub->cfg->T(text)); });
    connect(copy, &QPushButton::clicked, this, [this] { QGuiApplication::clipboard()->setText(pairing->text()); });
    connect(help, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, hub->cfg->T("Installation help"), hub->cfg->T(
            "Chrome/Edge: open the browser's Extensions page, enable Developer mode, choose Load unpacked and select the extension folder.\n\n"
            "Firefox: open about:debugging, choose This Firefox and Load Temporary Add-on, then select manifest.json. Firefox removes temporary add-ons when it closes.\n\n"
            "Open a VOD, click the extension icon, paste the pairing code and connect this tab."));
    });
    connect(folder, &QPushButton::clicked, this, [] {
        const QDir app(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
        const auto path = app.filePath("browser-extension");
#else
        const auto path = app.filePath("../share/yt-chatterino/browser-extension");
#endif
        QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::cleanPath(path)));
    });
    QByteArray random;
    for (int i = 0; i < 8; ++i) {
        const quint32 n = QRandomGenerator::system()->generate();
        for (int shift = 0; shift < 32; shift += 8) random.append(char((n >> shift) & 0xff));
    }
    secret = QString::fromLatin1(random.toHex());
    server.setMaxPendingConnections(8);
    connect(&server, &QWebSocketServer::newConnection, this, &VodWindow::acceptConnection);
    if (server.listen(QHostAddress::LocalHost, 0)) {
        pairing->setText(QString::number(server.serverPort()) + ':' + secret);
    } else {
        pairing->setText(hub->cfg->T("Local connection unavailable"));
        copy->setEnabled(false);
    }
    darkTitlebar(this);
}

VodWindow::~VodWindow() {
    if (peer) peer->close();
    server.close();
}

void VodWindow::acceptConnection() {
    while (server.hasPendingConnections()) {
        auto *socket = server.nextPendingConnection();
        if (!socket) continue;
        socket->setParent(this);
        if (!socket->peerAddress().isLoopback() || peer) { socket->close(); socket->deleteLater(); continue; }
        socket->setMaxAllowedIncomingFrameSize(8192);
        socket->setMaxAllowedIncomingMessageSize(8192);
        auto authenticated = QSharedPointer<bool>::create(false);
        QTimer::singleShot(5000, socket, [socket, authenticated] { if (!*authenticated) socket->close(); });
        connect(socket, &QWebSocket::textMessageReceived, this, [this, socket, authenticated](const QString &text) {
            if (text.size() > 8192) { socket->close(); return; }
            const auto data = QJsonDocument::fromJson(text.toUtf8()).object();
            if (!*authenticated) {
                if (peer || str(data["type"]) != "hello" || str(data["token"]) != secret) { socket->close(); return; }
                *authenticated = true; peer = socket;
                socket->sendTextMessage(QStringLiteral("{\"type\":\"ready\"}"));
                state->setText(hub->cfg->T("Browser connected; waiting for a VOD…"));
                return;
            }
            if (str(data["type"]) != "state" || !data["seconds"].isDouble() || !data["paused"].isBool()) return;
            const double seconds = data["seconds"].toDouble(-1);
            if (!std::isfinite(seconds) || seconds < 0 || seconds > 7 * 24 * 3600) return;
            replay.playback(QUrl(str(data["url"])), qint64(seconds * 1000), data["paused"].toBool(), data["seek"].toBool());
        });
        connect(socket, &QWebSocket::disconnected, this, [this, socket] {
            if (peer == socket) {
                peer = nullptr;
                state->setText(hub->cfg->T("Browser disconnected. Reconnect using the extension."));
            }
            socket->deleteLater();
        });
    }
}
} // namespace yc
