#include "native_tests.hpp"
#include "automod.hpp"
#include "vod.hpp"
#ifdef Q_OS_WIN
#include "obsupdate.hpp"
#endif

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
        ++requests; done({}, {}); // Twitch returns HTTP 204 with an empty body.
    };
    QString error;
    app->hub.autoMod->review(twitch, "held-1", true, [&](QJsonObject, QString e) { error = e; });
    QVERIFY(error.isEmpty()); QCOMPARE(requests, 1);
    QVERIFY(!twitch->autoModPending.contains("held-1"));
    QCOMPARE(twitch->history.last().special, QString("automod-resolved"));
    app->hub.autoMod->review(twitch, "held-1", true, [&](QJsonObject, QString e) { error = e; });
    QVERIFY(!error.isEmpty()); QCOMPARE(requests, 1);
    auto held2 = held;
    auto item2 = item; item2["message_id"] = "held-2"; held2["event"] = item2;
    app->hub.autoMod->event(held2);
    auto changed = held2; auto subscription = changed["subscription"].toObject();
    subscription["type"] = "automod.message.update"; changed["subscription"] = subscription;
    item2["status"] = "expired"; changed["event"] = item2;
    app->hub.autoMod->event(changed);
    QVERIFY(!twitch->autoModPending.contains("held-2"));
    QCOMPARE(twitch->history.last().special, QString("automod-resolved"));
    QCOMPARE(twitch->history.last().alertKind, QString("expired"));
    auto item3 = item; item3["message_id"] = "held-3"; held2["event"] = item3;
    app->hub.autoMod->event(held2);
    app->net.transport = [&](QUrl, QByteArray method, QByteArray body, QMap<QByteArray, QByteArray>, BytesCallback done) {
        QCOMPARE(method, QByteArray("POST"));
        QCOMPARE(str(QJsonDocument::fromJson(body).object()["action"]), QString("DENY"));
        ++requests; done({}, {});
    };
    app->hub.autoMod->review(twitch, "held-3", false, [&](QJsonObject, QString e) { error = e; });
    QVERIFY(error.isEmpty()); QCOMPARE(requests, 2);
    QCOMPARE(twitch->history.last().alertKind, QString("denied"));
}

void NativeTests::browserPollVotesRouteAndHandleClosedTabs() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
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
    QTRY_VERIFY(pane->findChild<QComboBox *>("twitch-poll-choice"));
    auto *choice = pane->findChild<QComboBox *>("twitch-poll-choice");
    auto *vote = pane->findChild<QPushButton *>("twitch-poll-vote");
    QVERIFY(vote); QVERIFY(!vote->isEnabled());
    choice->setCurrentIndex(1); QVERIFY(vote->isEnabled());
    vote->click(); QTRY_COMPARE(received.size(), 3);
    const auto uiCommand = QJsonDocument::fromJson(received.last()[0].toString().toUtf8()).object();
    QCOMPARE(str(uiCommand["question"]), QString("Which?"));
    QCOMPARE(str(uiCommand["choice"]), QString("choice-1"));
    QCOMPARE(str(uiCommand["label"]), QString("Yes"));
    reply["request"] = uiCommand["request"];
    socket.sendTextMessage(QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
    QTRY_VERIFY(pane->findChild<QLabel *>("twitch-poll-status"));
    QTRY_VERIFY(pane->findChild<QLabel *>("twitch-poll-status")->text().contains("confirmed", Qt::CaseInsensitive));
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

void NativeTests::obsUpdaterValidatesAndStagesUpdates() {
#ifndef Q_OS_WIN
    QSKIP("The Windows OBS bootstrapper runs on Windows only.");
#else
    const QString version = "9.8.7", name = "YT-Chatterino-OBS-Windows-" + version + ".zip";
    const auto url = "https://github.com/" + Repo + "/releases/download/v" + version + '/' + name;
    const QJsonObject asset{{"name", name}, {"browser_download_url", url}, {"size", 123},
                            {"digest", "sha256:" + QString(64, 'a')}};
    const QJsonObject release{{"tag_name", "v" + version}, {"assets", QJsonArray{asset}}};
    QCOMPARE(str(ObsPluginUpdater::releaseInfo(release, "2.5.6")["version"]), version);
    auto invalid = release; auto bad = asset; bad["browser_download_url"] = "https://example.com/update.zip";
    invalid["assets"] = QJsonArray{bad}; QVERIFY(ObsPluginUpdater::releaseInfo(invalid, "2.5.6").isEmpty());
    QVERIFY(!ObsPluginUpdater::safeListing("yt-chatterino-obs/../../evil.dll\n"));
    QVERIFY(!ObsPluginUpdater::safeListing("yt-chatterino-obs/a\nyt-chatterino-obs/a\n"));
    const QString input = temp.path() + "/obs-update-fixture", folder = input + "/yt-chatterino-obs";
    QVERIFY(QDir().mkpath(folder + "/data/sidecar"));
    auto write = [](const QString &path, const QByteArray &bytes) {
        QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    QVERIFY(write(folder + "/yt-chatterino-obs.dll", "MZbootstrap"));
    QVERIFY(write(folder + "/yt-chatterino-obs-core.dll", "MZcore"));
    QVERIFY(write(folder + "/LEIA-ME-OBS.txt", "fixture"));
    QVERIFY(write(folder + "/data/sidecar/YT-Chatterino.exe", "MZsidecar"));
    QVERIFY(writeObject(folder + "/data/sidecar/update-manifest.json",
                        {{"version", version}, {"files", QJsonArray{"YT-Chatterino.exe", "update-manifest.json"}}}));
    const auto archive = temp.path() + "/obs-update.zip";
    const auto tar = qEnvironmentVariable("SystemRoot", "C:/Windows") + "/System32/tar.exe";
    QCOMPARE(QProcess::execute(tar, {"-a", "-cf", archive, "-C", input, "yt-chatterino-obs"}), 0);
    QFile zip(archive); QVERIFY(zip.open(QIODevice::ReadOnly));
    ObsPluginUpdater updater(&app->net, nullptr, temp.path() + "/obs-updates");
    bool notified = false; updater.notify = [&](const QString &) { notified = true; }; updater.busy = true;
    updater.stage({{"version", version}}, zip.readAll());
    QTRY_VERIFY_WITH_TIMEOUT(notified, 15000);
    const auto marker = readObject(updater.rootPath + "/current.json");
    QCOMPARE(str(marker["version"]), version);
    const auto directory = str(marker["directory"]);
    QVERIFY(QFileInfo::exists(updater.rootPath + '/' + directory + "/yt-chatterino-obs-core.dll"));
    QCOMPARE(str(marker["core_sha256"]), QString::fromLatin1(QCryptographicHash::hash("MZcore", QCryptographicHash::Sha256).toHex()));
#endif
}
