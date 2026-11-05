#include "native_tests.hpp"
#include "phone.hpp"

static QByteArray phoneRequest(PhoneServer &server, QByteArray method, QByteArray path, QByteArray body = {},
                               QByteArray cookie = {}, QByteArray origin = {}, QByteArray hostOverride = {}) {
    QTcpSocket socket; QEventLoop loop; QTimer deadline; deadline.setSingleShot(true);
    QByteArray response;
    const auto host = server.address().mid(7).toLatin1();
    QObject::connect(&socket, &QTcpSocket::connected, &loop, [&] {
        QByteArray request = method + ' ' + path + " HTTP/1.1\r\nHost: " +
                             (hostOverride.isEmpty() ? host : hostOverride) + "\r\nConnection: close\r\n";
        if (!cookie.isEmpty()) request += "Cookie: " + cookie + "\r\n";
        if (!origin.isEmpty()) request += "Origin: " + origin + "\r\n";
        if (method == "POST") request += "Content-Type: application/json\r\nContent-Length: " +
                                         QByteArray::number(body.size()) + "\r\n";
        socket.write(request + "\r\n" + body);
    });
    QObject::connect(&socket, &QTcpSocket::readyRead, &loop, [&] { response += socket.readAll(); });
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::errorOccurred, &loop, [&](QAbstractSocket::SocketError) { loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    socket.connectToHost(QHostAddress::LocalHost, server.listener.serverPort());
    deadline.start(5000); loop.exec();
    response += socket.readAll(); socket.abort();
    return response;
}

void NativeTests::phonePairingSecurityAndMessaging() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    Message incoming; incoming.id = "m1"; incoming.uid = "123"; incoming.name = "Viewer";
    incoming.login = "viewer"; incoming.text = "Hello <script>alert(1)</script>";
    incoming.stamp = QDateTime::currentDateTime();
    twitch->publish(incoming);
    Span emote; emote.code = "Wave"; emote.urls = {"https://cdn.betterttv.net/emote/1/3x"};
    twitch->emotes[emote.code] = emote;
    twitch->header["avatar"] = "https://cdn.betterttv.net/emote/54fa8f1401e468494b85b537/3x";
    auto avatar = QSharedPointer<Images::Entry>::create();
    avatar->image = QImage(20, 20, QImage::Format_ARGB32); avatar->image.fill(Qt::blue);
    app->images.cache[str(twitch->header["avatar"])] = avatar;
    app->images.cache["https://cdn.betterttv.net/emote/54fa8fce01e468494b85b53c/3x"] = avatar;
    PhoneServer server(&app->hub);
    QVERIFY(!server.start(pane, QHostAddress::AnyIPv4));
    QVERIFY(server.start(pane, QHostAddress::LocalHost));
    QCOMPARE(server.pairingCode().size(), 6);
    const auto origin = server.address().toLatin1();
    auto result = phoneRequest(server, "GET", "/");
    QVERIFY(result.startsWith("HTTP/1.1 200"));
    QVERIFY(result.contains("Content-Security-Policy"));
    QVERIFY(result.contains("Phone beta"));
    QVERIFY(phoneRequest(server, "GET", "/api/state?after=0").startsWith("HTTP/1.1 401"));
    QVERIFY(phoneRequest(server, "GET", "/", {}, {}, {}, "attacker.test").startsWith("HTTP/1.1 403"));
    const auto payload = QJsonDocument(QJsonObject{{"code", server.pairingCode()}}).toJson(QJsonDocument::Compact);
    QVERIFY(phoneRequest(server, "POST", "/api/pair", payload, {}, "https://attacker.test").startsWith("HTTP/1.1 403"));
    QVERIFY(phoneRequest(server, "POST", "/api/pair", R"({"code":"wrong"})", {}, origin).startsWith("HTTP/1.1 403"));
    result = phoneRequest(server, "POST", "/api/pair", payload, {}, origin);
    QVERIFY(result.startsWith("HTTP/1.1 200"));
    QVERIFY(result.contains("HttpOnly; SameSite=Strict"));
    QByteArray cookieLine;
    for (const auto &line : result.split('\n')) if (line.startsWith("Set-Cookie: ")) cookieLine = line.trimmed();
    const auto cookie = cookieLine.mid(QByteArray("Set-Cookie: ").size()).split(';').value(0);
    QVERIFY(cookie.startsWith("ytphone="));
    result = phoneRequest(server, "GET", "/api/state?after=0", {}, cookie);
    QVERIFY(result.startsWith("HTTP/1.1 200"));
    const auto state = QJsonDocument::fromJson(result.mid(result.indexOf("\r\n\r\n") + 4)).object();
    QCOMPARE(state["sources"].toArray().size(), 1);
    QCOMPARE(str(state["sources"].toArray().first().toObject()["avatar"]), str(twitch->header["avatar"]));
    const auto previousAvatar = twitch->header["avatar"];
    twitch->header["avatar"] = "https://cdn.betterttv.net/emote/54fa8fce01e468494b85b53c/3x";
    result = phoneRequest(server, "GET", "/api/state?after=" + QByteArray::number(qint64(state["after"].toDouble())) +
                                    "&revision=" + QByteArray::number(state["revision"].toVariant().toULongLong()), {}, cookie);
    const auto refreshed = QJsonDocument::fromJson(result.mid(result.indexOf("\r\n\r\n") + 4)).object();
    QVERIFY(refreshed["reset"].toBool());
    QCOMPARE(str(refreshed["sources"].toArray().first().toObject()["avatar"]), str(twitch->header["avatar"]));
    twitch->header["avatar"] = previousAvatar;
    QCOMPARE(str(state["messages"].toArray().last().toObject()["text"]), incoming.text);
    QVERIFY(!result.contains("fake-token"));
    result = phoneRequest(server, "GET", "/api/emotes?source=twitch%3Aguicraft_", {}, cookie);
    QVERIFY(result.contains("Wave"));
    auto send = [&](QString source, QString text, qint64 seq = 0) {
        return QJsonDocument(QJsonObject{{"source", source}, {"text", text}, {"seq", double(seq)}}).toJson(QJsonDocument::Compact);
    };
    QVERIFY(phoneRequest(server, "POST", "/api/send", send("twitch:other", "hello"), cookie, origin).startsWith("HTTP/1.1 400"));
    QVERIFY(phoneRequest(server, "POST", "/api/send", send(twitch->key, "hello", 999999), cookie, origin).startsWith("HTTP/1.1 400"));
    int sent = 0;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray body, QMap<QByteArray,QByteArray>, BytesCallback done) {
        QCOMPARE(url.path(), QString("/helix/chat/messages")); QCOMPARE(method, QByteArray("POST"));
        QCOMPARE(str(QJsonDocument::fromJson(body).object()["message"]), QString("phone fixture"));
        ++sent; done(R"({"data":[{"is_sent":true,"message_id":"phone-message"}]})", {});
    };
    result = phoneRequest(server, "POST", "/api/send", send(twitch->key, "phone fixture"), cookie, origin);
    QVERIFY(result.startsWith("HTTP/1.1 200"));
    QCOMPARE(sent, 1);
    QVERIFY(twitch->history.last().own);
    const auto originalSeq = twitch->history.first().seq;
    const auto moderate = [&](QString action, qint64 seq) {
        return QJsonDocument(QJsonObject{{"source", twitch->key}, {"seq", double(seq)},
                                        {"action", action}, {"duration", 600}}).toJson(QJsonDocument::Compact);
    };
    QVERIFY(phoneRequest(server, "POST", "/api/moderate", moderate("delete", originalSeq), cookie, origin).startsWith("HTTP/1.1 403"));
    twitch->ownTwitchUserId = "42"; twitch->twitchModeratorKnown = true; twitch->twitchModerator = true;
    QVERIFY(phoneRequest(server, "POST", "/api/moderate", moderate("unknown", originalSeq), cookie, origin).startsWith("HTTP/1.1 403"));
    int moderated = 0;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray, QMap<QByteArray,QByteArray>, BytesCallback done) {
        QCOMPARE(url.path(), QString("/helix/moderation/chat")); QCOMPARE(method, QByteArray("DELETE"));
        ++moderated; done("{}", {});
    };
    result = phoneRequest(server, "POST", "/api/moderate", moderate("delete", originalSeq), cookie, origin);
    QVERIFY(result.startsWith("HTTP/1.1 200")); QCOMPARE(moderated, 1);
    QVERIFY(twitch->history.first().deleted);
    QVERIFY(phoneRequest(server, "POST", "/api/moderate", moderate("delete", originalSeq), cookie, origin).startsWith("HTTP/1.1 403"));
    result = phoneRequest(server, "GET", "/api/state?after=" + QByteArray::number(qint64(state["after"].toDouble())) +
                                    "&revision=" + QByteArray::number(state["revision"].toVariant().toULongLong()), {}, cookie);
    const auto changed = QJsonDocument::fromJson(result.mid(result.indexOf("\r\n\r\n") + 4)).object();
    QVERIFY(changed["reset"].toBool());
    for (int i = 0; i < 5; ++i)
        QVERIFY(phoneRequest(server, "POST", "/api/pair", R"({"code":"wrong"})", {}, origin).startsWith("HTTP/1.1 403"));
    QVERIFY(phoneRequest(server, "POST", "/api/pair", payload, {}, origin).startsWith("HTTP/1.1 429"));
    server.stop(); QVERIFY(!server.isActive()); QVERIFY(server.sessions.isEmpty());
}

void NativeTests::alertCardsAndEventParsing() {
    const auto parsed = parseIrc("@id=alert;login=alice;display-name=Alice;system-msg=Alice\\ssubscribed!;"
                                 "emotes=25:0-4 :tmi.twitch.tv USERNOTICE #guicraft_ :Kappa");
    QCOMPARE(parsed.message.alertText, QString("Subscribed!"));
    QCOMPARE(parsed.message.text.mid(parsed.message.alertBodyStart), QString("Kappa"));
    QCOMPARE(Message::fromJson(parsed.message.json()).alertText, parsed.message.alertText);
    const auto bits = parseIrc("@id=bits;bits=100;display-name=Bob :bob!bob@bob PRIVMSG #guicraft_ :Cheer100 yay");
    QCOMPARE(bits.message.alertText, QString("Cheered 100 Bits"));
    const auto reward = parseIrc("@id=reward;custom-reward-id=123;display-name=Bob :bob!bob@bob PRIVMSG #guicraft_ :hello");
    QCOMPARE(reward.message.alertText, QString("Redeemed a channel reward"));
    auto m = parsed.message; m.source = twitch->key; m.seq = 777;
    ChatView view(&app->hub); view.resize(430, 200); view.show(); view.setMessages({m});
    const auto text = view.document(0)->toPlainText();
    QVERIFY(text.contains(QString("Alice")));
    QVERIFY(text.contains("Subscribed!"));
    QVERIFY(text.contains("Alice: "));
    QVERIFY(text.indexOf("Subscribed!") < text.indexOf("Alice: "));
    view.viewport()->repaint(); QCoreApplication::processEvents();
    const auto image = view.viewport()->grab().toImage();
    QVERIFY(image.pixelColor(1, 4).blue() > 100);
    const auto gift = parseYouTube({{"id","g"}, {"authorName", QJsonObject{{"simpleText","Viewer"}}},
                                    {"message", QJsonObject{{"simpleText","Sent a gift"}}}},
                                   "liveChatGiftMessageRenderer");
    QCOMPARE(gift.special, QString("alert"));
    QCOMPARE(gift.alertText, QString("Sent a gift"));
}

void NativeTests::savedChannelNamesWithoutAt() {
    app->showSettings("Saved channels");
    auto *settings = app->settings.data();
    auto *platform = settings->findChild<QComboBox *>("saved-add-platform");
    auto *address = settings->findChild<QLineEdit *>("saved-add-address");
    auto *button = settings->findChild<QPushButton *>("saved-add-button");
    QVERIFY(platform && address && button);
    QString path;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray,QByteArray>, BytesCallback done) {
        path = url.path();
        done(R"(var ytInitialData = {"metadata":{"channelMetadataRenderer":{"externalId":"UCSJ4gkVC6NrvII8umztf0Ow","title":"Lofi Girl"}}};)", {});
    };
    platform->setCurrentIndex(1); address->setText("LofiGirl"); button->click();
    QCOMPARE(path, QString("/@LofiGirl"));
    auto saved = app->cfg.data["saved_channels"].toArray();
    QCOMPARE(str(saved.last().toObject()["channel_id"]), QString("UCSJ4gkVC6NrvII8umztf0Ow"));
    platform->setCurrentIndex(0); address->setText("Alpha"); button->click();
    platform->setCurrentIndex(2); address->setText("Beta"); button->click();
    saved = app->cfg.data["saved_channels"].toArray();
    QCOMPARE(str(saved[1].toObject()["channel_id"]), QString("alpha"));
    QCOMPARE(str(saved[2].toObject()["channel_id"]), QString("beta"));
}


void NativeTests::youtubeMentionKeepsDisplayName() {
    auto *youtube = app->hub.source("youtube", "0sfDth4lMLc");
    QJsonObject author{{"id", "message"}, {"authorName", QJsonObject{{"simpleText", "Lofi Girl"}}},
                       {"authorExternalChannelId", "UCabcdefghijklmnopqrstuv"},
                       {"authorEndpoint", QJsonObject{{"browseEndpoint", QJsonObject{{"canonicalBaseUrl", "/@LofiGirl"}}}}},
                       {"message", QJsonObject{{"simpleText", "hello"}}}};
    auto message = parseYouTube(author, "liveChatTextMessageRenderer");
    QCOMPARE(message.name, QString("Lofi Girl")); QCOMPARE(message.login, QString("Lofi Girl"));
    youtube->history << message;
    int requests = 0;
    app->net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray,QByteArray>, BytesCallback done) {
        ++requests; done({}, "No lookup expected");
    };
    auto *pane = app->addWorkspace({youtube})->focusedPane.data();
    app->show(); app->activateWindow(); pane->input->setFocus(Qt::OtherFocusReason); QCoreApplication::processEvents();
    QTest::keyClicks(pane->input, "@Lof");
    QTRY_VERIFY(pane->input->suggestionPopup && pane->input->suggestionPopup->isVisible());
    QCOMPARE(pane->input->suggestionPopup->item(0)->text(), QString("@Lofi Girl"));
    QTest::keyClick(pane->input, Qt::Key_Tab);
    QCOMPARE(pane->input->text(), QString("@Lofi Girl "));
    QCOMPARE(requests, 0);
}
