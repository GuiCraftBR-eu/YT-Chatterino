#include "native_tests.hpp"
#include "phone.hpp"

static QByteArray phone240(PhoneServer &server, QByteArray method, QByteArray path, QByteArray body = {}, QByteArray cookie = {}) {
    QTcpSocket socket; QEventLoop loop; QTimer deadline; deadline.setSingleShot(true);
    QByteArray response;
    QObject::connect(&socket, &QTcpSocket::connected, &loop, [&] {
        QByteArray request = method + ' ' + path + " HTTP/1.1\r\nHost: " + server.address().mid(7).toLatin1() + "\r\n";
        if (!cookie.isEmpty()) request += "Cookie: " + cookie + "\r\n";
        if (method == "POST") request += "Origin: " + server.address().toLatin1() + "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
        socket.write(request + "\r\n" + body);
    });
    QObject::connect(&socket, &QTcpSocket::readyRead, &loop, [&] { response += socket.readAll(); });
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::errorOccurred, &loop, [&](QAbstractSocket::SocketError) { loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    socket.connectToHost(QHostAddress::LocalHost, server.listener.serverPort()); deadline.start(3000); loop.exec();
    response += socket.readAll(); socket.abort(); return response;
}
static QJsonObject phoneJson(QByteArray response) {
    return QJsonDocument::fromJson(response.mid(response.indexOf("\r\n\r\n") + 4)).object();
}

void NativeTests::chatModeIndicators() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    QCOMPARE(twitch->chatModeSummary(), QString("Chat modes unavailable"));
    app->hub.twitch.channels[twitch->id] = twitch;
    app->hub.twitch.line("@emote-only=1;followers-only=15;r9k=1;room-id=12;slow=8;subs-only=1 :tmi.twitch.tv ROOMSTATE #guicraft_");
    QVERIFY(twitch->chatModeSummary().contains("Followers only (15 min)"));
    QVERIFY(twitch->chatModeSummary().contains("Slow mode (8 sec)"));
    QVERIFY(twitch->chatModeSummary().contains("Subscribers only"));
    QVERIFY(pane->chatModesLabel->text().contains("Emotes only"));
    app->hub.twitch.line("@followers-only=-1;emote-only=0 :tmi.twitch.tv ROOMSTATE #guicraft_");
    QVERIFY(!twitch->chatModes["followers"].toBool());
    QVERIFY(twitch->chatModes["subscribers"].toBool());
    app->hub.twitch.line("@r9k=0;slow=0;subs-only=0 :tmi.twitch.tv ROOMSTATE #guicraft_");
    QCOMPARE(twitch->chatModeSummary(), QString("No known restrictions"));
    auto *kick = app->hub.source("kick", "example");
    kick->kickUpdateChatModes({{"followers_mode", true}, {"following_min_duration", 60}, {"subscribers_mode", false}, {"emotes_mode", true}, {"slow_mode", true}, {"message_interval", 10}});
    QVERIFY(kick->chatModeSummary().contains("Followers only (1 h)"));
    kick->kickEvent("ChatroomUpdatedEvent", {{"slow_mode", false}});
    QVERIFY(!kick->chatModeSummary().contains("Slow mode"));
    QVERIFY(kick->chatModeSummary().contains("Emotes only"));
    kick->kickUpdateChatModes({{"followers_mode", QJsonObject{{"enabled", true}, {"min_duration", 5760}}},
                              {"subscribers_mode", QJsonObject{{"enabled", false}}},
                              {"emotes_mode", QJsonObject{{"enabled", false}}},
                              {"slow_mode", QJsonObject{{"enabled", true}, {"message_interval", 5}}},
                              {"account_age", QJsonObject{{"enabled", true}, {"min_duration", 1440}}}});
    QVERIFY(kick->chatModeSummary().contains("Followers only (4 d)"));
    QVERIFY(kick->chatModeSummary().contains("Slow mode (5 sec)"));
    QVERIFY(kick->chatModeSummary().contains("Account age restriction (1 d)"));
    auto *yt = app->hub.source("youtube", "abcdefghijk");
    QCOMPARE(yt->chatModeSummary(), QString("Chat modes unavailable"));
    yt->youtubeModeNotices(QJsonObject{{"liveChatModeChangeMessageRenderer", QJsonObject{
        {"icon", QJsonObject{{"iconType", "SLOW_MODE"}}}, {"text", QJsonObject{{"simpleText", "Slow mode is on"}}},
        {"subtext", QJsonObject{{"simpleText", "10 seconds"}}}}}});
    QVERIFY(yt->chatModeSummary().contains("Last YouTube mode notice"));
    QVERIFY(yt->chatModeSummary().contains("10 seconds"));
    app->cfg.data["language"] = "pt-BR";
    QVERIFY(kick->chatModeSummary().contains("Somente seguidores"));
}

void NativeTests::timestampFontSetting() {
    auto a = app->cfg.appearance();
    a["message_font_size"] = 16; a["timestamp_font_size"] = 8; a["show_timestamps"] = true;
    app->cfg.setAppearance(a);
    Message m; m.id = "t"; m.name = "A"; m.login = "a"; m.source = twitch->key; m.text = "hello";
    m.stamp = QDateTime::currentDateTime(); m.seq = 123;
    ChatView view(&app->hub); view.resize(500, 100); view.setMessages({m});
    auto document = view.document(0);
    QTextCursor atTime(document.data()); atTime.setPosition(1); atTime.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    QCOMPARE(atTime.charFormat().font().pointSize(), 8);
    QCOMPARE(app->cfg.messageFont().pointSize(), 16);
    app->showSettings("Display");
    QVERIFY(app->settings->findChild<QSpinBox *>("timestamp_font_size"));
}

void NativeTests::phoneThreadsHistoryAndLongPoll() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    Message root; root.id = "root"; root.name = "Alice"; root.login = "alice"; root.uid = "11"; root.text = "original"; root.stamp = QDateTime::currentDateTime();
    twitch->publish(root);
    Message reply; reply.id = "reply"; reply.name = "Bob"; reply.login = "bob"; reply.uid = "22"; reply.text = "answer";
    reply.reply = {{"id", "root"}, {"thread_id", "root"}, {"name", "Alice"}, {"text", "original"}}; twitch->publish(reply);
    Message other = root; other.id = "other"; other.text = "another"; twitch->publish(other);
    PhoneServer server(&app->hub); QVERIFY(server.start(pane, QHostAddress::LocalHost));
    const auto body = QJsonDocument(QJsonObject{{"code", server.pairingCode()}}).toJson(QJsonDocument::Compact);
    const auto paired = phone240(server, "POST", "/api/pair", body);
    QVERIFY(paired.startsWith("HTTP/1.1 200"));
    QByteArray cookie; for (auto line : paired.split('\n')) if (line.startsWith("Set-Cookie: ")) cookie = line.trimmed().mid(12).split(';').first();
    QVERIFY(cookie.startsWith("ytphone="));
    auto state = phoneJson(phone240(server, "GET", "/api/state?after=0", {}, cookie));
    const auto rows = state["messages"].toArray(); QCOMPARE(rows.size(), 3);
    QCOMPARE(str(rows[1].toObject()["reply"].toObject()["name"]), QString("Alice"));
    const auto seq = QByteArray::number(qint64(rows[1].toObject()["seq"].toDouble()));
    auto thread = phoneJson(phone240(server, "GET", "/api/thread?source=twitch%3Aguicraft_&seq=" + seq, {}, cookie));
    QCOMPARE(thread["messages"].toArray().size(), 2);
    QCOMPARE(str(thread["name"]), QString("Alice"));
    QCOMPARE(str(thread["messages"].toArray()[0].toObject()["text"]), QString("original"));
    const auto rootSeq = QByteArray::number(qint64(rows[0].toObject()["seq"].toDouble()));
    auto history = phoneJson(phone240(server, "GET", "/api/user-history?source=twitch%3Aguicraft_&seq=" + rootSeq, {}, cookie));
    QCOMPARE(history["messages"].toArray().size(), 2);
    QVERIFY(phone240(server, "GET", "/api/user-history?source=twitch%3Aother&seq=" + rootSeq, {}, cookie).startsWith("HTTP/1.1 400"));
    QElapsedTimer elapsed; elapsed.start();
    QTimer::singleShot(100, twitch, [this] { Message m; m.id="fresh"; m.name="Alice"; m.login="alice"; m.uid="11"; m.text="quick"; twitch->publish(m); });
    const auto longPath = "/api/state?after=" + QByteArray::number(qint64(state["after"].toDouble())) + "&revision=" + QByteArray::number(state["revision"].toVariant().toULongLong()) + "&wait=1";
    auto update = phoneJson(phone240(server, "GET", longPath, {}, cookie));
    QVERIFY2(elapsed.elapsed() < 1000, "Phone long poll did not wake promptly");
    QCOMPARE(str(update["messages"].toArray().last().toObject()["text"]), QString("quick"));
    server.stop();
}
