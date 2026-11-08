#include "native_tests.hpp"

void NativeTests::youtubePacingAndRateLimitFallback() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    yt->recommendedMs = 10000;
    QCOMPARE(yt->youtubeWebDelay(), 5000);
    yt->interval = 12;
    QCOMPARE(yt->youtubeWebDelay(), 12000);
    yt->interval = 4;
    yt->recommendedMs = 15000;
    QCOMPARE(yt->youtubeWebDelay(), 15000);
    yt->recommendedMs = 10000;
    yt->youtubeError("HTTP 429: rate limit");
    QVERIFY(app->hub.youtubeConservativeUntil > app->hub.youtubeClock.elapsed());
    QCOMPARE(yt->youtubeWebDelay(), 10000);
    auto *another = app->hub.source("youtube", "pumlxj4Ct_g");
    another->recommendedMs = 10000;
    QCOMPARE(another->youtubeWebDelay(), 10000);
    app->hub.youtubeConservativeUntil = 0;
    QCOMPARE(another->youtubeWebDelay(), 5000);
}

void NativeTests::youtubeBurstKeepsOrderAndModeration() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    auto action = [](const QString &id) {
        const QJsonObject render{{"id", id}, {"authorName", QJsonObject{{"simpleText", "Viewer"}}},
                                 {"authorExternalChannelId", "UCviewer"},
                                 {"message", QJsonObject{{"runs", QJsonArray{QJsonObject{{"text", id}}}}}}};
        return QJsonObject{{"addChatItemAction", QJsonObject{
            {"item", QJsonObject{{"liveChatTextMessageRenderer", render}}}}}};
    };
    yt->youtubeActions(QJsonArray{action("one"), action("two"), action("three")}, false);
    QCOMPARE(yt->history.size(), 1);
    QCOMPARE(yt->youtubePending.size(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(yt->history.size(), 3, 1000);
    QCOMPARE(yt->history[0].id, "one");
    QCOMPARE(yt->history[1].id, "two");
    QCOMPARE(yt->history[2].id, "three");
    yt->youtubeActions(QJsonArray{action("four"), action("five")}, false);
    QCOMPARE(yt->youtubePending.size(), 1);
    yt->redact({{"message_id", "five"}});
    QVERIFY(yt->youtubePending.isEmpty());
    QCOMPARE(yt->history.last().id, "five");
    QVERIFY(yt->history.last().deleted);
    yt->youtubeActions(QJsonArray{action("six"), action("seven")}, false);
    yt->setPaused(true);
    QVERIFY(yt->youtubePending.isEmpty());
    QCOMPARE(yt->history[yt->history.size() - 2].id, "seven");
    yt->youtubeActions(QJsonArray{action("eight"), action("nine")}, false);
    yt->stop();
    QVERIFY(yt->youtubePending.isEmpty());
    QVERIFY(!yt->youtubeDisplayTimer.isActive());

    auto *busy = app->hub.source("youtube", "pumlxj4Ct_g");
    QVector<Message> burst;
    for (int i = 0; i < 400; ++i) {
        Message m; m.id = QString::number(i); m.name = "Viewer"; m.text = "message";
        burst.append(m);
    }
    busy->queueYoutubeMessages(burst);
    QVERIFY(busy->youtubeDisplayChunk >= 2);
    QTRY_COMPARE_WITH_TIMEOUT(busy->history.size(), 400, 3000);
    QCOMPARE(busy->history.first().id, "0");
    QCOMPARE(busy->history.last().id, "399");
}

void NativeTests::followDatesAndPermissions() {
    QStringList paths;
    bool noFollow = false, privateSubscriptions = false;
    QJsonObject result;
    QString error;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray> headers, BytesCallback done) {
        paths << url.path();
        QVERIFY(headers.value("Authorization").startsWith("Bearer "));
        const auto query = QUrlQuery(url);
        if (url.path() == "/helix/channels/followers") {
            QCOMPARE(query.queryItemValue("broadcaster_id"), QString("12"));
            QCOMPARE(query.queryItemValue("user_id"), QString("77"));
            if (noFollow) done(R"({"data":[]})", {});
            else done(R"({"data":[{"user_id":"77","followed_at":"2020-05-04T12:00:00Z"}]})", {});
        } else if (url.path() == "/helix/channels/followed") {
            QCOMPARE(query.queryItemValue("user_id"), QString("42"));
            QCOMPARE(query.queryItemValue("broadcaster_id"), QString("12"));
            done(R"({"data":[{"broadcaster_id":"12","followed_at":"2021-07-03T12:00:00Z"}]})", {});
        } else if (url.path() == "/youtube/v3/subscriptions") {
            QCOMPARE(query.queryItemValue("channelId"), QString("UCviewer"));
            QCOMPARE(query.queryItemValue("forChannelId"), QString("UCowner"));
            if (privateSubscriptions) done({}, "HTTP 403: subscriptionForbidden");
            else done(R"({"items":[{"snippet":{"publishedAt":"2022-01-06T12:00:00Z"}}]})", {});
        } else done({}, "unexpected");
    };
    auto request = [&](Source *source, QString user) {
        result = {}; error.clear();
        app->hub.followSince(source, user, [&](QJsonObject d, QString e) { result = d; error = e; });
    };
    request(twitch, "77");
    QCOMPARE(str(result["status"]), "permission");
    QCOMPARE(str(result["scope"]), "moderator:read:followers");
    QVERIFY(paths.isEmpty());
    auto scopes = app->accounts.twitch["scope"].toArray();
    scopes.append("moderator:read:followers");
    scopes.append("user:read:follows");
    app->accounts.twitch["scope"] = scopes;
    twitch->ownTwitchUserId = "42";
    twitch->twitchModeratorKnown = true;
    twitch->twitchModerator = true;
    request(twitch, "77");
    QCOMPARE(str(result["status"]), "following");
    QCOMPARE(str(result["date"]), "2020-05-04T12:00:00Z");
    QCOMPARE(paths.size(), 1);
    noFollow = true;
    request(twitch, "77");
    QCOMPARE(str(result["status"]), "not_following");
    QCOMPARE(paths.size(), 2);
    twitch->twitchModerator = false;
    request(twitch, "77");
    QCOMPARE(str(result["status"]), "moderator_only");
    QCOMPARE(paths.size(), 2);
    request(twitch, "42");
    QCOMPARE(str(result["status"]), "following");
    QCOMPARE(str(result["date"]), "2021-07-03T12:00:00Z");
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    yt->header["channel_id"] = "UCowner";
    const int before = app->quota.used();
    request(yt, "UCviewer");
    QCOMPARE(str(result["status"]), "following");
    QCOMPARE(str(result["date"]), "2022-01-06T12:00:00Z");
    QCOMPARE(app->quota.used(), before + 1);
    privateSubscriptions = true;
    request(yt, "UCviewer");
    QCOMPARE(str(result["status"]), "unavailable");
    auto *kick = app->hub.source("kick", "fixture");
    kick->header["channel_id"] = "23";
    request(kick, "77");
    QCOMPARE(str(result["status"]), "unavailable");
}

void NativeTests::followDateUserCardAndPauseIndicator() {
    auto scopes = app->accounts.twitch["scope"].toArray();
    scopes.append("moderator:read:followers");
    app->accounts.twitch["scope"] = scopes;
    twitch->ownTwitchUserId = "42";
    twitch->twitchModeratorKnown = twitch->twitchModerator = true;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        if (url.path() == "/helix/users") {
            done(R"({"data":[{"id":"77","display_name":"Viewer","created_at":"2018-01-01T12:00:00Z"}]})", {});
        } else if (url.path() == "/helix/channels/followers") {
            if (QUrlQuery(url).queryItemValue("user_id").isEmpty()) done(R"({"total":12})", {});
            else done(R"({"data":[{"followed_at":"2020-05-04T12:00:00Z"}]})", {});
        } else done({}, "unexpected");
    };
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    Message viewer; viewer.uid = "77"; viewer.name = "Viewer"; viewer.login = "viewer";
    app->show(); app->showUser(pane, twitch, viewer);
    auto card = app->userCards.value(twitch->key + ":77");
    QVERIFY(card);
    auto *follow = card->findChild<QLabel *>("user-card-follow-date");
    QVERIFY(follow);
    QVERIFY(follow->text().contains("Following since:"));
    QVERIFY(follow->text().contains("04/05/2020"));
    auto *timer = card->findChild<QTimer *>("user-card-history-refresh");
    auto *history = card->findChild<ChatView *>();
    QVERIFY(timer && history);
    QSignalSpy refreshes(timer, &QTimer::timeout);
    for (int i = 0; i < 25; ++i) {
        Message message = viewer; message.id = QString::number(i); message.text = "hello";
        twitch->publish(message);
    }
    QTRY_COMPARE_WITH_TIMEOUT(history->messages.size(), 25, 1000);
    QCOMPARE(refreshes.size(), 1);

    app->cfg.data["pause_on_hover"] = true;
    ChatView view(&app->hub); view.resize(300, 120); view.show();
    QEvent enter(QEvent::Enter); QApplication::sendEvent(view.viewport(), &enter);
    QVERIFY(!view.pauseIndicator->isHidden());
    QCOMPARE(view.pauseIndicator->text(), "Paused");
    QVERIFY(view.pauseIndicator->testAttribute(Qt::WA_TransparentForMouseEvents));
    QEvent leave(QEvent::Leave); QApplication::sendEvent(view.viewport(), &leave);
    QVERIFY(view.pauseIndicator->isHidden());
}
