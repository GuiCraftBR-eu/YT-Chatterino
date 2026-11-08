#include "native_tests.hpp"
#include "vod.hpp"

void NativeTests::vodAddressValidation() {
    QString p, v, c;
    QVERIFY(VodReplay::identify(QUrl("https://www.youtube.com/watch?v=wXspodtIxYU&t=13"), &p, &v, &c));
    QCOMPARE(p, "youtube"); QCOMPARE(v, "wXspodtIxYU");
    QVERIFY(VodReplay::identify(QUrl("https://www.twitch.tv/videos/2918508215"), &p, &v, &c));
    QCOMPARE(p, "twitch"); QCOMPARE(v, "2918508215");
    QVERIFY(VodReplay::identify(QUrl("https://kick.com/xqc/videos/01a1b308-7f68-76ba-83ad-c88cc3a54dcf"), &p, &v, &c));
    QCOMPARE(p, "kick"); QCOMPARE(c, "xqc");
    QVERIFY(!VodReplay::identify(QUrl("http://www.youtube.com/watch?v=wXspodtIxYU"), nullptr, nullptr, nullptr));
    QVERIFY(!VodReplay::identify(QUrl("https://www.youtube.com.evil.test/watch?v=wXspodtIxYU"), nullptr, nullptr, nullptr));
    QVERIFY(!VodReplay::identify(QUrl("https://www.twitch.tv/alanzoka"), nullptr, nullptr, nullptr));
    QVERIFY(!VodReplay::identify(QUrl("https://kick.com/xqc/videos/not-a-uuid"), nullptr, nullptr, nullptr));
}

void NativeTests::vodTwitchPlaybackAndSeek() {
    int requests = 0;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray body, QMap<QByteArray,QByteArray> headers, BytesCallback done) {
        QCOMPARE(url.host(), QString("gql.twitch.tv")); QCOMPARE(method, QByteArray("POST"));
        QVERIFY(headers.contains("Client-ID")); QVERIFY(!headers.contains("Authorization"));
        auto vars = QJsonDocument::fromJson(body).object()["variables"].toObject();
        QCOMPARE(str(vars["videoID"]), QString("12345")); ++requests;
        QCOMPARE(vars["contentOffsetSeconds"].toDouble(), double(vars["contentOffsetSeconds"].toInt()));
        const int start = vars["contentOffsetSeconds"].toInt();
        QJsonArray edges;
        for (int offset : (start < 30 ? QList<int>{11,13} : QList<int>{49,51})) {
            QJsonObject node{{"id", QString::number(offset)}, {"contentOffsetSeconds", offset},
                {"createdAt", "2026-01-01T00:00:00Z"},
                {"commenter", QJsonObject{{"id", "1"}, {"displayName", "Viewer"}, {"login", "viewer"}}},
                {"message", QJsonObject{{"fragments", QJsonArray{QJsonObject{{"text", QString("at %1").arg(offset)}}}}, {"userBadges", QJsonArray{}}}}};
            edges.append(QJsonObject{{"cursor", QString::number(offset)}, {"node", node}});
        }
        QJsonObject comments{{"edges", edges}, {"pageInfo", QJsonObject{{"hasNextPage", false}}}};
        done(QJsonDocument(QJsonObject{{"data", QJsonObject{{"video", QJsonObject{{"comments", comments}}}}}}).toJson(), {});
    };
    VodReplay vod(&app->hub);
    QSignalSpy messages(&vod, &VodReplay::message), cleared(&vod, &VodReplay::cleared);
    const QUrl url("https://www.twitch.tv/videos/12345");
    vod.playback(url, 12037, true, false);
    QCOMPARE(cleared.size(), 1); QCOMPARE(messages.size(), 1); QCOMPARE(requests, 1);
    auto first = qvariant_cast<Message>(messages[0][0]); QCOMPARE(first.text, "at 11"); QCOMPARE(first.replayOffsetMs, 11000);
    vod.playback(url, 12037, true, false); QCOMPARE(messages.size(), 1);
    vod.playback(url, 13000, false, false); QCOMPARE(messages.size(), 2);
    vod.playback(url, 50000, true, true); QCOMPARE(cleared.size(), 2); QCOMPARE(messages.size(), 3);
    QCOMPARE(qvariant_cast<Message>(messages[2][0]).text, "at 49");
    vod.playback(url, 51000, false, false); QCOMPARE(messages.size(), 4);
    QCOMPARE(qvariant_cast<Message>(messages[3][0]).text, "at 51");
    Message round = Message::fromJson(first.json()); QCOMPARE(round.replayOffsetMs, 11000);
    ChatView view(&app->hub); view.setMessages({first});
    QVERIFY(view.document(0)->toPlainText().contains("0:00:11"));
}

void NativeTests::vodYouTubeReplayParsing() {
    int calls = 0;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray body, QMap<QByteArray,QByteArray> headers, BytesCallback done) {
        QVERIFY(!headers.contains("Authorization")); ++calls;
        if (url.path() == "/watch") {
            done(R"(ytInitialData = {"liveChatRenderer":{"isReplay":true,"continuations":[{"reloadContinuationData":{"continuation":"start"}}]}};)", {});
        } else if (url.path() == "/live_chat_replay") {
            done(R"(ytcfg.set({"INNERTUBE_CONTEXT":{"client":{"clientName":"WEB"}}}); ytInitialData = {"liveChatContinuation":{"continuations":[{"liveChatReplayContinuationData":{"continuation":"seekable"}}]}};)", {});
        } else {
            QCOMPARE(method, QByteArray("POST"));
            auto request = QJsonDocument::fromJson(body).object(); QCOMPARE(str(request["continuation"]), QString("seekable"));
            QJsonObject renderer{{"id", "youtube-message"}, {"authorName", QJsonObject{{"simpleText", "Viewer"}}},
                                 {"message", QJsonObject{{"runs", QJsonArray{QJsonObject{{"text", "hello"}}}}}}};
            QJsonObject inner{{"addChatItemAction", QJsonObject{{"item", QJsonObject{{"liveChatTextMessageRenderer", renderer}}}}}};
            QJsonObject replay{{"videoOffsetTimeMsec", "11000"}, {"actions", QJsonArray{inner}}};
            QJsonObject live{{"actions", QJsonArray{QJsonObject{{"replayChatItemAction", replay}}}}};
            done(QJsonDocument(QJsonObject{{"continuationContents", QJsonObject{{"liveChatContinuation", live}}}}).toJson(), {});
        }
    };
    VodReplay vod(&app->hub); QSignalSpy messages(&vod, &VodReplay::message);
    vod.playback(QUrl("https://www.youtube.com/watch?v=wXspodtIxYU"), 12000, true, false);
    QCOMPARE(calls, 3); QCOMPARE(messages.size(), 1);
    const auto m = qvariant_cast<Message>(messages[0][0]); QCOMPARE(m.id, "youtube-message"); QCOMPARE(m.replayOffsetMs, 11000);
    vod.retry.stop();
}

void NativeTests::vodKickReplayParsing() {
    const QString id = "01a1b308-7f68-76ba-83ad-c88cc3a54dcf";
    QStringList paths;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray,QByteArray> headers, BytesCallback done) {
        paths << url.path(); QVERIFY(!headers.contains("Authorization"));
        if (url.path() == "/api/v2/channels/xqc") done(R"({"id":668})", {});
        else if (url.path() == "/api/v1/channels/668/videos/" + id)
            done(QJsonDocument(QJsonObject{{"data", QJsonObject{{"id",id},{"start_time","2026-11-06T22:44:01Z"},{"duration",100}}}}).toJson(), {});
        else if (url.path() == "/api/v1/chat/668/history")
            done(R"({"data":{"messages":[{"id":"kick-message","created_at":"2026-11-06T22:44:12Z","content":"hello","sender":{"id":1,"username":"Viewer","identity":{}}}]}})", {});
        else done({}, "unexpected");
    };
    VodReplay vod(&app->hub); QSignalSpy messages(&vod, &VodReplay::message);
    vod.playback(QUrl("https://kick.com/xqc/videos/" + id), 12000, true, false);
    QCOMPARE(paths.size(), 3); QCOMPARE(messages.size(), 1);
    const auto m = qvariant_cast<Message>(messages[0][0]); QCOMPARE(m.id, "kick-message"); QCOMPARE(m.replayOffsetMs, 11000);
    vod.retry.stop();
}

void NativeTests::vodPairingRejectsInvalidToken() {
    QVERIFY(VodBridge::allowedOrigin("chrome-extension://abcdefghijklmnopabcdefghijklmnop"));
    QVERIFY(VodBridge::allowedOrigin("moz-extension://a5b27e91-a65a-4e23-85d6-d298f4c1ac12"));
    QVERIFY(!VodBridge::allowedOrigin("https://example.com"));
    QVERIFY(!VodBridge::allowedOrigin("null"));
    VodBridge bridge(&app->hub, nullptr, 0);
    QVERIFY(bridge.server.isListening());
    QCOMPARE(bridge.server.serverAddress(), QHostAddress(QHostAddress::LocalHost));
    const QUrl endpoint(QString("ws://127.0.0.1:%1").arg(bridge.server.serverPort()));
    QNetworkRequest badRequest(endpoint); badRequest.setRawHeader("Origin", "https://example.com");
    QWebSocket bad; bad.open(badRequest);
    QTRY_VERIFY_WITH_TIMEOUT(bad.state() == QAbstractSocket::UnconnectedState, 3000);
    QVERIFY(bridge.clients.isEmpty());
    QNetworkRequest request(endpoint); request.setRawHeader("Origin", "chrome-extension://abcdefghijklmnopabcdefghijklmnop");
    QWebSocket good; QSignalSpy ready(&good, &QWebSocket::textMessageReceived);
    good.open(request); QTRY_COMPARE(good.state(), QAbstractSocket::ConnectedState);
    good.sendTextMessage(R"({"type":"hello","client":"a5b27e91-a65a-4e23-85d6-d298f4c1ac12"})");
    QTRY_COMPARE(ready.size(), 1); QCOMPARE(bridge.clients.size(), 1);
    good.sendTextMessage(R"({"type":"state","tab":1,"url":"https://www.youtube.com.evil.test/watch?v=wXspodtIxYU","seconds":20,"paused":true})");
    QTest::qWait(30); QVERIFY(bridge.players.isEmpty());
    good.close(); QTRY_VERIFY(bridge.clients.isEmpty());
}

void NativeTests::unavailableFollowDateIsHidden() {
    auto scopes = app->accounts.twitch["scope"].toArray(); scopes.append("moderator:read:followers");
    app->accounts.twitch["scope"] = scopes; twitch->ownTwitchUserId = "42";
    twitch->twitchModeratorKnown = twitch->twitchModerator = true;
    app->net.transport = [](QUrl url, QByteArray, QByteArray, QMap<QByteArray,QByteArray>, BytesCallback done) {
        if (url.path() == "/helix/users") done(R"({"data":[{"id":"77","display_name":"Viewer"}]})", {});
        else if (url.path() == "/helix/channels/followers") done(R"({"data":[]})", {});
        else done({}, "unavailable");
    };
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    Message viewer; viewer.uid = "77"; viewer.name = "Viewer"; viewer.login = "viewer";
    app->show(); app->showUser(pane, twitch, viewer);
    auto card = app->userCards.value(twitch->key + ":77"); QVERIFY(card);
    auto *follow = card->findChild<QLabel *>("user-card-follow-date"); QVERIFY(follow); QVERIFY(follow->isHidden());
}

void NativeTests::notificationIconsAreCenteredAndConsistent() {
    for (const auto &kind : QStringList{"prime", "subscription", "reward", "money", "gift", "bits", "other"}) {
        const auto image = alertIcon(kind);
        QCOMPARE(image.size(), QSize(64, 64));
        int left = image.width(), top = image.height(), right = -1, bottom = -1;
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y).alpha() < 3) continue;
            left = qMin(left, x); right = qMax(right, x); top = qMin(top, y); bottom = qMax(bottom, y);
        }
        QVERIFY(right >= left && bottom >= top);
        QVERIFY(qAbs((left + right) - 63) <= 2); QVERIFY(qAbs((top + bottom) - 63) <= 2);
        QVERIFY(qMax(right - left + 1, bottom - top + 1) >= 52);
        QVERIFY(qMax(right - left + 1, bottom - top + 1) <= 56);
    }
}
