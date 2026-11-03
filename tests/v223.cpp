#include "native_tests.hpp"

void NativeTests::streamHeaderWrapAndOptions() {
    twitch->header["live"] = true;
    twitch->header["viewers"] = 7;
    twitch->header["started_at"] = QDateTime::currentDateTimeUtc().addSecs(-3665).toString(Qt::ISODate);
    const QString longTitle = QString("A very long stream title with several words and details ").repeated(4);
    twitch->header["title"] = longTitle;
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    yt->header = {{"channel", "Video"}, {"title", longTitle}, {"live", true}, {"viewers", 23}};
    auto *extra = app->hub.source("twitch", "extra");
    extra->header = {{"channel", "Extra"}, {"title", "Extra title"}, {"live", false}};
    auto *fourth = app->hub.source("twitch", "fourth");
    fourth->header = {{"channel", "Fourth"}, {"title", "Fourth title"}, {"live", false}};
    Pane pane(&app->hub, {twitch, yt, extra, fourth});
    pane.resize(330, 900);
    pane.show();
    QTest::qWait(100);
    QVERIFY(pane.mixedTitles.size() >= 4);
    QVERIFY(pane.mixedTitles[0]->text().contains(longTitle));
    QVERIFY(pane.mixedTitles[0]->text().startsWith("7 - 01:01:"));
    QVERIFY(pane.mixedTitles[3]->text().contains("Fourth title"));
    QVERIFY(pane.mixedTitles[0]->wordWrap());
    QTRY_VERIFY(pane.mixedTitles[0]->height() >=
                pane.mixedTitles[0]->heightForWidth(pane.mixedTitles[0]->width()));
    auto a = app->cfg.appearance();
    a["header_viewers"] = false; a["header_duration"] = false;
    a["header_category"] = false; a["header_title"] = true;
    app->cfg.setAppearance(a);
    QCOMPARE(pane.mixedTitles[0]->text(), longTitle);
    a["header_title"] = false;
    app->cfg.setAppearance(a);
    QVERIFY(pane.mixedTitles[0]->text().isEmpty());
}

void NativeTests::deletedMessagesOptInAndOpacity() {
    Message message;
    message.id = "deleted";
    message.name = "Example";
    message.text = "A message received before deletion";
    twitch->publish(message);
    Pane pane(&app->hub, {twitch});
    pane.resize(450, 250);
    pane.show();
    twitch->redact({{"message_id", "deleted"}});
    QCOMPARE(twitch->history.last().text, message.text);
    QVERIFY(twitch->history.last().deleted);
    QVERIFY(pane.view->messageText(0).contains("[message deleted]"));
    QCOMPARE(visibleMessageText(twitch->history.last(), app->cfg), "[message deleted]");
    QVERIFY(!accepts(twitch->history.last(),
                     {{"include_terms", QJsonArray{"received before"}}}, &app->cfg));
    app->cfg.data["show_deleted_messages"] = true;
    emit app->cfg.appearanceChanged();
    QVERIFY(pane.view->messageText(0).contains(message.text));
    QCOMPARE(visibleMessageText(twitch->history.last(), app->cfg), message.text);
    QVERIFY(accepts(twitch->history.last(),
                    {{"include_terms", QJsonArray{"received before"}}}, &app->cfg));

    ChatView view(&app->hub);
    view.resize(450, 100);
    view.show();
    auto visible = twitch->history.last();
    visible.deleted = false;
    view.setMessages({visible});
    QTest::qWait(30);
    auto normal = view.viewport()->grab().toImage();
    visible.deleted = true;
    view.setMessages({visible});
    QTest::qWait(30);
    auto faded = view.viewport()->grab().toImage();
    qint64 normalLight = 0, fadedLight = 0;
    for (int y = 0; y < qMin(50, normal.height()); ++y)
        for (int x = 0; x < normal.width(); ++x) {
            const auto a = normal.pixelColor(x, y), b = faded.pixelColor(x, y);
            normalLight += a.red() + a.green() + a.blue();
            fadedLight += b.red() + b.green() + b.blue();
        }
    QVERIFY(normalLight > 0);
    QVERIFY(fadedLight < normalLight * 0.7);
}

void NativeTests::twitchAlertsAndHighlight() {
    const auto p = parseIrc("@id=notice1;login=alice;display-name=Alice;system-msg=Alice\\ssubscribed!;"
                            "emotes=25:0-4 :tmi.twitch.tv USERNOTICE #guicraft_ :Kappa");
    QCOMPARE(p.message.name, "Alice");
    QCOMPARE(p.message.special, "alert");
    QVERIFY(p.message.text.contains("Alice subscribed!"));
    QCOMPARE(p.message.emotes.size(), 1);
    QCOMPARE(p.message.text.mid(p.message.emotes[0].start, 5), "Kappa");
    const auto reward = parseIrc("@id=r1;custom-reward-id=123;display-name=Bob "
                                 ":bob!bob@bob PRIVMSG #guicraft_ :hello");
    QCOMPARE(reward.message.special, "alert");
    app->hub.twitch.channels["guicraft_"] = twitch;
    app->hub.twitch.line("@id=notice2;login=alice;display-name=Alice;system-msg=Alice\\ssubscribed! "
                         ":tmi.twitch.tv USERNOTICE #guicraft_");
    QCOMPARE(twitch->history.last().special, "alert");
    auto h = app->cfg.feature("highlight");
    h["alerts"] = true;
    app->cfg.setFeature("highlight", h);
    QCOMPARE(highlight(twitch->history.last(), app->cfg, {}).name(), "#227400");
}

void NativeTests::youtubeMembershipGiftParsing() {
    const QJsonObject header{{"authorName", QJsonObject{{"simpleText", "Gifter"}}},
                             {"primaryText", QJsonObject{{"simpleText", "Gifted 5 memberships"}}}};
    const QJsonObject item{{"id", "gift1"}, {"authorExternalChannelId", "UCgifter"},
                           {"header", QJsonObject{{"liveChatSponsorshipsHeaderRenderer", header}}}};
    auto message = parseYouTube(item, "liveChatSponsorshipsGiftPurchaseAnnouncementRenderer");
    QCOMPARE(message.name, "Gifter");
    QCOMPARE(message.text, "Gifted 5 memberships");
    QCOMPARE(message.special, "member");
}

void NativeTests::youtubePinAndPollLifecycle() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    Pane pane(&app->hub, {yt});
    pane.resize(500, 400);
    pane.show();
    const QJsonObject renderer{{"id", "m1"}, {"authorName", QJsonObject{{"simpleText", "Owner"}}},
                               {"message", QJsonObject{{"simpleText", "Welcome to the stream"}}}};
    const QJsonObject banner{{"actionId", "pin1"}, {"contents", QJsonObject{
        {"liveChatTextMessageRenderer", renderer}}}};
    yt->youtubeActions({QJsonObject{{"addBannerToLiveChatCommand", QJsonObject{
        {"bannerRenderer", QJsonObject{{"liveChatBannerRenderer", banner}}}}}}});
    QCOMPARE(str(yt->pinned["text"]), "Welcome to the stream");
    QVERIFY(pane.decorations->isVisible());
    auto labels = pane.decorations->findChildren<QLabel *>("chat-decoration");
    QVERIFY(!labels.isEmpty());
    QVERIFY(labels[0]->text().contains("Owner: Welcome to the stream"));

    const QJsonArray options{QJsonObject{{"optionText", "Yes"}, {"tally", "20"}},
                             QJsonObject{{"optionText", "No"}, {"tally", "5"}}};
    const QJsonObject metadata{{"questionText", "Play another?"}, {"status", "active"}, {"options", options}};
    yt->youtubeApiPollState({{"activePollItem", QJsonObject{{"snippet", QJsonObject{
        {"pollDetails", QJsonObject{{"metadata", metadata}}}}}}}});
    QCOMPARE(str(yt->activePoll["text"]), "Play another?");
    QVERIFY(str(yt->activePoll["options"]).contains("Yes (20)"));
    yt->youtubeApiPollState({});
    QVERIFY(yt->activePoll.isEmpty());
    yt->youtubeActions({QJsonObject{{"removeBannerForLiveChatCommand",
                                   QJsonObject{{"targetActionId", "wrong"}}}}});
    QVERIFY(!yt->pinned.isEmpty());
    yt->youtubeActions({QJsonObject{{"removeBannerForLiveChatCommand",
                                   QJsonObject{{"targetActionId", "pin1"}}}}});
    QVERIFY(yt->pinned.isEmpty());
    QVERIFY(!pane.decorations->isVisible());
}

void NativeTests::twitchPinAndPollPermissionAndLifecycle() {
    app->hub.testMode = false;
    twitch->header["channel_id"] = "42";
    QStringList paths;
    bool withPin = true, withPoll = true;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        paths << url.path();
        QJsonArray values;
        if (url.path().endsWith("/chat/pins") && withPin)
            values.append(QJsonObject{{"message_id", "pin"}, {"sender_user_name", "Alice"},
                                      {"message", QJsonObject{{"text", "Pinned text"}}}});
        if (url.path().endsWith("/polls") && withPoll)
            values.append(QJsonObject{{"status", "ACTIVE"}, {"title", "Which game?"},
                                      {"choices", QJsonArray{QJsonObject{{"title", "Chess"}, {"votes", 3}}}}});
        done(QJsonDocument(QJsonObject{{"data", values}}).toJson(), {});
    };
    twitch->refreshDecorations();
    QVERIFY(paths.contains("/helix/chat/pins"));
    QVERIFY(paths.contains("/helix/polls"));
    QCOMPARE(str(twitch->pinned["text"]), "Pinned text");
    QCOMPARE(str(twitch->activePoll["text"]), "Which game?");
    QVERIFY(str(twitch->activePoll["options"]).contains("Chess (3)"));
    withPin = withPoll = false;
    twitch->refreshDecorations();
    QVERIFY(twitch->pinned.isEmpty());
    QVERIFY(twitch->activePoll.isEmpty());
    paths.clear();
    twitch->header["channel_id"] = "other";
    twitch->refreshDecorations();
    QVERIFY(!paths.contains("/helix/polls"));

    QMap<QString, QList<BytesCallback>> pending;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        pending[url.path()].append(done);
    };
    twitch->header["channel_id"] = "42";
    twitch->refreshDecorations();
    QCOMPARE(pending["/helix/chat/pins"].size(), 1);
    QCOMPARE(pending["/helix/polls"].size(), 1);
    app->accounts.twitch = {};
    emit app->accounts.changed();
    QVERIFY(twitch->pinned.isEmpty());
    QVERIFY(twitch->activePoll.isEmpty());
    const QJsonObject oldPin{{"message", QJsonObject{{"text", "Stale pin"}}}};
    pending["/helix/chat/pins"].takeFirst()(
        QJsonDocument(QJsonObject{{"data", QJsonArray{oldPin}}}).toJson(), {});
    const QJsonObject oldPoll{{"status", "ACTIVE"}, {"title", "Stale poll"}};
    pending["/helix/polls"].takeFirst()(
        QJsonDocument(QJsonObject{{"data", QJsonArray{oldPoll}}}).toJson(), {});
    QVERIFY(twitch->pinned.isEmpty());
    QVERIFY(twitch->activePoll.isEmpty());
    app->hub.testMode = true;
}
