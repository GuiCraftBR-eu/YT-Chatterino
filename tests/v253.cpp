#include "native_tests.hpp"
#include "vod.hpp"

static Message sample253(qint64 seq, const QString &source, const QString &name, const QString &text = "hello") {
    Message m; m.seq = seq; m.id = QString::number(seq); m.source = source;
    m.name = name; m.login = name; m.text = text; m.stamp = QDateTime::currentDateTime();
    return m;
}

void NativeTests::mixedRightClickAndYoutubeMentions() {
    auto *youtube = app->hub.source("youtube", "abcdefghijk");
    auto *pane = app->addWorkspace({twitch, youtube})->focusedPane.data();
    disconnect(pane, &Pane::messageMenu, app.get(), &MainWindow::messageMenu);
    auto *view = pane->view;
    auto yt = sample253(100, youtube->key, "@DisplayName"); yt.login = "channel-id";
    auto tw = sample253(101, twitch->key, "TwitchUser");
    view->setMessages({yt, tw}); app->resize(600, 400); app->show(); QTest::qWait(50);
    view->verticalScrollBar()->setValue(0);
    QSignalSpy mentions(view, &ChatView::mentionRequested), replies(view, &ChatView::replyRequested), menus(view, &ChatView::messageMenuRequested);
    const QPoint youtubePos(50, view->offsets[0] + 5);
    QTest::mousePress(view->viewport(), Qt::RightButton, Qt::NoModifier, youtubePos);
    QCOMPARE(mentions.size(), 1); QCOMPARE(pane->target->currentData().toString(), youtube->key);
    QCOMPARE(pane->input->text(), QString("@DisplayName "));
    QContextMenuEvent suppressed(QContextMenuEvent::Mouse, youtubePos, view->viewport()->mapToGlobal(youtubePos));
    QCoreApplication::sendEvent(view->viewport(), &suppressed); QCOMPARE(menus.size(), 0);
    QTest::mouseRelease(view->viewport(), Qt::RightButton, Qt::NoModifier, youtubePos);
    QContextMenuEvent controlled(QContextMenuEvent::Mouse, youtubePos, view->viewport()->mapToGlobal(youtubePos), Qt::ControlModifier);
    QCoreApplication::sendEvent(view->viewport(), &controlled); QCOMPARE(menus.size(), 1);
    const QPoint twitchPos(50, view->offsets[1] + 5);
    QTest::mousePress(view->viewport(), Qt::RightButton, Qt::NoModifier, twitchPos);
    QCOMPARE(replies.size(), 1); QCOMPARE(pane->target->currentData().toString(), twitch->key); QCOMPARE(pane->reply.id, tw.id);
    QTest::mouseRelease(view->viewport(), Qt::RightButton, Qt::NoModifier, twitchPos);
    pane->input->clear(); yt.name = "Display Name"; pane->mentionUser(yt);
    QCOMPARE(pane->input->text(), QString("@Display Name "));
    QVERIFY(pane->reply.id.isEmpty());
}

void NativeTests::highlightedScrollbarTracksMessages() {
    auto settings = app->cfg.feature("highlight"); settings["users"] = QJsonObject{{"special", "#20b050"}};
    app->cfg.setFeature("highlight", settings);
    ChatView view(&app->hub); view.resize(350, 180); view.messageCap = 120; view.show();
    for (int i = 0; i < 120; ++i) view.append(sample253(1000+i, twitch->key, i == 50 ? "special" : "other"));
    QTest::qWait(40); QVERIFY(view.verticalScrollBar()->maximum() > 0);
    QCOMPARE(view.highlightColors.size(), view.messages.size());
    QCOMPARE(view.highlightColors[50], QColor("#20b050"));
    const QImage image = view.verticalScrollBar()->grab().toImage();
    bool green = false;
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
        const auto c = image.pixelColor(x,y); if (c.green() > c.red()+25 && c.green() > c.blue()+25) green = true;
    }
    QVERIFY(green);
    view.append(sample253(2000, twitch->key, "other"));
    QCOMPARE(view.messages.size(), 120); QCOMPARE(view.highlightColors.size(), 120);
    QCOMPARE(view.highlightColors[49], QColor("#20b050"));
    settings["users"] = QJsonObject{{"special", "#a020c0"}}; app->cfg.setFeature("highlight", settings);
    QCOMPARE(view.highlightColors[49], QColor("#a020c0"));
}

void NativeTests::twitchCheermotesRenderAndCopy() {
    QVERIFY(Images::allowed(QUrl("https://d3aqoihi2n8ty8.cloudfront.net/actions/cheer/dark/animated/100/2.gif")));
    QVERIFY(!Images::allowed(QUrl("https://unrelated.cloudfront.net/a.gif")));
    const QString shamrock = "https://d3aqoihi2n8ty8.cloudfront.net/actions/shamrock/dark/animated/100/2.gif";
    const QString custom = "https://static-cdn.jtvnw.net/bits/custom.gif";
    for (const auto &url : {shamrock, custom}) {
        auto entry = QSharedPointer<Images::Entry>::create(); entry->image = QImage(24,24,QImage::Format_ARGB32); entry->image.fill(Qt::green);
        app->images.cache[url] = entry;
    }
    twitch->cheermotes = QJsonArray{QJsonObject{{"prefix","CoolBits"},{"tiers",QJsonArray{QJsonObject{
        {"min_bits",10},{"color","#00ff00"},{"images",QJsonObject{{"dark",QJsonObject{{"animated",QJsonObject{{"2",custom}}}}}}}}}}}};
    ChatView view(&app->hub); view.resize(450,300); view.show();
    auto bits = sample253(3000,twitch->key,"Viewer","Shamrock100 CoolBits10"); bits.special="alert"; bits.alertKind="bits"; bits.alertText="Cheered 110 Bits";
    view.setMessages({bits}); auto doc=view.document(0); QVERIFY(doc);
    const auto details=view.details.value(bits.seq); QCOMPARE(details.emoteImages.size(),2);
    QVERIFY(doc->toPlainText().contains("100")); QVERIFY(doc->toPlainText().contains("10"));
    QStringList codes=details.imageCodes.values(); QVERIFY(codes.contains("Shamrock")); QVERIFY(codes.contains("CoolBits"));
    QVERIFY(view.messageText(0).contains("Shamrock100 CoolBits10"));
    auto ordinary=sample253(3001,twitch->key,"Viewer","Shamrock100"); view.setMessages({ordinary});
    QVERIFY(view.document(0)->toPlainText().contains("Shamrock100")); QVERIFY(view.details.value(ordinary.seq).emoteImages.isEmpty());
    auto *vod=app->hub.vodSource(QUrl("https://www.twitch.tv/videos/123")); QVERIFY(vod);
    auto replay=sample253(3002,vod->key,"Viewer","Shamrock100"); view.setMessages({replay});
    QVERIFY(view.document(0)->toPlainText().contains(QChar::ObjectReplacementCharacter));
    QVERIFY(view.messageText(0).contains("Shamrock100"));
    QImage fixture(20,20,QImage::Format_ARGB32); fixture.fill(Qt::red);
    QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(fixture.save(&buffer,"PNG"));
    int attempts=0;
    app->net.transport=[&](QUrl url,QByteArray,QByteArray,QMap<QByteArray,QByteArray>,BytesCallback done) {
        ++attempts;
        if (url.path().contains("/10000/")) done(png,{}); else done({},"HTTP 403");
    };
    auto large=sample253(3003,twitch->key,"Viewer","Shamrock100000"); large.special="alert"; large.alertKind="bits"; large.alertText="Cheered 100000 Bits";
    view.setMessages({large}); view.document(0);
    QCOMPARE(attempts,3);
    const QString high="https://d3aqoihi2n8ty8.cloudfront.net/actions/shamrock/dark/animated/100000/2.gif";
    QVERIFY(app->images.cache.contains(high));
    view.invalidate(); QVERIFY(view.messageText(0).contains("Shamrock100000"));
}

void NativeTests::emoteCatalogRetriesIndependently() {
    app->hub.testMode = false;
    int bttv = 0, seven = 0, native = 0;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray,QByteArray>, BytesCallback done) {
        const QString path=url.path();
        if (url.host()=="api.betterttv.net" && path.contains("/users/")) {
            ++bttv; if (bttv==1) done({},"HTTP 503"); else done(R"({"channelEmotes":[{"id":"b","code":"Recovered"}],"sharedEmotes":[]})",{});
        } else if (url.host()=="7tv.io" && path.contains("/users/")) {
            ++seven; done(R"({"emote_set":{"emotes":[]}})",{});
        } else if (url.host()=="api.twitch.tv" && path.endsWith("/chat/emotes")) {
            ++native; done(R"({"data":[]})",{});
        } else if (path.contains("global") && url.host()=="api.betterttv.net") done("[]",{});
        else if (path.contains("global") && url.host()=="7tv.io") done(R"({"emotes":[]})",{});
        else if (path.contains("global") && url.host()=="api.twitch.tv") done(R"({"data":[]})",{});
        else done({},"fixture");
    };
    twitch->loadEmotes("12"); QCOMPARE(bttv,1); QCOMPARE(seven,1); QCOMPARE(native,1); QVERIFY(twitch->emoteRetry.isActive());
    twitch->emoteRetry.stop(); twitch->emoteRetryUntil["12:bttv"] = 0; twitch->loadEmotes("12");
    QCOMPARE(bttv,2); QCOMPARE(seven,1); QCOMPARE(native,1); QVERIFY(twitch->emotes.contains("Recovered"));
    app->hub.testMode = true;
}

void NativeTests::browserSyncAndLateChatAttachment() {
    app->startVodBridge(0); QVERIFY(app->vodBridge);
    const QString client="a5b27e91-a65a-4e23-85d6-d298f4c1ac12";
    const QJsonObject chat{{"available",true},{"pin",QJsonObject{{"text","Already pinned"}}}};
    app->vodBridge->processLiveState(client, {{"tab",1},{"url","https://www.twitch.tv/latechannel"},{"chat",chat}});
    auto *late=app->hub.source("twitch","latechannel"); app->addWorkspace({late});
    QCOMPARE(str(late->browserPinned["text"]),QString("Already pinned"));
    QNetworkRequest request(QUrl(QString("ws://127.0.0.1:%1").arg(app->vodBridge->server.serverPort())));
    request.setRawHeader("Origin", "chrome-extension://abcdefghijklmnopabcdefghijklmnop");
    QWebSocket socket; QSignalSpy received(&socket,&QWebSocket::textMessageReceived); socket.open(request);
    QTRY_COMPARE(socket.state(),QAbstractSocket::ConnectedState);
    socket.sendTextMessage(R"({"type":"hello","client":"f5b27e91-a65a-4e23-85d6-d298f4c1ac12"})");
    QTRY_VERIFY(received.size()>=1); received.clear(); app->addChats();
    QTRY_VERIFY(!received.isEmpty());
    QCOMPARE(str(QJsonDocument::fromJson(received.last()[0].toString().toUtf8()).object()["type"]),QString("sync-request"));
    BytesCallback delayed;
    app->net.transport=[&](QUrl, QByteArray, QByteArray, QMap<QByteArray,QByteArray>, BytesCallback done) { delayed=std::move(done); };
    app->vodBridge->processLiveState(client, {{"tab",2},{"url","https://www.twitch.tv/newlive"}}); QVERIFY(bool(delayed));
    app->vodBridge->processLiveState(client, {{"tab",2},{"url","https://www.twitch.tv/newlive"},{"live",true},{"name","NewLive"}});
    delayed({},"network failure");
    QCOMPARE(str(app->vodBridge->liveItems.value(client+":2")["name"]),QString("NewLive"));
}

void NativeTests::streamPreviewSurvivesChatScrolling() {
    const QString url="https://static-cdn.jtvnw.net/test/thumbnail.png";
    auto entry=QSharedPointer<Images::Entry>::create(); entry->image=QImage(320,180,QImage::Format_ARGB32); entry->image.fill(Qt::blue);
    app->images.cache[url]=entry; twitch->header["thumbnail"]=url; twitch->header["live"]=true;
    auto *pane=app->addWorkspace({twitch})->focusedPane.data(); app->resize(450,300); app->show();
    for (int i=0;i<100;++i) pane->view->append(sample253(4000+i,twitch->key,"Viewer"));
    QTest::qWait(40); QVERIFY(pane->view->showStreamTooltip(twitch,pane->mapToGlobal(QPoint(20,20))));
    QVERIFY(pane->view->emoteTooltip && pane->view->emoteTooltip->isVisible());
    pane->view->verticalScrollBar()->setValue(0); pane->view->invalidate();
    QVERIFY(pane->view->emoteTooltip->isVisible()); QCOMPARE(pane->view->streamPreviewSource.data(),twitch);
    pane->view->hideEmoteTooltip(); QVERIFY(!pane->view->emoteTooltip->isVisible());
}
