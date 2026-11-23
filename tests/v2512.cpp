#include "native_tests.hpp"
#include "vod.hpp"

void NativeTests::similarChannelNumberVariants() {
    auto *source = app->hub.source("twitch", "cellbit");
    auto *pane = app->addWorkspace({source})->focusedPane.data();
    QSet<QString> paths;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        paths.insert(url.path());
        if (url.path() == "/@livesdo_cellb1t/streams")
            done(R"(ytInitialData = {"contents":[{"videoRenderer":{"videoId":"abcdefghijk","title":{"simpleText":"Live"},"badges":[{"style":"LIVE"}]}}]};)", {});
        else done({}, "HTTP 404");
    };
    app->suggestSimilarLive(source, pane, true);
    for (const auto &path : {"/@cellbit_/streams", "/@cellb1t/streams", "/@cellb1t_/streams", "/@livesdo_cellb1t/streams", "/@ce11b1t/streams", "/@live_cellb1t/streams"})
        QVERIFY2(paths.contains(path), path);
    auto *list = app->findChild<QListWidget *>("similar-live-channels"); QVERIFY(list);
    QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->checkState(), Qt::Unchecked);
    QCOMPARE(pane->sources.size(), 1);
    qobject_cast<QDialog *>(list->window())->reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    paths.clear();
    auto *reverse = app->hub.source("kick", "c3llb1t");
    pane->setSources({reverse});
    app->suggestSimilarLive(reverse, pane, true);
    QVERIFY(paths.contains("/gql"));
    // Twitch queries use their JSON request body, so also exercise reverse mapping against YouTube.
    QVERIFY(paths.contains("/@cellbit/streams"));
    QVERIFY(paths.size() <= 150);

    paths.clear();
    auto *longName = app->hub.source("kick", "aaaaaaaaaaaio");
    pane->setSources({longName});
    app->suggestSimilarLive(longName, pane, true);
    QVERIFY(paths.contains("/@aaaaaaaaaaa1o/streams"));
    QVERIFY(paths.contains("/@aaaaaaaaaaai0/streams"));
}

void NativeTests::reconnectSuggestsLiveChannels() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    int lookups = 0;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        ++lookups;
        if (url.path() == "/api/v2/channels/guicraft_")
            done(R"({"livestream":{"session_title":"Kick live"}})", {});
        else done({}, "HTTP 404");
    };
    QMenu menu; app->populatePaneMenu(menu, pane);
    auto *reconnect = menu.findChild<QAction *>("channel-reconnect"); QVERIFY(reconnect);
    const auto epoch = twitch->generation;
    app->testMode = false;
    reconnect->trigger();
    app->testMode = true;
    QCOMPARE(twitch->generation, epoch + 1);
    QVERIFY(lookups > 0);
    auto *list = app->findChild<QListWidget *>("similar-live-channels"); QVERIFY(list);
    QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->checkState(), Qt::Unchecked);
    QCOMPARE(pane->sources.size(), 1);
    qobject_cast<QDialog *>(list->window())->reject();
}

void NativeTests::similarSearchShowsEarlyAndStopsWhenClosed() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    QList<BytesCallback> pending;
    int requests = 0;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        ++requests;
        if (url.path() == "/api/v2/channels/guicraft_")
            done(R"({"livestream":{"session_title":"Kick live"}})", {});
        else pending << done;
    };
    app->suggestSimilarLive(twitch, pane, true);
    auto *list = app->findChild<QListWidget *>("similar-live-channels"); QVERIFY(list);
    QCOMPARE(list->count(), 1); QVERIFY(list->window()->isVisible());
    QVERIFY(!pending.isEmpty());
    const int started = requests;
    qobject_cast<QDialog *>(list->window())->reject();
    const auto outstanding = pending;
    for (const auto &reply : outstanding) reply({}, "HTTP 404");
    QCOMPARE(requests, started);
}

void NativeTests::twitchChannelPointMessagesHighlightSeparately() {
    auto settings = app->cfg.feature("highlight");
    settings["alerts"] = false; settings["mention_me"] = false;
    settings["words"] = QJsonArray(); settings["users"] = QJsonObject(); settings["roles"] = QJsonObject();
    settings["alert_color"] = "#553366";
    app->cfg.setFeature("highlight", settings);
    auto highlighted = parseIrc("@msg-id=highlighted-message :bob!b@b PRIVMSG #guicraft_ :Hello").message;
    auto reward = parseIrc("@custom-reward-id=abc :bob!b@b PRIVMSG #guicraft_ :Reward message").message;
    auto bits = parseIrc("@bits=50 :bob!b@b PRIVMSG #guicraft_ :Cheer50").message;
    auto normal = parseIrc(":bob!b@b PRIVMSG #guicraft_ :Hello").message;
    Message kickReward; kickReward.source = "kick:creator"; kickReward.special = "alert"; kickReward.alertKind = "reward";
    QVERIFY(highlighted.activity["channel_points"].toBool());
    QVERIFY(reward.activity["channel_points"].toBool());
    QVERIFY(!bits.activity["channel_points"].toBool());
    QVERIFY(!normal.activity["channel_points"].toBool());
    QCOMPARE(highlight(highlighted, app->cfg).name(), QString("#553366"));
    QCOMPARE(highlight(reward, app->cfg).name(), QString("#553366"));
    QVERIFY(!highlight(bits, app->cfg).isValid());
    QVERIFY(!highlight(normal, app->cfg).isValid());
    QVERIFY(!highlight(kickReward, app->cfg).isValid());
    auto restored = Message::fromJson(reward.json());
    QVERIFY(restored.activity["channel_points"].toBool());

    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    highlighted.seq = 1; reward.seq = 2; bits.seq = 3; normal.seq = 4;
    for (auto *message : {&highlighted, &reward, &bits, &normal}) message->source = twitch->key;
    pane->view->setMessages({highlighted, reward, bits, normal});
    QCOMPARE(pane->view->highlightColors[0].name(), QString("#553366"));
    QCOMPARE(pane->view->highlightColors[1].name(), QString("#553366"));
    QVERIFY(!pane->view->highlightColors[2].isValid());
    QVERIFY(!pane->view->highlightColors[3].isValid());

    app->showSettings("Highlights");
    auto *toggle = app->settings->findChild<QCheckBox *>("highlight-channel-points"); QVERIFY(toggle);
    QVERIFY(toggle->isChecked()); toggle->setChecked(false); app->settings->applyButton->click();
    QVERIFY(!app->cfg.feature("highlight")["channel_points"].toBool(true));
    QVERIFY(!highlight(highlighted, app->cfg).isValid());
    QVERIFY(!pane->view->highlightColors[0].isValid());
    settings = app->cfg.feature("highlight"); settings["alerts"] = true; app->cfg.setFeature("highlight", settings);
    QVERIFY(highlight(bits, app->cfg).isValid());
    QVERIFY(!highlight(highlighted, app->cfg).isValid());
}

void NativeTests::browserMixesRecognizeNumberVariants() {
    app->vodBridge = new VodBridge(&app->hub, app.get(), 0, false);
    app->vodBridge->liveItems.insert("one", {{"platform", "twitch"}, {"id", "cellbit"}, {"name", "Cellbit"}});
    app->vodBridge->liveItems.insert("two", {{"platform", "youtube"}, {"id", "abcdefghijk"}, {"name", "Ce11b1t Live"}});
    app->addChats();
    auto *mixes = app->findChild<QListWidget *>("add-chat-browser-mixes"); QVERIFY(mixes);
    QCOMPARE(mixes->count(), 1);
    const auto group = mixes->item(0)->data(Qt::UserRole).toJsonArray();
    QCOMPARE(group.size(), 2);
}

void NativeTests::popupEnterSendsWithoutOpeningEmotes() {
    auto *workspace = app->addWorkspace({twitch});
    for (bool overlay : {false, true}) {
        app->openPopup(workspace->focusedPane, overlay);
        auto popup = app->popupWindows.last(); QVERIFY(popup);
        auto *pane = popup->findChild<Pane *>(); QVERIFY(pane);
        QSignalSpy emoteClicks(pane->emoteButton, &QPushButton::clicked);
        int sent = 0; BytesCallback pending;
        app->net.transport = [&](QUrl url, QByteArray method, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
            if (url.path() == "/helix/chat/messages" && method == "POST") { ++sent; pending = done; }
            else done({}, "HTTP 404");
        };
        pane->input->setFocus();
        pane->input->setText("hello");
        QTest::keyClick(pane->input, Qt::Key_Return);
        QCOMPARE(sent, 1); QCOMPARE(emoteClicks.count(), 0); QVERIFY(pane->input->isReadOnly());
        QTest::keyClick(pane->input, Qt::Key_Return);
        QCOMPARE(sent, 1); QCOMPARE(emoteClicks.count(), 0);
        QVERIFY(bool(pending)); pending(R"({"data":[{"is_sent":true,"message_id":"1"}]})", {});
        QVERIFY(pane->input->text().isEmpty());
        pane->input->setText("again");
        QTest::keyClick(pane->input, Qt::Key_Enter);
        QCOMPARE(sent, 2); QCOMPARE(emoteClicks.count(), 0);
        pending(R"({"data":[{"is_sent":true,"message_id":"2"}]})", {});
        QTest::keyClick(pane->input, Qt::Key_Return);
        QCOMPARE(sent, 2); QCOMPARE(emoteClicks.count(), 0);
        popup->close();
        app->net.transport = [](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
            done({}, "HTTP 404");
        };
    }
}

void NativeTests::reconnectKeepsSpecificYouTubeVideo() {
    auto *selected = app->hub.source("youtube", "abcdefghijk");
    auto *other = app->hub.source("youtube", "pumlxj4Ct_g");
    selected->header = {{"channel", "Same creator"}, {"title", "Selected broadcast"}};
    selected->context = {{"client", QJsonObject{{"clientVersion", "1"}}}};
    other->header = {{"channel", "Same creator"}, {"title", "Other broadcast"}};
    other->continuation = "other-token";
    QList<QUrl> requests; QList<BytesCallback> replies;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        requests << url; replies << done;
    };
    app->hub.testMode = false;
    selected->reconnect();
    QCOMPARE(requests.size(), 1); QCOMPARE(requests[0].path(), QString("/live_chat"));
    QCOMPARE(QUrlQuery(requests[0]).queryItemValue("v"), selected->id);
    selected->reconnect();
    QCOMPARE(requests.size(), 2); QCOMPARE(QUrlQuery(requests[1]).queryItemValue("v"), selected->id);
    const QByteArray page = R"(ytInitialData = {"contents":{"liveChatRenderer":{"continuations":[{"timedContinuationData":{"continuation":"selected-token","timeoutMs":5000}}]}}};)";
    replies[0](page, {}); QVERIFY(selected->continuation.isEmpty());
    replies[1](page, {}); QCOMPARE(selected->continuation, QString("selected-token"));
    QCOMPARE(other->continuation, QString("other-token"));
    QCOMPARE(str(selected->header["title"]), QString("Selected broadcast"));
    selected->poll.stop();
    selected->context = {};
    selected->reconnect();
    QCOMPARE(requests.size(), 3); QCOMPARE(requests[2].path(), QString("/watch"));
    QCOMPARE(QUrlQuery(requests[2]).queryItemValue("v"), selected->id);
    replies[2](R"(ytInitialPlayerResponse = {"videoDetails":{"videoId":"pumlxj4Ct_g","title":"Wrong broadcast"}};)", {});
    QCOMPARE(str(selected->header["title"]), QString("Selected broadcast"));
    QCOMPARE(other->continuation, QString("other-token"));
    selected->poll.stop(); app->hub.testMode = true;
}

void NativeTests::youtubeChatIdRejectsStaleOrWrongVideo() {
    auto *selected = app->hub.source("youtube", "abcdefghijk");
    QList<BytesCallback> replies;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        if (url.path() == "/youtube/v3/videos") {
            QCOMPARE(QUrlQuery(url).queryItemValue("id"), selected->id);
            replies << done;
        } else done({}, "HTTP 404");
    };
    QString result, error;
    app->hub.youtubeChatId(selected, [&](QString id, QString e) { result = id; error = e; });
    QCOMPARE(replies.size(), 1);
    selected->reconnect();
    replies[0](R"({"items":[{"id":"abcdefghijk","liveStreamingDetails":{"activeLiveChatId":"old"}}]})", {});
    QVERIFY(result.isEmpty()); QVERIFY(!error.isEmpty()); QVERIFY(str(selected->header["live_chat_id"]).isEmpty());
    error.clear();
    app->hub.youtubeChatId(selected, [&](QString id, QString e) { result = id; error = e; });
    QCOMPARE(replies.size(), 2);
    replies[1](R"({"items":[{"id":"pumlxj4Ct_g","liveStreamingDetails":{"activeLiveChatId":"wrong"}}]})", {});
    QVERIFY(result.isEmpty()); QVERIFY(!error.isEmpty()); QVERIFY(str(selected->header["live_chat_id"]).isEmpty());
}
