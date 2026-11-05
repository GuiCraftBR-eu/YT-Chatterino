#include "native_tests.hpp"
#include "phone.hpp"

static QByteArray phone241(PhoneServer &server, QByteArray method, QByteArray path, QByteArray body = {}, QByteArray cookie = {}) {
    QTcpSocket socket; QEventLoop loop; QTimer deadline; deadline.setSingleShot(true); QByteArray response;
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
    socket.connectToHost(QHostAddress::LocalHost, server.listener.serverPort()); deadline.start(5000); loop.exec();
    response += socket.readAll(); socket.abort(); return response;
}
static QJsonObject phone241Json(QByteArray response) {
    return QJsonDocument::fromJson(response.mid(response.indexOf("\r\n\r\n") + 4)).object();
}

void NativeTests::resizableStreamHeaderAndAlertBackground() {
    twitch->header["title"] = QString("A long stream title with several lines of words ").repeated(10);
    auto *workspace = app->addWorkspace({twitch});
    auto *pane = workspace->focusedPane.data();
    pane->resize(300, 400); pane->show(); QCoreApplication::processEvents(); pane->refresh();
    QCoreApplication::processEvents();
    const int font = pane->headerText->font().pointSize();
    QVERIFY(pane->headerText->heightForWidth(220) > QFontMetrics(pane->headerText->font()).lineSpacing() * 2);
    auto *handle = pane->headerResize;
    const QPoint local(2, 2), global = handle->mapToGlobal(local);
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(2, -200), QPointF(global.x(), global.y()-202), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(2,-200), QPointF(global.x(), global.y()-202), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &release);
    QCOMPARE(pane->headerLines, 1);
    QCOMPARE(pane->headerText->font().pointSize(), font);
    QCOMPARE(pane->headerText->maximumHeight(), QFontMetrics(pane->headerText->font()).lineSpacing());
    const auto saved = pane->serialize(); QCOMPARE(saved["header_lines"].toInt(), 1);
    Workspace restored(&app->hub); auto *restoredWidget = restored.restoreNode(saved); QVERIFY(restoredWidget);
    auto *restoredPane = qobject_cast<Pane *>(restoredWidget); QVERIFY(restoredPane); QCOMPARE(restoredPane->headerLines, 1);
    Message alert; alert.source = twitch->key; alert.id = "alert"; alert.name = "Viewer"; alert.login = "viewer";
    alert.text = "Subscribed!"; alert.special = "alert"; alert.alertText = alert.text; alert.alertBodyStart = alert.text.size(); alert.seq = 100;
    ChatView view(&app->hub); view.resize(700, 160); view.show(); view.setMessages({alert});
    view.viewport()->repaint(); QCoreApplication::processEvents();
    const auto screenshot = view.viewport()->grab().toImage();
    const auto bg = screenshot.pixelColor(screenshot.width() - 20, 5);
    QVERIFY2(bg.red() == bg.green() && bg.green() == bg.blue(), qPrintable(bg.name()));
}

void NativeTests::phoneMentionModerationAndChannelEditing() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    twitch->ownTwitchUserId = "42"; twitch->twitchModeratorKnown = true; twitch->twitchModerator = true;
    Message alice; alice.id="a"; alice.uid="111"; alice.name="Alice"; alice.login="alice"; alice.text="Alice history"; twitch->publish(alice);
    Message bob; bob.id="b"; bob.uid="222"; bob.name="Bob"; bob.login="bob"; bob.text="Hello @Alice and @Nobody https://example.org/@Ignored"; twitch->publish(bob);
    const qint64 bobSeq = twitch->history.last().seq;
    PhoneServer server(&app->hub); QVERIFY(server.start(pane, QHostAddress::LocalHost));
    const auto pair = QJsonDocument(QJsonObject{{"code",server.pairingCode()}}).toJson(QJsonDocument::Compact);
    const auto paired = phone241(server,"POST","/api/pair",pair); QVERIFY(paired.startsWith("HTTP/1.1 200"));
    QByteArray cookie; for (auto line : paired.split('\n')) if (line.startsWith("Set-Cookie: ")) cookie = line.trimmed().mid(12).split(';').first();
    QVERIFY(cookie.startsWith("ytphone="));
    auto state = phone241Json(phone241(server,"GET","/api/state?after=0",{},cookie));
    auto row = state["messages"].toArray().last().toObject();
    QCOMPARE(row["mentions"].toArray().size(), 2);
    QCOMPARE(str(row["mentions"].toArray().first().toObject()["name"]), QString("Alice"));
    const auto reference = QByteArray::number(bobSeq);
    const auto path = QByteArray("/api/user-history?source=twitch%3Aguicraft_&seq=") + reference;
    auto history = phone241Json(phone241(server,"GET",path+"&mention=0",{},cookie));
    QCOMPARE(str(history["name"]), QString("Alice")); QCOMPARE(history["messages"].toArray().size(), 1);
    QVERIFY(history["canModerateUser"].toBool());
    auto absent = phone241Json(phone241(server,"GET",path+"&mention=1",{},cookie));
    QCOMPARE(str(absent["name"]), QString("Nobody")); QVERIFY(absent["messages"].toArray().isEmpty());
    QVERIFY(!absent["canModerateUser"].toBool());
    QVERIFY(phone241(server,"GET",path+"&mention=999",{},cookie).startsWith("HTTP/1.1 400"));
    const auto editBody = QJsonDocument(QJsonObject{{"source",twitch->key},{"title","Fixture title"},{"categoryId","123"}}).toJson(QJsonDocument::Compact);
    QVERIFY(phone241(server,"POST","/api/channel",editBody,cookie).startsWith("HTTP/1.1 403"));
    QVERIFY(phone241(server,"GET","/api/categories?source=twitch%3Aguicraft_&q=game",{},cookie).startsWith("HTTP/1.1 403"));
    twitch->header["channel_id"]="42"; twitch->header["title"]="Previous";
    int searches=0, edits=0, bans=0;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray body, QMap<QByteArray,QByteArray>, BytesCallback done) {
        if (url.path()=="/helix/search/categories") { ++searches; done(R"({"data":[{"id":"123","name":"Game"}]})",{}); return; }
        if (url.path()=="/helix/channels" && method=="PATCH") {
            ++edits; const auto data=QJsonDocument::fromJson(body).object();
            QCOMPARE(str(data["title"]),QString("Fixture title")); QCOMPARE(str(data["game_id"]),QString("123")); done("{}",{}); return;
        }
        if (url.path()=="/helix/moderation/bans" && method=="POST") {
            ++bans; QCOMPARE(str(at(QJsonDocument::fromJson(body).object(),{"data","user_id"})),QString("111")); done("{}",{}); return;
        }
        done({},"Unexpected fixture request");
    };
    const auto categories=phone241Json(phone241(server,"GET","/api/categories?source=twitch%3Aguicraft_&q=game",{},cookie));
    QCOMPARE(categories["categories"].toArray().size(),1); QCOMPARE(searches,1);
    QVERIFY(phone241(server,"POST","/api/channel",editBody,cookie).startsWith("HTTP/1.1 200")); QCOMPARE(edits,1);
    auto banBody=QJsonDocument(QJsonObject{{"source",twitch->key},{"seq",double(bobSeq)},{"mention",0},{"action","timeout"},{"duration",60}}).toJson(QJsonDocument::Compact);
    QVERIFY(phone241(server,"POST","/api/moderate",banBody,cookie).startsWith("HTTP/1.1 200")); QCOMPARE(bans,1);
    banBody=QJsonDocument(QJsonObject{{"source",twitch->key},{"seq",double(bobSeq)},{"mention",99},{"action","ban"}}).toJson(QJsonDocument::Compact);
    QVERIFY(phone241(server,"POST","/api/moderate",banBody,cookie).startsWith("HTTP/1.1 403")); QCOMPARE(bans,1);
    Message owner; owner.id="owner"; owner.uid="42"; owner.name="Owner"; owner.login="owner"; owner.text="owner"; twitch->publish(owner);
    banBody=QJsonDocument(QJsonObject{{"source",twitch->key},{"seq",double(twitch->history.last().seq)},{"action","ban"}}).toJson(QJsonDocument::Compact);
    QVERIFY(phone241(server,"POST","/api/moderate",banBody,cookie).startsWith("HTTP/1.1 403")); QCOMPARE(bans,1);
    server.stop();
}

void NativeTests::phoneKickAndYouTubeChannelActions() {
    auto *kick = app->hub.source("kick", "mine"); kick->header = {{"channel_id",42},{"channel","Mine"},{"title","Old"}};
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc"); yt->header = {{"channel_id","UCtest"},{"channel","My YouTube"},{"title","Old"},{"live_chat_id","chat"}};
    app->accounts.kick = {{"user_id",42},{"access_token","fixture"},{"scope","channel:write moderation:ban moderation:chat_message:manage"},{"expires_at",double(QDateTime::currentSecsSinceEpoch()+600)}};
    auto *pane = app->addWorkspace({kick,yt})->focusedPane.data();
    Message km; km.id="km"; km.uid="77"; km.name="KickUser"; km.login="kickuser"; km.text="hello"; kick->publish(km);
    Message ym; ym.id="ym"; ym.uid="UCviewer"; ym.name="YTUser"; ym.login="YTUser"; ym.text="hello"; yt->publish(ym);
    PhoneServer server(&app->hub); QVERIFY(server.start(pane,QHostAddress::LocalHost));
    const auto pair=QJsonDocument(QJsonObject{{"code",server.pairingCode()}}).toJson(QJsonDocument::Compact);
    const auto paired=phone241(server,"POST","/api/pair",pair); QByteArray cookie;
    for(auto line:paired.split('\n')) if(line.startsWith("Set-Cookie: ")) cookie=line.trimmed().mid(12).split(';').first();
    QVERIFY(cookie.startsWith("ytphone="));
    int kickEdits=0,kickBans=0,ytGets=0,ytEdits=0,ytBans=0,kickSearches=0;
    app->net.transport=[&](QUrl url,QByteArray method,QByteArray body,QMap<QByteArray,QByteArray>,BytesCallback done) {
        const auto data=QJsonDocument::fromJson(body).object();
        if(url.path()=="/public/v1/categories") {++kickSearches; done(R"({"data":[{"id":123,"name":"Game"}]})",{}); return;}
        if(url.path()=="/public/v1/channels" && method=="PATCH") {++kickEdits; QCOMPARE(str(data["stream_title"]),QString("Kick title")); QCOMPARE(data["category_id"].toInt(),123); done("{}",{}); return;}
        if(url.path()=="/public/v1/moderation/bans") {++kickBans; QCOMPARE(data["user_id"].toInt(),77); QCOMPARE(data["duration"].toInt(),1); done("{}",{}); return;}
        if(url.path().endsWith("/videos") && method=="GET") {++ytGets; done(R"({"items":[{"snippet":{"title":"Old","categoryId":"20","description":"Keep"}}]})",{}); return;}
        if(url.path().endsWith("/videos") && method=="PUT") {++ytEdits; QCOMPARE(str(at(data,{"snippet","title"})),QString("YouTube title")); QCOMPARE(str(at(data,{"snippet","description"})),QString("Keep")); done("{}",{}); return;}
        if(url.path().endsWith("/liveChat/bans")) {++ytBans; QCOMPARE(str(at(data,{"snippet","bannedUserDetails","channelId"})),QString("UCviewer")); done("{}",{}); return;}
        done({},"Unexpected fixture request");
    };
    auto request=[](QJsonObject o){return QJsonDocument(o).toJson(QJsonDocument::Compact);};
    const auto categories=phone241Json(phone241(server,"GET","/api/categories?source=kick%3Amine&q=game",{},cookie));
    QCOMPARE(categories["categories"].toArray().size(),1); QCOMPARE(kickSearches,1);
    QVERIFY(phone241(server,"POST","/api/channel",request({{"source",kick->key},{"title","Kick title"},{"categoryId","123"}}),cookie).startsWith("HTTP/1.1 200")); QCOMPARE(kickEdits,1);
    QVERIFY(phone241(server,"POST","/api/channel",request({{"source",yt->key},{"title","YouTube title"},{"categoryId","123"}}),cookie).startsWith("HTTP/1.1 403"));
    QVERIFY(phone241(server,"POST","/api/channel",request({{"source",yt->key},{"title","YouTube title"},{"categoryId",""}}),cookie).startsWith("HTTP/1.1 200")); QCOMPARE(ytGets,1); QCOMPARE(ytEdits,1);
    QVERIFY(phone241(server,"POST","/api/moderate",request({{"source",kick->key},{"seq",double(kick->history.last().seq)},{"action","timeout"},{"duration",60}}),cookie).startsWith("HTTP/1.1 200")); QCOMPARE(kickBans,1);
    QVERIFY(phone241(server,"POST","/api/moderate",request({{"source",yt->key},{"seq",double(yt->history.last().seq)},{"action","timeout"},{"duration",60}}),cookie).startsWith("HTTP/1.1 200")); QCOMPARE(ytBans,1);
    QVERIFY(phone241(server,"POST","/api/moderate",request({{"source",yt->key},{"seq",double(yt->history.last().seq)},{"action","unban"}}),cookie).startsWith("HTTP/1.1 403"));
    server.stop();
}
