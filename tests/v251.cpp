#include "native_tests.hpp"
#include "vod.hpp"

void NativeTests::browserLiveSuggestionsAndNavigation() {
    int twitchChecks=0, youtubeChecks=0, kickChecks=0;
    app->net.transport=[&](QUrl url,QByteArray,QByteArray body,QMap<QByteArray,QByteArray>,BytesCallback done) {
        if(url.host()=="gql.twitch.tv") {
            ++twitchChecks; const auto login=str(QJsonDocument::fromJson(body).object()["variables"].toObject()["login"]);
            const bool live=login=="felps";
            done(QJsonDocument(QJsonObject{{"data",QJsonObject{{"user",QJsonObject{{"displayName","Felps"},
                {"stream",live ? QJsonValue(QJsonObject{{"id","123"}}) : QJsonValue(QJsonValue::Null)}}}}}}).toJson(),{});
        } else if(url.host()=="www.youtube.com") {
            ++youtubeChecks;
            done(R"(ytInitialPlayerResponse = {"videoDetails":{"videoId":"abcdefghijk","author":"FelpsLIVE","isLive":true}};)",{});
        } else if(url.host()=="kick.com") { ++kickChecks; done(R"({"livestream":null})",{}); }
        else done({},"fixture");
    };
    app->startVodBridge(0); const QString client="a5b27e91-a65a-4e23-85d6-d298f4c1ac12";
    const int tabs=app->tabs->count();
    auto send=[&](int tab,QString url) { app->vodBridge->processLiveState(client,{{"tab",tab},{"url",url}}); };
    send(1,"https://www.twitch.tv/felps");send(2,"https://www.youtube.com/watch?v=abcdefghijk");
    send(3,"https://kick.com/offline");send(4,"https://www.twitch.tv/videos/123");
    send(5,"https://www.twitch.tv.evil.test/felps");
    QCOMPARE(twitchChecks,1); QCOMPARE(youtubeChecks,1); QCOMPARE(kickChecks,1);
    QCOMPARE(app->vodBridge->liveItems.size(),2); QCOMPARE(app->tabs->count(),tabs);
    for(int i=0;i<4;++i)send(1,"https://www.twitch.tv/felps");
    QCOMPARE(twitchChecks,1);
    app->show();app->addChats(); QDialog *dialog=nullptr;
    for(auto *d:app->findChildren<QDialog*>())if(d->isVisible()&&d->windowTitle()=="Add chats")dialog=d;
    QVERIFY(dialog);auto *list=dialog->findChild<QListWidget*>("add-chat-browser-live");
    auto *mixes=dialog->findChild<QListWidget*>("add-chat-browser-mixes");
    auto *layout=dialog->findChild<QComboBox*>("add-chat-layout");
    QVERIFY(list&&mixes&&layout);QCOMPARE(list->count(),2);QCOMPARE(mixes->count(),1);
    QMetaObject::invokeMethod(mixes,"itemClicked",Qt::DirectConnection,Q_ARG(QListWidgetItem*,mixes->item(0)));
    QCOMPARE(layout->currentIndex(),3);QCOMPARE(list->item(0)->checkState(),Qt::Checked);QCOMPARE(list->item(1)->checkState(),Qt::Checked);
    dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Open)->click();
    QVERIFY(app->currentPane());QVERIFY(app->currentPane()->view->mixed);QCOMPARE(app->currentPane()->sources.size(),2);
    QVERIFY(app->hub.sources.contains("twitch:felps"));QVERIFY(app->hub.sources.contains("abcdefghijk"));
    app->addChats();dialog=nullptr;for(auto *d:app->findChildren<QDialog*>())if(d->isVisible()&&d->windowTitle()=="Add chats")dialog=d;
    QVERIFY(dialog);list=dialog->findChild<QListWidget*>("add-chat-browser-live");mixes=dialog->findChild<QListWidget*>("add-chat-browser-mixes");
    app->vodBridge->removeLivePlayer(client+":2");QCOMPARE(list->count(),1);QCOMPARE(mixes->count(),0);
    app->vodBridge->disconnectClient(client);QCOMPARE(list->count(),0);
    QVERIFY(VodBridge::identifyLive(QUrl("https://www.youtube.com/watch?v=abcdefghijk")).size()>0);
    QVERIFY(VodBridge::identifyLive(QUrl("http://www.twitch.tv/felps")).isEmpty());
    QVERIFY(VodBridge::identifyLive(QUrl("https://www.twitch.tv.evil.test/felps")).isEmpty());
}

void NativeTests::f5ReloadsEmotesWithoutReconnecting() {
    int bttv=0,seven=0,native=0; auto *other=app->hub.source("youtube","abcdefghijk");
    other->header["channel_id"]="yt123";
    auto old=[](QString code,QString url){ Span s;s.code=code;s.urls={url};return s; };
    twitch->channelEmotes["OldB"]=old("OldB","https://cdn.betterttv.net/emote/old/3x");
    twitch->channelEmotes["Old7"]=old("Old7","https://cdn.7tv.app/emote/old/4x.webp");
    twitch->emotes=twitch->channelEmotes;
    app->net.transport=[&](QUrl url,QByteArray,QByteArray,QMap<QByteArray,QByteArray>,BytesCallback done) {
        if(url.host()=="api.betterttv.net") { ++bttv; done(R"({"channelEmotes":[{"id":"new","code":"NewB"}],"sharedEmotes":[]})",{}); }
        else if(url.host()=="7tv.io") { ++seven; done({},"HTTP 503"); }
        else if(url.host()=="api.twitch.tv" && url.path().endsWith("/chat/emotes")) { ++native;done(R"({"data":[{"id":"n","name":"NewNative"}]})",{}); }
        else done({},"fixture");
    };
    auto *pane=app->addWorkspace({twitch,other})->focusedPane.data();QVERIFY(pane);
    app->show();app->activateWindow();pane->input->setFocus();QTest::qWait(30);
    const auto firstGeneration=twitch->generation, secondGeneration=other->generation;
    QTest::keyClick(pane->input,Qt::Key_F5);
    QTRY_COMPARE(bttv,2);QCOMPARE(seven,2);QCOMPARE(native,1);
    QCOMPARE(twitch->generation,firstGeneration);QCOMPARE(other->generation,secondGeneration);
    QVERIFY(!twitch->channelEmotes.contains("OldB"));QVERIFY(twitch->channelEmotes.contains("NewB"));
    QVERIFY(twitch->channelEmotes.contains("Old7"));QVERIFY(twitch->channelEmotes.contains("NewNative"));
    QVERIFY(std::any_of(twitch->history.cbegin(),twitch->history.cend(),[](const Message &m){return m.system && m.text=="BetterTTV channel emotes reloaded.";}));
    QTest::keyClick(pane->input,Qt::Key_F5,Qt::ControlModifier);
    QTRY_VERIFY(twitch->generation>firstGeneration);QVERIFY(other->generation>secondGeneration);
}
