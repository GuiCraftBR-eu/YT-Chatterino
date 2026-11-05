#include "native_tests.hpp"
#include "phone.hpp"

static Message deletedFixture(QString id, QString uid, QString login) {
    Message m; m.id = id; m.uid = uid; m.login = login; m.name = login; m.text = "hello";
    m.stamp = QDateTime::currentDateTime(); return m;
}

void NativeTests::deletionReasonsFromPlatforms() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    app->hub.twitch.channels[twitch->id] = twitch;
    twitch->publish(deletedFixture("single", "1", "alice"));
    twitch->publish(deletedFixture("timeout", "1", "alice"));
    twitch->publish(deletedFixture("ban", "2", "bob"));
    twitch->publish(deletedFixture("untouched", "3", "charlie"));
    twitch->publish(deletedFixture("no-tags", "4", "dave"));
    app->hub.twitch.line("@target-msg-id=single :tmi.twitch.tv CLEARMSG #guicraft_ :hello");
    QCOMPARE(twitch->history[0].deletionReason, QString("deleted"));
    app->hub.twitch.line("@target-user-id=1;ban-duration=350 :tmi.twitch.tv CLEARCHAT #guicraft_ :alice");
    QCOMPARE(twitch->history[0].deletionReason, QString("deleted")); // Earlier individual deletion is not relabelled.
    QCOMPARE(twitch->history[1].deletionReason, QString("timeout"));
    QCOMPARE(twitch->history[1].deletionDuration, qint64(350));
    QCOMPARE(deletionReasonText(twitch->history[1], app->cfg), QString("Timed out for 5m 50s"));
    QVERIFY(!twitch->history[2].deleted);
    app->hub.twitch.line("@target-user-id=2 :tmi.twitch.tv CLEARCHAT #guicraft_ :bob");
    QCOMPARE(twitch->history[2].deletionReason, QString("ban"));
    QVERIFY(!twitch->history[3].deleted);
    app->hub.twitch.line(":tmi.twitch.tv CLEARCHAT #guicraft_ :dave"); // Match the login, not the whole room; no tags means no known cause.
    QCOMPARE(twitch->history[4].deletionReason, QString("removed"));
    QVERIFY(!twitch->history[3].deleted);
    app->cfg.data["show_deleted_messages"] = true;
    pane->reload();
    QVERIFY(pane->view->document(1)->toPlainText().contains("hello · Timed out for 5m 50s"));
    QVERIFY(pane->view->document(2)->toPlainText().contains("hello · Banned"));
    app->cfg.data["show_deleted_messages"] = false;
    pane->reload();
    QVERIFY(pane->view->document(1)->toPlainText().contains("[message deleted] · Timed out for 5m 50s"));
    auto saved = Message::fromJson(twitch->history[1].json());
    QCOMPARE(saved.deletionReason, QString("timeout")); QCOMPARE(saved.deletionDuration, qint64(350));
    app->cfg.data["language"] = "pt-BR";
    QCOMPARE(deletionReasonText(saved, app->cfg), QString("Silenciado por 5m 50s"));
    app->hub.twitch.line(":tmi.twitch.tv CLEARCHAT #guicraft_");
    QCOMPARE(twitch->history[3].deletionReason, QString("clear"));

    auto *kick = app->hub.source("kick", "example");
    kick->publish(deletedFixture("k1", "10", "one"));
    kick->publish(deletedFixture("k2", "11", "two"));
    kick->publish(deletedFixture("k3", "12", "three"));
    kick->kickEvent("UserBannedEvent", {{"user", QJsonObject{{"id", 10}}}, {"permanent", false}, {"duration", 2}});
    kick->kickEvent("UserBannedEvent", {{"user", QJsonObject{{"id", 11}}}, {"permanent", true}});
    kick->kickEvent("UserBannedEvent", {{"user", QJsonObject{{"id", 12}}}});
    QCOMPARE(kick->history[0].deletionReason, QString("timeout"));
    QCOMPARE(kick->history[0].deletionDuration, qint64(120));
    QCOMPARE(kick->history[1].deletionReason, QString("ban"));
    QCOMPARE(kick->history[2].deletionReason, QString("removed"));

    auto *mine = app->hub.source("kick", "mine"); mine->header["channel_id"] = 42;
    app->accounts.kick = {{"user_id", 42}, {"access_token", "fixture"},
        {"scope", "user:read moderation:ban"}, {"expires_at", double(QDateTime::currentSecsSinceEpoch() + 600)}};
    app->net.transport = [](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) { done("{}", {}); };
    const auto victim = deletedFixture("local", "77", "victim"); mine->publish(victim);
    QString error = "not called";
    app->hub.moderate(mine, victim, "timeout", 90, [&](QJsonObject, QString e) { error = e; });
    QVERIFY(error.isEmpty()); QCOMPARE(mine->history.first().deletionReason, QString("timeout"));
    QCOMPARE(mine->history.first().deletionDuration, qint64(120)); // Kick rounds timeouts up to minutes.

    auto *youtube = app->hub.source("youtube", "abcdefghijk");
    youtube->publish(deletedFixture("yt1", "UCtest", "example"));
    youtube->youtubeActions({QJsonObject{{"markChatItemsByAuthorAsDeletedAction", QJsonObject{{"externalChannelId", "UCtest"}}}}});
    QCOMPARE(youtube->history[0].deletionReason, QString("removed")); // The web event provides no ban details.
}

static QByteArray request244(PhoneServer &server, QByteArray method, QByteArray path, QByteArray body = {}, QByteArray cookie = {}) {
    QTcpSocket socket; QEventLoop loop; QTimer deadline; deadline.setSingleShot(true); QByteArray response;
    QObject::connect(&socket, &QTcpSocket::connected, &loop, [&] {
        QByteArray request = method + ' ' + path + " HTTP/1.1\r\nHost: " + server.address().mid(7).toLatin1() + "\r\n";
        if (!cookie.isEmpty()) request += "Cookie: " + cookie + "\r\n";
        if (method == "POST") request += "Origin: " + server.address().toLatin1() + "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
        socket.write(request + "\r\n" + body);
    });
    QObject::connect(&socket, &QTcpSocket::readyRead, &loop, [&] { response += socket.readAll(); });
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    socket.connectToHost(QHostAddress::LocalHost, server.listener.serverPort()); deadline.start(5000); loop.exec();
    response += socket.readAll(); socket.abort(); return response;
}

void NativeTests::deletionReasonsOnPhone() {
    app->cfg.data["show_deleted_messages"] = true;
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    twitch->publish(deletedFixture("phone", "55", "viewer"));
    PhoneServer server(&app->hub); QVERIFY(server.start(pane, QHostAddress::LocalHost));
    const auto payload = QJsonDocument(QJsonObject{{"code", server.pairingCode()}}).toJson(QJsonDocument::Compact);
    const auto response = request244(server, "POST", "/api/pair", payload); QVERIFY(response.startsWith("HTTP/1.1 200"));
    QByteArray cookie; for (auto line : response.split('\n')) if (line.startsWith("Set-Cookie: ")) cookie = line.trimmed().mid(12).split(';').first();
    QVERIFY(cookie.startsWith("ytphone="));
    twitch->redact({{"user_id", "55"}, {"reason", "timeout"}, {"duration", 600}});
    const auto raw = request244(server, "GET", "/api/state?after=0", {}, cookie);
    const auto state = QJsonDocument::fromJson(raw.mid(raw.indexOf("\r\n\r\n") + 4)).object();
    const auto message = state["messages"].toArray().last().toObject();
    QCOMPARE(str(message["text"]), QString("hello"));
    QCOMPARE(str(message["deletionLabel"]), QString("Timed out for 10m"));
    app->cfg.data["show_deleted_messages"] = false;
    const auto rawHidden = request244(server, "GET", "/api/state?after=0", {}, cookie);
    const auto hidden = QJsonDocument::fromJson(rawHidden.mid(rawHidden.indexOf("\r\n\r\n") + 4)).object()["messages"].toArray().last().toObject();
    QCOMPARE(str(hidden["text"]), QString("[message deleted]"));
    QCOMPARE(str(hidden["deletionLabel"]), QString("Timed out for 10m"));
    server.stop();
}
