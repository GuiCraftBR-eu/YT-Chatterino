#include "native_tests.hpp"
#include "vod.hpp"

static QJsonObject replayEdge(QString id, int offset, QString text) {
    QJsonObject message{{"fragments", QJsonArray{QJsonObject{{"text", text}}}},
        {"userBadges", QJsonArray{QJsonObject{{"setID", "premium"}, {"version", "1"}},
                                   QJsonObject{{"setID", "subscriber"}, {"version", "1"}},
                                   QJsonObject{{"setID", ""}, {"version", ""}}}}};
    QJsonObject node{{"id", id}, {"contentOffsetSeconds", offset}, {"createdAt", "2020-01-01T00:00:00Z"},
                     {"commenter", QJsonObject{{"id", "99"}, {"displayName", "Viewer"}, {"login", "viewer"}}},
                     {"message", message}};
    return {{"cursor", id}, {"node", node}};
}

void NativeTests::vodAutomaticTabsMixAndBadges() {
    int globalRequests = 0, channelRequests = 0;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray raw, QMap<QByteArray,QByteArray> headers, BytesCallback done) {
        if (url.host() != "gql.twitch.tv") { done({}, "unexpected"); return; }
        QVERIFY(!headers.contains("Authorization"));
        const auto body = QJsonDocument::fromJson(raw).object();
        const QString query = str(body["query"]);
        QJsonObject data;
        if (str(body["operationName"]) == "VideoCommentsByOffsetOrCursor") {
            const auto vars = body["variables"].toObject();
            const QString video = str(vars["videoID"]);
            const int offset = vars["contentOffsetSeconds"].toInt() + 10;
            const QString room = video == "111" ? "101" : "202";
            QJsonObject comments{{"edges", QJsonArray{replayEdge(video + '-' + QString::number(offset), offset, "replay " + video)}},
                                 {"pageInfo", QJsonObject{{"hasNextPage", false}}}};
            data["video"] = QJsonObject{{"creator", QJsonObject{{"id", room}}}, {"comments", comments}};
        } else if (query.contains("query{badges")) {
            ++globalRequests;
            data["badges"] = QJsonArray{QJsonObject{{"setID", "premium"}, {"version", "1"}, {"title", "Prime"},
                {"imageURL", "https://static-cdn.jtvnw.net/badges/v1/global/2"}},
                QJsonObject{{"setID", "subscriber"}, {"version", "1"}, {"title", "Global"},
                {"imageURL", "https://static-cdn.jtvnw.net/badges/v1/wrong/2"}}};
        } else if (query.contains("broadcastBadges")) {
            ++channelRequests;
            const auto room = str(body["variables"].toObject()["id"]);
            data["user"] = QJsonObject{{"id", room}, {"displayName", "Channel " + room},
                {"profileImageURL", "https://static-cdn.jtvnw.net/avatar/" + room},
                {"broadcastBadges", QJsonArray{QJsonObject{{"setID", "subscriber"}, {"version", "1"}, {"title", "Subscriber"},
                    {"imageURL", "https://static-cdn.jtvnw.net/badges/v1/" + room + "/2"}}}}};
        } else if (query.contains("video(id")) {
            data["video"] = QJsonObject{{"title", "Recorded stream"}};
        } else { done({}, "unexpected query"); return; }
        done(QJsonDocument(QJsonObject{{"data", data}}).toJson(QJsonDocument::Compact), {});
    };
    app->startVodBridge(0); QVERIFY(app->vodBridge && app->vodBridge->server.isListening());
    const QString client = "a5b27e91-a65a-4e23-85d6-d298f4c1ac12";
    auto state = [&](int tab, QString video, double seconds, bool seek = false) {
        app->vodBridge->processState(client, {{"tab",tab},{"url","https://www.twitch.tv/videos/"+video},
                                             {"seconds",seconds},{"paused",true},{"seek",seek}});
    };
    const int initialTabs = app->tabs->count();
    state(1,"111",12.5); state(2,"222",12.5);
    auto *first = app->hub.sources.value("vod:twitch:111");
    auto *second = app->hub.sources.value("vod:twitch:222");
    QVERIFY(first && second && first != second); QCOMPARE(app->tabs->count(), initialTabs);
    QCOMPARE(globalRequests, 1); QCOMPARE(channelRequests, 2);
    QCOMPARE(first->name(), "Channel 101"); QCOMPARE(second->name(), "Channel 202");
    QVERIFY(first->history.isEmpty()); QVERIFY(second->history.isEmpty());
    auto *mixed = app->addWorkspace({first, second})->focusedPane.data();
    state(1,"111",12.5); state(2,"222",12.5);
    QCOMPARE(first->history.size(), 1); QCOMPARE(second->history.size(), 1);
    QCOMPARE(first->history[0].badges.size(), 2);
    QCOMPARE(first->badges.value("premium/1").urls.value(0), "https://static-cdn.jtvnw.net/badges/v1/global/2");
    QCOMPARE(first->badges.value("subscriber/1").urls.value(0), "https://static-cdn.jtvnw.net/badges/v1/101/2");
    QVERIFY(mixed && mixed->view->mixed); QCOMPARE(mixed->view->messages.size(), 2);
    QVERIFY(mixed->composer->isHidden()); QCOMPARE(mixed->target->count(), 0);
    auto *withLive = app->addWorkspace({first, twitch, second})->focusedPane.data();
    QCOMPARE(withLive->target->count(), 1); QCOMPARE(withLive->targetSource(), twitch);
    QVERIFY(!withLive->composer->isHidden()); QVERIFY(withLive->target->isHidden());
    Message live; live.name = "Live"; live.id = "live"; live.text = "live message"; live.stamp = QDateTime::currentDateTime();
    twitch->publish(live); state(1,"111",30.5,true);
    QCOMPARE(second->history.size(), 1); QCOMPARE(first->history.size(), 1);
    QVERIFY(std::any_of(mixed->view->messages.cbegin(), mixed->view->messages.cend(), [second](const Message &m){return m.source==second->key;}));
    withLive->reload(); QCOMPARE(withLive->view->messages.last().source, first->key);
    QVERIFY(!app->hub.canEditChannel(first)); QVERIFY(!app->hub.hasModerationRole(first));
    bool called = false; app->hub.send(first,"should not send",{},[&](QJsonObject,QString error){ called=true; QVERIFY(!error.isEmpty()); });
    QVERIFY(called);
}

void NativeTests::vodSessionRestoreAndSuppression() {
    const QUrl url("https://kick.com/xqc/videos/01a1b308-7f68-76ba-83ad-c88cc3a54dcf");
    auto *source = app->hub.vodSource(url); QVERIFY(source && source->isVod);
    auto *pane = app->addWorkspace({source, twitch})->focusedPane.data(); QVERIFY(pane);
    app->saveSession(); const QString profile = app->cfg.home;
    MainWindow restored(profile, true);
    auto *saved = restored.hub.sources.value(source->key); QVERIFY(saved && saved->isVod);
    QCOMPARE(saved->vodUrl, url); QVERIFY(restored.currentPane()->view->mixed);
    QCOMPARE(restored.currentPane()->target->count(), 1);
    app->startVodBridge(0);
    app->vodBridge->suppress(source->key);
    app->vodBridge->processState("a5b27e91-a65a-4e23-85d6-d298f4c1ac12", {{"tab",1},{"url",url.toString()},{"seconds",12},{"paused",true}});
    QVERIFY(!source->vodReplay->initialized);
    app->vodBridge->allow(source->key);
    app->vodBridge->processState("a5b27e91-a65a-4e23-85d6-d298f4c1ac12", {{"tab",1},{"url",url.toString()},{"seconds",12},{"paused",true}});
    QVERIFY(source->vodReplay->initialized);
}

void NativeTests::vodBrowserOriginsAndReadOnlyModes() {
    QVERIFY(!VodBridge::allowedOrigin("https://www.twitch.tv"));
    QVERIFY(!VodBridge::allowedOrigin("http://127.0.0.1"));
    QVERIFY(!VodBridge::allowedOrigin("chrome-extension://abcdefghijklmnopabcdefghijklmnop.evil.test"));
    QVERIFY(VodBridge::allowedOrigin("chrome-extension://abcdefghijklmnopabcdefghijklmnop"));
    QVERIFY(VodBridge::allowedOrigin("moz-extension://a5b27e91-a65a-4e23-85d6-d298f4c1ac12"));
    auto *vod = app->hub.vodSource(QUrl("https://www.twitch.tv/videos/12345")); QVERIFY(vod);
    auto *pane = app->addWorkspace({vod})->focusedPane.data(); QVERIFY(pane);
    Message m; m.source=vod->key; m.id="m"; m.name="Viewer";
    QVERIFY(!pane->view->canReply(m)); QVERIFY(pane->composer->isHidden());
    QMenu menu; app->populatePaneMenu(menu, pane);
    QVERIFY(!menu.findChild<QAction *>("channel-edit"));
}
