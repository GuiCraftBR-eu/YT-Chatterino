#include "native_tests.hpp"

static QJsonObject kickMessage(QString id, QString time, QString content = "hello") {
    return {{"id", id}, {"content", content}, {"created_at", time},
            {"sender", QJsonObject{{"id", 42}, {"username", "TestUser"}, {"slug", "testuser"},
                {"identity", QJsonObject{{"color", "#aabbcc"}}}}}};
}

void NativeTests::kickLinksAndParser() {
    QCOMPARE(kickLogin("https://kick.com/WestCol"), QString("westcol"));
    QCOMPARE(kickLogin("kick:@Example_Name"), QString("example_name"));
    QCOMPARE(kickLogin("https://kick.com/popout/westcol/chat"), QString("westcol"));
    QCOMPARE(platformCharacterCount(QString::fromUtf8("é"), "kick"), 1);
    QVERIFY(kickLogin("https://kick.com.evil.test/westcol").isEmpty());
    QVERIFY(kickLogin("https://kick.com/settings").isEmpty());
    QCOMPARE(browserUrl("kick", "westcol", "player"), QString("https://player.kick.com/westcol"));
    QCOMPARE(browserUrl("kick", "westcol", "chat"), QString("https://kick.com/popout/westcol/chat"));
    auto input = kickMessage("m1", "2026-10-30T10:00:00Z", QString::fromUtf8("😀 [emote:123:Wave] hi"));
    auto sender = input["sender"].toObject();
    auto identity = sender["identity"].toObject();
    identity["badges_v2"] = QJsonArray{
        QJsonObject{{"badge_type", "subscriber"}, {"name", "Subscriber"},
                    {"image_url", "https://ext.cdn.kick.com/badge.png"}, {"selected", true}},
        QJsonObject{{"badge_type", "moderator"}, {"selected", false}}};
    sender["identity"] = identity;
    input["sender"] = sender;
    input["metadata"] = R"({"original_message":{"id":"root","content":"previous"},"original_sender":{"username":"Alice"}})";
    auto message = parseKick(input, true);
    QCOMPARE(message.text, QString::fromUtf8("😀 Wave hi"));
    QCOMPARE(message.emotes.size(), 1);
    QCOMPARE(message.text.mid(message.emotes[0].start, message.emotes[0].end - message.emotes[0].start), QString("Wave"));
    QCOMPARE(message.emotes[0].urls.first(), QString("https://files.kick.com/emotes/123/fullsize"));
    QCOMPARE(message.badges.size(), 1);
    QCOMPARE(message.badges[0].role, QString("subscriber"));
    QCOMPARE(str(message.reply["id"]), QString("root"));
    QVERIFY(message.history && message.stamp.isValid());
    QVERIFY(Images::allowed(QUrl("https://files.kick.com/emotes/123/fullsize")));
    QVERIFY(Images::allowed(QUrl("https://ext.cdn.kick.com/badge.png")));
    QVERIFY(!Images::allowed(QUrl("https://kick.com.evil.test/badge.png")));
    auto *source = app->hub.source("kick", "example");
    Span global; global.code = "Wave"; global.urls = {"https://cdn.betterttv.net/emote/global/3x"};
    source->emotes["Wave"] = global;
    for (const auto &url : {global.urls.first(), message.emotes.first().urls.first(), message.badges.first().urls.first()}) {
        auto image = QSharedPointer<Images::Entry>::create();
        image->image = QImage(16, 16, QImage::Format_ARGB32);
        image->image.fill(Qt::green);
        app->images.cache[url] = image;
    }
    message.source = source->key; message.seq = 9001;
    ChatView view(&app->hub);
    view.setMessages({message});
    auto document = view.document(0); (void)document->size();
    const auto images = view.details.value(message.seq).emoteImages;
    QCOMPARE(images.size(), 1);
    QCOMPARE(images.constBegin().value().second.first(), message.emotes.first().urls.first());
    QCOMPARE(view.details.value(message.seq).badgeImages.size(), 1);
}

void NativeTests::kickChatPollingAndRetry() {
    auto *source = app->hub.source("kick", "example");
    QCOMPARE(source->key, QString("kick:example"));
    auto *pane = app->addWorkspace({source})->focusedPane.data();
    int phase = 0, requests = 0;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        QCOMPARE(method, QByteArray("GET"));
        if (url.path() == "/api/v2/channels/example") {
            done(R"({"id":123,"user":{"id":456,"username":"Example","profile_pic":"https://files.kick.com/a.png"},"livestream":{"session_title":"Live title","viewer_count":10,"start_time":"2026-10-30 10:00:00","categories":[{"name":"Games"}]}})", {});
            return;
        }
        if (url.path() == "/emotes/example") { done("[]", {}); return; }
        if (url.host() == "files.kick.com") { done({}, "Fixture image unavailable"); return; }
        QCOMPARE(url.path(), QString("/api/v2/channels/123/messages"));
        ++requests;
        if (phase == 2) { done({}, "HTTP 429: limited"); return; }
        QJsonArray messages;
        QString cursor;
        if (phase == 0) {
            messages = {kickMessage("b", "2026-10-30T10:00:02Z"), kickMessage("a", "2026-10-30T10:00:01Z")};
            cursor = "first";
        } else if (QUrlQuery(url).queryItemValue("cursor").isEmpty()) {
            messages = {kickMessage("d", "2026-10-30T10:00:04Z"), kickMessage("sent", "2026-10-30T10:00:03Z"), kickMessage("c", "2026-10-30T10:00:03Z")};
            cursor = "next";
        } else {
            messages = {kickMessage("b", "2026-10-30T10:00:02Z")};
        }
        done(QJsonDocument(QJsonObject{{"data", QJsonObject{{"messages", messages}, {"cursor", cursor}}}}).toJson(), {});
    };
    app->hub.testMode = false;
    source->kickConnect();
    source->poll.stop();
    source->kickPoll();
    QCOMPARE(source->room(), QString("456"));
    QCOMPARE(source->header["category"].toString(), QString("Games"));
    QCOMPARE(source->header["started_at"].toString(), QString("2026-10-30T10:00:00Z"));
    QCOMPARE(source->history[0].id, QString("a"));
    QCOMPARE(source->history[1].id, QString("b"));
    QVERIFY(source->history[0].history);
    Message local; local.id = "sent"; local.name = "Me"; local.text = "locally sent"; local.own = true;
    source->publish(local);
    phase = 1;
    source->poll.stop();
    source->kickPoll();
    QCOMPARE(requests, 3);
    QCOMPARE(source->history[source->history.size() - 2].id, QString("c"));
    QCOMPARE(source->history.last().id, QString("d"));
    QVERIFY(!source->history.last().history);
    bool ownUpdated = false;
    for (const auto &m : source->history) if (m.id == "sent") ownUpdated = m.own && m.color == "#aabbcc";
    QVERIFY(ownUpdated);
    QTRY_VERIFY_WITH_TIMEOUT(pane->view->messages.size() >= 4, 1000);
    phase = 2;
    source->poll.stop();
    source->kickPoll();
    QVERIFY(source->poll.isActive());
    QVERIFY(source->poll.interval() >= 120000);
    source->stop();
    app->hub.testMode = true;
}

void NativeTests::kickSendingAndCredentialIsolation() {
    QCOMPARE(app->accounts.vault.target("google").contains("Google OAuth"), true);
    QVERIFY(app->accounts.vault.target("kick") != app->accounts.vault.target("google"));
    QVERIFY(app->accounts.vault.target("kick") != app->accounts.vault.target("twitch"));
    auto *source = app->hub.source("kick", "example");
    source->header["channel_id"] = 456;
    app->accounts.kick = {{"access_token", "fixture"}, {"refresh_token", "refresh"}, {"client_id", "id"},
                          {"client_secret", "secret-fixture"}, {"name", "TestUser"}, {"user_id", 42},
                          {"expires_at", double(QDateTime::currentSecsSinceEpoch() + 3600)}};
    int sent = 0;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray body, QMap<QByteArray, QByteArray> headers,
                             BytesCallback done) {
        QCOMPARE(url.toString(), QString("https://api.kick.com/public/v1/chat"));
        QCOMPARE(method, QByteArray("POST"));
        QCOMPARE(headers.value("Authorization"), QByteArray("Bearer fixture"));
        const auto payload = QJsonDocument::fromJson(body).object();
        QCOMPARE(payload["broadcaster_user_id"].toInt(), 456);
        QCOMPARE(payload["content"].toString(), QString("hello"));
        QCOMPARE(payload["type"].toString(), QString("user"));
        QCOMPARE(payload["reply_to_message_id"].toString(), QString("parent"));
        ++sent;
        done(R"({"data":{"is_sent":true,"message_id":"sent-message"}})", {});
    };
    QString error;
    Message parent; parent.id = "parent"; parent.source = source->key; parent.name = "Viewer"; parent.text = "prior";
    auto *pane = app->addWorkspace({source})->focusedPane.data();
    pane->startReply(parent);
    QCOMPARE(pane->reply.id, QString("parent"));
    app->hub.send(source, "hello", parent, [&](QJsonObject, QString e) { error = e; });
    QVERIFY(error.isEmpty());
    QCOMPARE(sent, 1);
    QVERIFY(source->history.last().own);
    QCOMPARE(source->history.last().id, QString("sent-message"));
    QCOMPARE(str(source->history.last().reply["id"]), QString("parent"));
    QVERIFY(!QJsonDocument(app->cfg.data).toJson().contains("secret-fixture"));
    QVERIFY(!app->hub.canEditChannel(source));
    QVERIFY(!app->hub.hasModerationRole(source));
}

void NativeTests::kickCallbackRejectsWrongState() {
    auto &accounts = app->accounts;
    accounts.kickBusy = true;
    accounts.kickState = "expected";
    accounts.kickVerifier = "verifier-fixture";
    accounts.kickClientId = "client-fixture";
    accounts.kickClientSecret = "secret-fixture";
    QVERIFY(accounts.kickCallbackServer.listen(QHostAddress::LocalHost, 0));
    int calls = 0;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        ++calls;
        if (url.host() == "id.kick.com") done(R"({"access_token":"fixture","refresh_token":"refresh","expires_in":3600,"scope":"user:read chat:write"})", {});
        else done(R"({"data":[{"user_id":42,"name":"TestUser","email":"must-not-be-stored@example.test"}]})", {});
    };
    auto callback = [&](QString state) {
        auto *socket = new QTcpSocket(&accounts);
        socket->connectToHost(QHostAddress::LocalHost, accounts.kickCallbackServer.serverPort());
        QVERIFY(socket->waitForConnected(3000));
        socket->write(("GET /callback?code=test&state=" + state + " HTTP/1.1\r\nHost: localhost\r\n\r\n").toUtf8());
        socket->flush();
    };
    callback("wrong");
    QTest::qWait(50);
    QVERIFY(accounts.kickBusy);
    QCOMPARE(calls, 0);
    callback("expected");
    QTRY_VERIFY_WITH_TIMEOUT(!accounts.kickBusy, 3000);
    QCOMPARE(calls, 2);
    QCOMPARE(str(accounts.kick["user_id"]), QString("42"));
    QVERIFY(!accounts.kick.contains("email"));
    QVERIFY(accounts.kickState.isEmpty() && accounts.kickClientSecret.isEmpty());
}

void NativeTests::kickTokenRefreshIsSerialized() {
    auto &accounts = app->accounts;
    accounts.kick = {{"access_token", "old"}, {"refresh_token", "refresh"}, {"client_id", "id"},
                     {"client_secret", "private"}, {"expires_at", 0}, {"user_id", 42}};
    BytesCallback pending;
    int requests = 0, completed = 0;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray body, QMap<QByteArray, QByteArray>, BytesCallback done) {
        QCOMPARE(url.toString(), QString("https://id.kick.com/oauth/token"));
        QCOMPARE(method, QByteArray("POST"));
        QVERIFY(body.contains("grant_type=refresh_token"));
        ++requests;
        pending = done;
    };
    accounts.kickToken([&](QString token, QString error) { QVERIFY(error.isEmpty()); QCOMPARE(token, QString("new")); ++completed; });
    accounts.kickToken([&](QString token, QString error) { QVERIFY(error.isEmpty()); QCOMPARE(token, QString("new")); ++completed; });
    QCOMPARE(requests, 1);
    QVERIFY(pending);
    pending(R"({"access_token":"new","refresh_token":"rotated","expires_in":3600})", {});
    QCOMPARE(completed, 2);
    QCOMPARE(str(accounts.kick["refresh_token"]), QString("rotated"));
    QCOMPARE(str(accounts.kick["client_secret"]), QString("private"));
    QVERIFY(!accounts.kickRefreshing);
}

void NativeTests::singleAndMixedChatTargetVisibility() {
    auto *kick = app->hub.source("kick", "example");
    auto *pane = app->addWorkspace({kick})->focusedPane.data();
    QVERIFY(pane->target->isHidden());
    QCOMPARE(pane->targetSource(), kick);
    pane->setSources({kick, twitch});
    QVERIFY(!pane->target->isHidden());
    QCOMPARE(pane->target->count(), 2);
    pane->target->setCurrentIndex(1);
    QCOMPARE(pane->targetSource(), twitch);
    pane->setSources({kick});
    QVERIFY(pane->target->isHidden());
    QCOMPARE(pane->targetSource(), kick);
}

void NativeTests::kickSessionAndSavedChannel() {
    app->openChats({"https://kick.com/example"});
    auto *source = app->hub.sources.value("kick:example");
    QVERIFY(source);
    app->saveChannel(source);
    QCOMPARE(app->cfg.data["saved_channels"].toArray().last().toObject()["platform"].toString(), QString("kick"));
    app->saveSession();
    const auto profile = app->cfg.home;
    app.reset();
    app = std::make_unique<MainWindow>(profile, true);
    QVERIFY(app->hub.sources.contains("kick:example"));
    QVERIFY(app->currentPane());
    QCOMPARE(app->currentPane()->targetSource()->platform, QString("kick"));
}

void NativeTests::kickAccountUiProtectsSecret() {
    app->showSettings("Accounts");
    auto *settings = app->settings.data();
    QVERIFY(settings);
    auto *button = settings->findChild<QPushButton *>("kick-sign-in");
    QVERIFY(button);
    QTest::mouseClick(button, Qt::LeftButton);
    auto *id = settings->findChild<QLineEdit *>("kick-client-id");
    auto *secret = settings->findChild<QLineEdit *>("kick-client-secret");
    QVERIFY(id && secret);
    QCOMPARE(secret->echoMode(), QLineEdit::Password);
    QVERIFY(secret->text().isEmpty());
    auto *dialog = qobject_cast<QDialog *>(id->window());
    QVERIFY(dialog);
    dialog->close();
}
