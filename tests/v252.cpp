#include "native_tests.hpp"
#include "vod.hpp"

void NativeTests::liveIndicatorsFollowEachChannel() {
    auto *youtube = app->hub.source("youtube", "abcdefghijk");
    auto *kick = app->hub.source("kick", "example");
    youtube->header["live"] = false;
    kick->header["live"] = false;
    auto *workspace = app->addWorkspace({twitch, youtube, kick});
    auto *pane = workspace->focusedPane.data();
    QVERIFY(pane);
    const int index = app->tabs->indexOf(workspace);
    QVERIFY(index >= 0);
    app->refreshTabs();
    QVERIFY(app->tabs->tabIcon(index).isNull());
    kick->header["live"] = true;
    emit kick->headerChanged();
    QVERIFY(!app->tabs->tabIcon(index).isNull());
    if (qEnvironmentVariableIsSet("YT_CHATTERINO_TEST_SCREENSHOT")) {
        configureApplication(*qobject_cast<QApplication *>(qApp));
        app->resize(520, 760);
        app->show();
        QTest::qWait(50);
        QVERIFY(app->grab().save(qEnvironmentVariable("YT_CHATTERINO_TEST_SCREENSHOT")));
    }
    const auto liveImage = pane->mixedIcons[2]->pixmap().toImage();
    const auto offlineImage = pane->mixedIcons[0]->pixmap().toImage();
    QVERIFY(liveImage.pixelColor(16, 16).red() > offlineImage.pixelColor(16, 16).red());
    kick->header["live"] = false;
    emit kick->headerChanged();
    QVERIFY(app->tabs->tabIcon(index).isNull());
    youtube->header["live"] = true;
    emit youtube->headerChanged();
    QVERIFY(!app->tabs->tabIcon(index).isNull());
}

void NativeTests::twitchLiveIndicatorWithoutSignIn() {
    app->accounts.twitch = {};
    int requests = 0;
    BytesCallback pending;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        QVERIFY(url.host() == "gql.twitch.tv");
        ++requests; pending = std::move(done);
    };
    twitch->refreshTwitchPublicLive();
    twitch->refreshTwitchPublicLive();
    QCOMPARE(requests, 1);
    QVERIFY(bool(pending));
    pending(R"({"data":{"user":{"login":"guicraft_","displayName":"GuiCraft_","stream":{"id":"123"}}}})", {});
    QVERIFY(twitch->header["live"].toBool());
    QVERIFY(!twitch->header.contains("viewers"));
    twitch->refreshTwitchPublicLive();
    QCOMPARE(requests, 2);
    pending({}, "offline fixture failure");
    QVERIFY(twitch->header["live"].toBool());
    twitch->refreshTwitchPublicLive();
    pending(R"({"data":{"user":{"login":"guicraft_","displayName":"GuiCraft_","stream":null}}})", {});
    QVERIFY(!twitch->header["live"].toBool());
    QCOMPARE(twitch->header["viewers"].toInt(), 0);
}

void NativeTests::twitchBrowserPanelsFollowTabsAndPermissions() {
    app->startVodBridge(0);
    QVERIFY(app->vodBridge);
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    const QString client = "a5b27e91-a65a-4e23-85d6-d298f4c1ac12";
    const QJsonObject chat{{"available", true}, {"pin", QJsonObject{{"name", "Alice"}, {"text", "Please read"}}},
        {"poll", QJsonObject{{"text", "Which game?"}}}, {"prediction", QJsonObject{{"text", "Will they win?"}}}};
    auto send = [&](int tab, QJsonObject data, QString url = "https://www.twitch.tv/guicraft_") {
        app->vodBridge->processLiveState(client, {{"tab", tab}, {"url", url}, {"chat", data}});
    };
    send(1, chat);
    QCOMPARE(str(twitch->browserPinned["text"]), "Please read");
    QCOMPARE(str(twitch->browserPoll["text"]), "Which game?");
    QCOMPARE(str(twitch->browserPrediction["text"]), "Will they win?");
    auto labels = pane->decorations->findChildren<QLabel *>("chat-decoration");
    QCOMPARE(labels.size(), 3);
    QVERIFY(labels[0]->text().contains("Alice: Please read"));
    const auto actions = pane->decorations->findChildren<QToolButton *>("open-twitch-engagement");
    QCOMPARE(actions.size(), 2);
    QString requested;
    pane->openBrowserPanel = [&](Source *source, const QString &kind) { if (source == twitch) requested = kind; return true; };
    actions[0]->click(); QCOMPARE(requested, "poll");
    actions[1]->click(); QCOMPARE(requested, "prediction");
    twitch->setDecorations({{"text", "Official pin"}}, {});
    labels = pane->decorations->findChildren<QLabel *>("chat-decoration");
    QVERIFY(labels[0]->text().contains("Official pin"));
    twitch->setDecorations({}, {});
    send(2, {{"available", true}, {"pin", QJsonObject{{"text", "Second tab"}}}});
    QCOMPARE(str(twitch->browserPinned["text"]), "Second tab");
    app->vodBridge->removeLivePlayer(client + ":2");
    QCOMPARE(str(twitch->browserPinned["text"]), "Please read");
    send(1, {{"available", false}});
    QVERIFY(twitch->browserPinned.isEmpty());
    QVERIFY(twitch->browserPoll.isEmpty());
    send(1, chat, "https://www.twitch.tv.evil.example/guicraft_");
    QVERIFY(twitch->browserPinned.isEmpty());
    send(1, {{"available", true}, {"pin", QJsonObject{{"text", QString(2000, 'x')}}}});
    QCOMPARE(str(twitch->browserPinned["text"]).size(), 800);
    app->vodBridge->disconnectClient(client);
    QVERIFY(twitch->browserPinned.isEmpty());
}

void NativeTests::browserBridgeUnicodeAndPanelRouting() {
    app->startVodBridge(0);
    QVERIFY(app->vodBridge && app->vodBridge->server.isListening());
    QNetworkRequest request(QUrl(QString("ws://127.0.0.1:%1").arg(app->vodBridge->server.serverPort())));
    request.setRawHeader("Origin", "chrome-extension://abcdefghijklmnopabcdefghijklmnop");
    QWebSocket socket;
    QSignalSpy received(&socket, &QWebSocket::textMessageReceived);
    socket.open(request);
    QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
    socket.sendTextMessage(R"({"type":"hello","client":"a5b27e91-a65a-4e23-85d6-d298f4c1ac12"})");
    QTRY_COMPARE(received.size(), 1);
    const QString unicode(800, QChar(0x4e2d));
    const QJsonObject panel{{"text", unicode}, {"options", unicode}};
    const QJsonObject chat{{"available", true}, {"pin", panel}, {"poll", panel}, {"prediction", panel}};
    const auto bytes = QJsonDocument(QJsonObject{{"type", "live-state"}, {"tab", 17},
        {"url", "https://www.twitch.tv/guicraft_"}, {"chat", chat}}).toJson(QJsonDocument::Compact);
    QVERIFY(bytes.size() > 8192 && bytes.size() < 16384);
    socket.sendTextMessage(QString::fromUtf8(bytes));
    QTRY_COMPARE(str(twitch->browserPinned["text"]), unicode);
    QVERIFY(app->vodBridge->openTwitchPanel(twitch, "poll"));
    QTRY_COMPARE(received.size(), 2);
    const auto command = QJsonDocument::fromJson(received.last()[0].toString().toUtf8()).object();
    QCOMPARE(str(command["type"]), "open-panel");
    QCOMPARE(command["tab"].toInt(), 17);
    QCOMPARE(str(command["kind"]), "poll");
    QVERIFY(!app->vodBridge->openTwitchPanel(twitch, "vote"));
    socket.sendTextMessage(QString(17000, QChar('x')));
    QTRY_COMPARE(socket.state(), QAbstractSocket::UnconnectedState);
    QTRY_VERIFY(twitch->browserPinned.isEmpty());
}
