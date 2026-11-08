#include "native_tests.hpp"
#include "vod.hpp"

void NativeTests::replayAlertsUseEventCards() {
    const QStringList texts{
        "Alice subscribed with Prime. They've subscribed for 27 months, currently on a 1 month streak! ",
        "Alice subscribed at Tier 1. They've subscribed for 20 months! ",
        "Alice is gifting 1 Tier 1 Subs to Cellbit's community! They've gifted a total of 1 in the channel! ",
        "Alice gifted a Tier 1 sub to felipsfire! ",
        "I subscribed with Prime yesterday", "Alice subscribed with Prime today", "hello"};
    app->net.transport = [texts](QUrl, QByteArray, QByteArray, QMap<QByteArray,QByteArray>, BytesCallback done) {
        QJsonArray edges; int index = 0;
        for (const auto &text : texts) {
            const auto id=QString::number(++index);
            QJsonObject node{{"id", id}, {"contentOffsetSeconds", index},
                {"commenter", QJsonObject{{"id", "1"}, {"displayName", "Alice"}, {"login", "alice"}}},
                {"message", QJsonObject{{"fragments", QJsonArray{QJsonObject{{"text", text}}}}}}};
            edges.append(QJsonObject{{"cursor",id},{"node",node}});
        }
        done(QJsonDocument(QJsonObject{{"data",QJsonObject{{"video",QJsonObject{{"comments",QJsonObject{
            {"edges",edges},{"pageInfo",QJsonObject{{"hasNextPage",false}}}}}}}}}}).toJson(), {});
    };
    VodReplay replay(&app->hub); QSignalSpy received(&replay, &VodReplay::message);
    replay.playback(QUrl("https://www.twitch.tv/videos/123"), 10000, true, false);
    QCOMPARE(received.size(), texts.size());
    const QStringList kinds{"prime","subscription","gift","gift","","",""};
    for (int i=0;i<received.size();++i) {
        const auto m=qvariant_cast<Message>(received[i][0]); QCOMPARE(m.alertKind,kinds[i]);
        QCOMPARE(m.special.isEmpty(), kinds[i].isEmpty());
        if (!kinds[i].isEmpty()) { QVERIFY(!m.alertText.startsWith("Alice ")); QCOMPARE(m.alertBodyStart,m.text.size()); }
    }
    auto prime=qvariant_cast<Message>(received[0][0]); prime.seq=1; prime.source=twitch->key;
    ChatView view(&app->hub); view.setMessages({prime});
    QVERIFY(view.document(0)->toPlainText().contains("subscribed with Prime"));
    QVERIFY(view.details.value(1).imageCodes.values().contains("[Alert]"));
    QCOMPARE(parseYouTube({},"liveChatPaidMessageRenderer").alertKind, QString("money"));
    QCOMPARE(parseYouTube({},"liveChatMembershipItemRenderer").alertKind, QString("subscription"));
    QCOMPARE(parseYouTube({},"liveChatSponsorshipsGiftPurchaseAnnouncementRenderer").alertKind, QString("gift"));
    const QJsonObject celebration{{"type","subscription"}};
    const QJsonObject metadata{{"celebration",celebration}};
    const auto kick=parseKick(QJsonObject{{"id","k"},{"type","celebration"},{"metadata",metadata}},true);
    QCOMPARE(kick.special, QString("alert")); QCOMPARE(kick.alertKind, QString("subscription"));
}

void NativeTests::detectedVodCanBeReopened() {
    int replayRequests=0;
    app->net.transport = [&replayRequests](QUrl url, QByteArray, QByteArray raw, QMap<QByteArray,QByteArray>, BytesCallback done) {
        auto body=QJsonDocument::fromJson(raw).object();
        if (url.host()=="gql.twitch.tv" && str(body["operationName"])=="VideoCommentsByOffsetOrCursor") {
            ++replayRequests;
            QJsonObject comments{{"edges",QJsonArray{}},{"pageInfo",QJsonObject{{"hasNextPage",false}}}};
            done(QJsonDocument(QJsonObject{{"data",QJsonObject{{"video",QJsonObject{{"comments",comments}}}}}}).toJson(), {});
        } else done({}, "fixture");
    };
    app->startVodBridge(0); const QString client="a5b27e91-a65a-4e23-85d6-d298f4c1ac12";
    QJsonObject state{{"tab",1},{"url","https://www.twitch.tv/videos/123"},{"seconds",10},{"paused",true}};
    const int before=app->tabs->count(); app->vodBridge->processState(client,state);
    QCOMPARE(app->tabs->count(),before);
    auto *vod=app->hub.sources.value("vod:twitch:123"); QVERIFY(vod && vod->vodReplay->announced);
    QVERIFY(!vod->vodReplay->initialized);
    QCOMPARE(replayRequests,1);
    for(int i=0;i<5;++i) app->vodBridge->processState(client,state);
    QCOMPARE(replayRequests,1);
    auto checkList=[&] {
        app->addChats(); auto dialogs=app->findChildren<QDialog *>(); QDialog *dialog=nullptr;
        for(auto *d:dialogs) if(d->isVisible() && d->windowTitle()=="Add chats") dialog=d;
        QVERIFY(dialog); auto *list=dialog->findChild<QListWidget *>("add-chat-vods");
        QVERIFY(list); QCOMPARE(list->count(),1); QCOMPARE(list->item(0)->data(Qt::UserRole).toString(),vod->vodUrl.toString());
        dialog->close();
    };
    checkList();
    app->addWorkspace({vod}); app->vodBridge->processState(client,state); QVERIFY(vod->vodReplay->initialized);
    app->closeTab(app->tabs->currentIndex()); app->pruneSources();
    QVERIFY(app->hub.sources.value(vod->key)==vod); QVERIFY(!vod->header["vod_shown"].toBool());
    QVERIFY(!vod->vodReplay->initialized); checkList();
    const int closedRequests=replayRequests;
    for(int i=0;i<5;++i) app->vodBridge->processState(client,state);
    QCOMPARE(replayRequests,closedRequests);
    app->openChats({"vod|"+vod->vodUrl.toString()},"tabs"); app->vodBridge->processState(client,state);
    QVERIFY(vod->vodReplay->initialized); QVERIFY(vod->header["vod_shown"].toBool());
}
