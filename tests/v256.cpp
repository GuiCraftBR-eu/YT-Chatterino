#include "native_tests.hpp"
#include "automod.hpp"
#include "vod.hpp"

void NativeTests::twitchChatModesUseValidatedRequests() {
    twitch->header["channel_id"] = "42";
    int requests = 0;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray body, QMap<QByteArray, QByteArray>, BytesCallback done) {
        QCOMPARE(url.path(), QString("/helix/chat/settings"));
        QCOMPARE(QUrlQuery(url).queryItemValue("broadcaster_id"), QString("42"));
        QCOMPARE(QUrlQuery(url).queryItemValue("moderator_id"), QString("42"));
        if (requests++ == 0) QCOMPARE(method, QByteArray("GET"));
        else {
            QCOMPARE(method, QByteArray("PATCH"));
            const auto patch = QJsonDocument::fromJson(body).object();
            QCOMPARE(patch["slow_mode"].toBool(), true);
            QCOMPARE(patch["slow_mode_wait_time"].toInt(), 30);
        }
        done(R"({"data":[{"emote_mode":false,"follower_mode":true,"follower_mode_duration":15,"subscriber_mode":false,"slow_mode":true,"slow_mode_wait_time":30,"unique_chat_mode":false}]})", {});
    };
    QString error;
    app->hub.twitchChatSettings(twitch, {}, [&](QJsonObject, QString e) { error = e; });
    QVERIFY(error.isEmpty()); QCOMPARE(requests, 1);
    QVERIFY(twitch->chatModes["followers"].toBool());
    QCOMPARE(twitch->chatModes["follow_minutes"].toInt(), 15);
    app->hub.twitchChatSettings(twitch, {{"slow_mode", true}, {"slow_mode_wait_time", 30}}, [&](QJsonObject, QString e) { error = e; });
    QVERIFY(error.isEmpty()); QCOMPARE(requests, 2);
    app->hub.twitchChatSettings(twitch, {{"slow_mode_wait_time", 200}}, [&](QJsonObject, QString e) { error = e; });
    QVERIFY(!error.isEmpty()); QCOMPARE(requests, 2);
}

void NativeTests::autoModHeldMessagesCanBeReviewed() {
    twitch->header["channel_id"] = "42";
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    const QJsonObject condition{{"broadcaster_user_id", "42"}, {"moderator_user_id", "42"}};
    const QJsonObject item{{"broadcaster_user_id", "42"}, {"message_id", "held-1"},
        {"user_id", "99"}, {"user_name", "Viewer"}, {"message", QJsonObject{{"text", "Hello"}}}};
    const QJsonObject held{{"subscription", QJsonObject{{"type", "automod.message.hold"}, {"condition", condition}}}, {"event", item}};
    app->hub.autoMod->event(held);
    app->hub.autoMod->event(held);
    QCOMPARE(twitch->history.size(), 1);
    QVERIFY(twitch->autoModPending.contains("held-1"));
    const auto message = twitch->history.last();
    QCOMPARE(message.special, QString("automod-held"));
    pane->view->setMessages({message});
    const auto doc = pane->view->document(0); QVERIFY(doc);
    QVERIFY(doc->toHtml().contains("ytautomod:allow"));
    int requests = 0;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray body, QMap<QByteArray, QByteArray>, BytesCallback done) {
        QCOMPARE(url.path(), QString("/helix/moderation/automod/message"));
        QCOMPARE(method, QByteArray("POST"));
        const auto payload = QJsonDocument::fromJson(body).object();
        QCOMPARE(str(payload["user_id"]), QString("42"));
        QCOMPARE(str(payload["msg_id"]), QString("held-1"));
        QCOMPARE(str(payload["action"]), QString("ALLOW"));
        ++requests; done("{}", {});
    };
    QString error;
    app->hub.autoMod->review(twitch, "held-1", true, [&](QJsonObject, QString e) { error = e; });
    QVERIFY(error.isEmpty()); QCOMPARE(requests, 1);
    QVERIFY(!twitch->autoModPending.contains("held-1"));
    QCOMPARE(twitch->history.last().special, QString("automod-resolved"));
    app->hub.autoMod->review(twitch, "held-1", true, [&](QJsonObject, QString e) { error = e; });
    QVERIFY(!error.isEmpty()); QCOMPARE(requests, 1);
}

void NativeTests::browserPollVotesRouteAndHandleClosedTabs() {
    app->startVodBridge(0);
    QNetworkRequest request(QUrl(QString("ws://127.0.0.1:%1").arg(app->vodBridge->server.serverPort())));
    request.setRawHeader("Origin", "chrome-extension://abcdefghijklmnopabcdefghijklmnop");
    QWebSocket socket; QSignalSpy received(&socket, &QWebSocket::textMessageReceived);
    socket.open(request); QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
    socket.sendTextMessage(R"({"type":"hello","client":"a5b27e91-a65a-4e23-85d6-d298f4c1ac12"})");
    QTRY_COMPARE(received.size(), 1);
    const QJsonObject poll{{"text", "Which?"}, {"state", "open"},
                           {"choices", QJsonArray{QJsonObject{{"id", "choice-1"}, {"label", "Yes"}}}}};
    const QJsonObject state{{"type", "live-state"}, {"tab", 17}, {"url", "https://www.twitch.tv/guicraft_"},
                            {"chat", QJsonObject{{"available", true}, {"poll", poll}}}};
    socket.sendTextMessage(QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact)));
    QTRY_COMPARE(str(twitch->browserPoll["state"]), QString("open"));
    QString status;
    app->vodBridge->voteTwitchPoll(twitch, "Which?", "choice-1", "Yes", [&](QJsonObject result, QString) { status = str(result["status"]); });
    QTRY_COMPARE(received.size(), 2);
    const auto command = QJsonDocument::fromJson(received.last()[0].toString().toUtf8()).object();
    QCOMPARE(str(command["type"]), QString("vote-poll"));
    QCOMPARE(command["tab"].toInt(), 17);
    auto reply = QJsonObject{{"type", "poll-result"}, {"request", command["request"]}, {"tab", 18}, {"status", "confirmed"}};
    socket.sendTextMessage(QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
    QTest::qWait(25); QVERIFY(status.isEmpty());
    reply["tab"] = 17;
    socket.sendTextMessage(QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
    QTRY_COMPARE(status, QString("confirmed"));
    status.clear();
    app->vodBridge->voteTwitchPoll(twitch, "Which?", "choice-1", "Yes", [&](QJsonObject result, QString) { status = str(result["status"]); });
    socket.sendTextMessage(R"({"type":"live-closed","tab":17})");
    QTRY_COMPARE(status, QString("tab_closed"));
}

void NativeTests::overlayOpacityPersistsInSettings() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    app->cfg.data["overlay_opacity"] = 43;
    app->openPopup(pane, true);
    auto overlay = app->popupWindows.last(); QVERIFY(overlay);
    QVERIFY(qAbs(overlay->windowOpacity() - 0.43) < 0.02);
    app->showSettings("Display");
    auto *spin = app->settings->findChild<QSpinBox *>("overlay-opacity-setting"); QVERIFY(spin);
    QCOMPARE(spin->value(), 43);
    spin->setValue(61); app->settings->applyButton->click();
    QCOMPARE(app->cfg.data["overlay_opacity"].toInt(), 61);
    QVERIFY(qAbs(overlay->windowOpacity() - 0.61) < 0.02);
}
