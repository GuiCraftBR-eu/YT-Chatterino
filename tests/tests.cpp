#include "native_tests.hpp"

static Message msg(QString id, QString text = "hello", QString uid = "42") {
    Message m;
    m.id = id;
    m.name = "Tester";
    m.login = "tester";
    m.uid = uid;
    m.text = text;
    m.stamp = QDateTime::currentDateTime();
    return m;
}

static Message replyMessage(QString id, QString parent, QString root, QString body = "reply") {
    auto m = msg(id, body);
    m.reply = {{"id", parent},
               {"thread_id", root},
               {"name", "Parent"},
               {"login", "parent"},
               {"text", "original body"}};
    return m;
}

void NativeTests::authenticate() {
    app->accounts.twitch = {
        {"access_token", "fake-token"},
        {"login", "tester"},
        {"user_id", "42"},
        {"expires_at", double(QDateTime::currentSecsSinceEpoch() + 3600)},
        {"scope", QJsonArray{"chat:read", "user:write:chat", "user:read:emotes",
                             "user:read:moderated_channels", "moderator:manage:chat_messages",
                             "moderator:manage:banned_users", "channel:manage:broadcast"}}};
    app->accounts.google = {{"access_token", "fake-google-token"},
                            {"channel_id", "UCtest"},
                            {"channel_title", "Tester"},
                            {"expires_at", double(QDateTime::currentSecsSinceEpoch() + 3600)}};
}

void NativeTests::init() {
    QVERIFY(temp.isValid());
    QString dir = temp.path() + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(dir);
    app = std::make_unique<MainWindow>(dir, true);
    auto f = app->cfg.features();
    auto alerts = f["alerts"].toObject();
    alerts["enabled"] = false;
    f["alerts"] = alerts;
    app->cfg.data["features"] = f;
    unexpectedRequests.clear();
    app->net.transport = [this](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>,
                                BytesCallback done) {
        unexpectedRequests << url.toString();
        done({}, "Unexpected test request");
    };
    twitch = app->hub.source("twitch", "guicraft_");
    twitch->header = {{"channel_id", "12"},          {"channel", "GuiCraft_"}, {"title", "Offline title"},
                      {"category", "Just Chatting"}, {"live", false},          {"viewers", 0}};
    authenticate();
}

void NativeTests::cleanup() {
    app.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void NativeTests::videoLinks_data() {
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");
    QTest::newRow("id") << "0sfDth4lMLc" << "0sfDth4lMLc";
    QTest::newRow("watch") << "https://www.youtube.com/watch?v=0sfDth4lMLc&t=1" << "0sfDth4lMLc";
    QTest::newRow("short") << "https://youtu.be/0sfDth4lMLc" << "0sfDth4lMLc";
    QTest::newRow("live") << "youtube.com/live/0sfDth4lMLc" << "0sfDth4lMLc";
    QTest::newRow("wrong-host") << "https://youtube.com.evil.test/watch?v=0sfDth4lMLc" << "";
    QTest::newRow("invalid") << "not a stream" << "";
}

void NativeTests::videoLinks() {
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(videoId(input), expected);
}

void NativeTests::twitchLinks_data() {
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");
    QTest::newRow("underscore") << "@guicraft_" << "guicraft_";
    QTest::newRow("case") << "https://www.twitch.tv/GuiCraft_" << "guicraft_";
    QTest::newRow("popout") << "https://www.twitch.tv/popout/guicraft_/chat" << "guicraft_";
    QTest::newRow("hyphen") << "@guicraft-" << "";
    QTest::newRow("reserved") << "https://www.twitch.tv/directory" << "";
    QTest::newRow("host") << "https://twitch.tv.evil.test/guicraft_" << "";
}

void NativeTests::twitchLinks() {
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(twitchLogin(input), expected);
}

void NativeTests::embeddedJsonBalancesStrings() {
    QString html = "prefix ytInitialData = {\"text\":\"} \\\" brace\",\"nested\":{\"n\":2}}; suffix";
    auto d = embeddedJson(html, "ytInitialData");
    QCOMPARE(d["nested"].toObject()["n"].toInt(), 2);
    QVERIFY(!d["text"].toString().isEmpty());
    QVERIFY(embeddedJson("bad", "ytInitialData").isEmpty());
}

void NativeTests::ircReplyBadgesEmotes() {
    QString line = "@id=one;user-id=42;room-id=12;display-name=Tester;color=#00ff00;badges=moderator/"
                   "1,subscriber/12;badge-info=subscriber/"
                   "73;tmi-sent-ts=1700000000000;emotes=25:6-10;reply-parent-msg-id=parent;reply-thread-"
                   "parent-msg-id=root;reply-parent-user-login=someone;reply-parent-msg-body="
                   "hello\\sworld :tester!t@t PRIVMSG #guicraft_ :Hello Kappa";
    auto parsed = parseIrc(line);
    QCOMPARE(parsed.channel, "guicraft_");
    QCOMPARE(parsed.message.reply["text"].toString(), "hello world");
    QCOMPARE(parsed.message.reply["thread_id"].toString(), "root");
    QVERIFY(roles(parsed.message).contains("moderator"));
    QCOMPARE(parsed.message.emotes.size(), 1);
    QCOMPARE(parsed.message.emotes[0].start, 6);
    QCOMPARE(parsed.message.emotes[0].end, 11);
}

void NativeTests::ircAstralOffsetsAndAction() {
    auto p = parseIrc("@id=x;emotes=25:2-6 :tester!t@t PRIVMSG #room :😀 Kappa");
    QCOMPARE(p.message.emotes[0].start, 3);
    QCOMPARE(p.message.text.mid(3, 5), "Kappa");
    auto a = parseIrc(QString(":t!t@t PRIVMSG #room :") + QChar(1) + "ACTION waves" + QChar(1));
    QCOMPARE(a.message.text, "waves");
}

void NativeTests::twitchGifKeepsSignedUrlAndRejectsOtherHosts() {
    QString text = "[GIF]";
    auto p = parseIrc(
        "@id=x;gifs=0-4|abc|https://media.giphy.com/media/abc/giphy.gif?sig=123 :t!t@t PRIVMSG #room :" +
        text);
    QCOMPARE(p.message.emotes.size(), 1);
    QVERIFY(p.message.emotes[0].gif);
    QVERIFY(p.message.emotes[0].urls[0].endsWith("?sig=123"));
    auto bad = parseIrc("@id=x;gifs=0-4|abc|https://evil.test/gif :t!t@t PRIVMSG #room :" + text);
    QVERIFY(bad.message.emotes.isEmpty());
}

void NativeTests::youtubeRunsBadgesAndUnicode() {
    auto renderer =
        QJsonDocument::fromJson(
            R"json({"id":"yt","authorName":{"simpleText":"YT user"},"authorExternalChannelId":"UCuser","timestampUsec":"1700000000000000","message":{"runs":[{"text":"😀 "},{"emoji":{"emojiId":"x","shortcuts":[":wave:"],"image":{"thumbnails":[{"url":"https://yt3.ggpht.com/x","width":48}]}}}]},"authorBadges":[{"liveChatAuthorBadgeRenderer":{"icon":{"iconType":"MODERATOR"}}}]})json")
            .object();
    auto m = parseYouTube(renderer, "liveChatTextMessageRenderer", true);
    QCOMPARE(m.name, "YT user");
    QCOMPARE(m.text, "😀 :wave:");
    QCOMPARE(m.emotes[0].start, 3);
    QVERIFY(roles(m).contains("moderator"));
    QVERIFY(m.history);
    QVERIFY(m.reply.isEmpty());
}

void NativeTests::youtubePaidPrefixMovesEmotes() {
    auto m = parseYouTube(
        {{"id", "paid"},
         {"authorName", QJsonObject{{"simpleText", "Paid"}}},
         {"purchaseAmountText", QJsonObject{{"simpleText", "R$ 5"}}},
         {"sticker",
          QJsonObject{{"thumbnails",
                       QJsonArray{QJsonObject{{"url", "https://yt3.ggpht.com/sticker"}, {"width", 100}}}}}}},
        "liveChatPaidStickerRenderer");
    QCOMPARE(m.special, "super");
    QVERIFY(m.text.startsWith("R$ 5 "));
    QCOMPARE(m.emotes[0].start, 5);
}

void NativeTests::messageRoundTrip() {
    auto m = replyMessage("a", "p", "root", "hello");
    m.badges << Badge{"moderator", "1", "Mod", "moderator", {"https://static-cdn.jtvnw.net/badge"}};
    m.emotes << Span{0, 5, "hello", {"https://cdn.7tv.app/x"}, false};
    m.seq = 123;
    m.own = true;
    auto restored = Message::fromJson(m.json());
    QCOMPARE(restored.text, m.text);
    QCOMPARE(restored.reply, m.reply);
    QCOMPARE(restored.emotes[0].urls, m.emotes[0].urls);
    QCOMPARE(restored.seq, m.seq);
    QVERIFY(restored.own);
}

void NativeTests::datesAndLowercaseTime_data() {
    QTest::addColumn<QString>("fmt");
    QTest::addColumn<QString>("expected");
    QTest::newRow("24") << "HH:mm" << "13:02";
    QTest::newRow("seconds") << "HH:mm:ss" << "13:02:04";
    QTest::newRow("12") << "hh:mm am/pm" << "1:02 pm";
    QTest::newRow("12seconds") << "hh:mm:ss am/pm" << "1:02:04 pm";
}

void NativeTests::datesAndLowercaseTime() {
    QFETCH(QString, fmt);
    QFETCH(QString, expected);
    auto a = app->cfg.appearance();
    a["time_format"] = fmt;
    app->cfg.setAppearance(a);
    QCOMPARE(app->cfg.time(QDateTime(QDate(2026, 1, 9), QTime(13, 2, 4))), expected);
    QCOMPARE(app->cfg.date("2026-01-09"), "09/01/2026");
    a["date_format"] = "YYYY-MM-DD";
    app->cfg.setAppearance(a);
    QCOMPARE(app->cfg.date("2026-01-09"), "2026-01-09");
}

void NativeTests::midnightNoonAndLegacyFontMigration() {
    auto a = app->cfg.appearance();
    a["time_format"] = "hh:mm am/pm";
    app->cfg.setAppearance(a);
    QCOMPARE(app->cfg.time(QDateTime(QDate(2026, 1, 9), QTime(0, 0))), "12:00 am");
    QCOMPARE(app->cfg.time(QDateTime(QDate(2026, 1, 9), QTime(12, 0))), "12:00 pm");
    QString dir = temp.path() + "/legacy";
    QDir().mkpath(dir);
    writeObject(dir + "/.ytchatterino.json",
                {{"appearance", QJsonObject{{"font_size", 17}, {"time_format", "hh:mm AM/PM"}}}});
    Config c(dir);
    QCOMPARE(c.messageFont().pointSize(), 17);
    QCOMPARE(c.usernameFont().pointSize(), 17);
    QCOMPARE(c.appearance()["time_format"].toString(), "hh:mm am/pm");
}

void NativeTests::quotaMigrationAndPacificReset() {
    app->cfg.data["youtube_daily_limit"] = 10000;
    app->cfg.data["youtube_external_usage"] = QJsonObject{{"date", app->quota.day()}, {"used", 100}};
    app->quota.add(20);
    QCOMPARE(app->quota.remaining(), 9880);
    app->quota.daily = {{"date", "2000-01-01"}, {"used", 9999}};
    app->quota.roll();
    QCOMPARE(app->quota.used(), 0);
    QCOMPARE(app->quota.remaining(), 9900);
}

void NativeTests::highlightRoleNicknameWordsAndBoundaries() {
    auto h = app->cfg.feature("highlight");
    h["users"] = QJsonObject{{"tester", "#123456"}};
    h["roles"] = QJsonObject{{"moderator", "#227400"}};
    h["words"] = QJsonArray{"hello"};
    app->cfg.setFeature("highlight", h);
    auto m = msg("x", "hello there");
    m.badges << Badge{"moderator", "1", "MOD", "moderator", {}};
    bool mention = false;
    QCOMPARE(highlight(m, app->cfg, {}, &mention).name(), "#123456");
    QVERIFY(mention);
    QVERIFY(wordMatch("hey @tester!", "tester"));
    QVERIFY(!wordMatch("testers", "tester"));
    QVERIFY(wordMatch("@güi hello", "güi"));
}

void NativeTests::filtersAndSystemVisibility() {
    auto m = msg("x", "hello world");
    QVERIFY(accepts(m, {{"include_terms", QJsonArray{"world"}}}));
    QVERIFY(!accepts(m, {{"exclude_terms", QJsonArray{"world"}}}));
    QVERIFY(!accepts(m, {{"hidden_users", QJsonArray{"@TESTER"}}}));
    m.system = true;
    QVERIFY(!accepts(m, {{"hide_system", true}}));
}

void NativeTests::retentionDedupAndSeenBounded() {
    app->cfg.data["message_limit"] = 100;
    for (int i = 0; i < 250; ++i) {
        twitch->publish(msg(QString::number(i)));
    }
    QCOMPARE(twitch->history.size(), 100);
    QCOMPARE(twitch->seen.size(), 200);
    auto last = twitch->history.last().seq;
    twitch->publish(msg("249"));
    QCOMPARE(twitch->history.last().seq, last);
}

void NativeTests::moderationDoesNotCreateReplyMetadata() {
    twitch->publish(msg("a", "first", "42"));
    twitch->publish(msg("b", "second", "99"));
    twitch->redact({{"user_id", "42"}});
    QVERIFY(twitch->history[0].deleted);
    QVERIFY(!twitch->history[1].deleted);
    QVERIFY(twitch->history[0].reply.isEmpty());
    QVERIFY(twitch->history[1].reply.isEmpty());
    twitch->redact({{"all", true}});
    QVERIFY(twitch->history[1].reply.isEmpty());
}

void NativeTests::moderationHidesReplyContext() {
    twitch->publish(msg("root", "secret"));
    twitch->publish(replyMessage("child", "root", "root"));
    twitch->redact({{"message_id", "root"}});
    QVERIFY(twitch->history[0].deleted);
    QCOMPARE(twitch->history[1].reply["text"].toString(), "[message deleted]");
}

void NativeTests::nestedThreadsMissingRootAndCycle() {
    QVector<Message> history{msg("a"), replyMessage("b", "a", ""), replyMessage("c", "b", ""), msg("other")};
    QString root;
    auto found = collectThread(history, history[2], &root);
    QCOMPARE(root, "a");
    QCOMPARE(found.size(), 3);
    QHash<QString, Message> index;
    index["a"] = replyMessage("a", "b", "");
    index["b"] = replyMessage("b", "a", "");
    QCOMPARE(threadRoot(index["a"], index), "a");
    QCOMPARE(threadRoot(index["b"], index), "a");
    QCOMPARE(threadRoot(replyMessage("z", "missing", "root"), {}), "root");
}

void NativeTests::publicBrowserUrlsNoCredentials() {
    for (const auto &dest : QStringList{"stream", "player", "chat"}) {
        auto url = browserUrl("twitch", "guicraft_", dest);
        QVERIFY(url.startsWith("https://"));
        QVERIFY(url.contains("guicraft_"));
        QVERIFY(!url.contains("token"));
        QVERIFY(!browserUrl("youtube", "0sfDth4lMLc", dest).isEmpty());
    }
    QVERIFY(browserUrl("twitch", "bad/room", "chat").isEmpty());
}

void NativeTests::imageHostValidation() {
    QVERIFY(Images::allowed(QUrl("https://cdn.7tv.app/emote/a/3x.webp")));
    QVERIFY(Images::allowed(QUrl("https://media.giphy.com/a?sig=1")));
    QVERIFY(!Images::allowed(QUrl("https://giphy.com.evil.test/a")));
    QVERIFY(!Images::allowed(QUrl("http://cdn.7tv.app/a")));
    QVERIFY(!Images::allowed(QUrl("https://user:password@cdn.7tv.app/a")));
}

void NativeTests::viewportCapCopyAndTimestampToggle() {
    app->cfg.data["message_limit"] = 100;
    auto *w = app->addWorkspace({twitch});
    auto *p = w->focusedPane.data();
    for (int i = 0; i < 120; ++i) {
        twitch->publish(msg(QString::number(i), "body " + QString::number(i)));
    }
    QCOMPARE(p->view->messages.size(), 100);
    auto body = p->view->messageText(0);
    QVERIFY(body.contains("body 20"));
    auto a = app->cfg.appearance();
    a["show_timestamps"] = false;
    app->cfg.setAppearance(a);
    QString without = p->view->messageText(0);
    QVERIFY(without.startsWith("Tester:"));
    a["show_timestamps"] = true;
    app->cfg.setAppearance(a);
    QVERIFY(p->view->messageText(0).startsWith(app->cfg.time(p->view->messages[0].stamp)));
    p->view->selectionStart = {0, 0};
    p->view->selectionEnd = {1, p->view->document(1)->characterCount() - 1};
    auto copied = p->view->selectedText();
    QVERIFY(copied.contains("body 20"));
    QVERIFY(copied.contains("body 21"));
}

void NativeTests::independentFontsAndShortcuts() {
    auto *p = app->addWorkspace({twitch})->focusedPane.data();
    twitch->publish(msg("a", "keep me"));
    auto a = app->cfg.appearance();
    a["message_font_size"] = 15;
    a["username_font_size"] = 18;
    app->cfg.setAppearance(a);
    QCOMPARE(app->cfg.messageFont().pointSize(), 15);
    QCOMPARE(app->cfg.usernameFont().pointSize(), 18);
    QVERIFY(p->view->messageText(0).contains("keep me"));
    app->changeFont(1);
    QCOMPARE(app->cfg.messageFont().pointSize(), 16);
    QCOMPARE(app->cfg.usernameFont().pointSize(), 19);
    app->changeFont(0, true);
    QCOMPARE(app->cfg.messageFont().pointSize(), 12);
    QCOMPARE(app->cfg.usernameFont().pointSize(), 12);
}

void NativeTests::largePaneCapRetainsHistoryAcrossAppearance() {
    auto *p = app->addWorkspace({twitch})->focusedPane.data();
    auto limits = app->cfg.data["chat_message_limits"].toObject();
    limits[p->key()] = 3000;
    app->cfg.data["chat_message_limits"] = limits;
    emit app->cfg.appearanceChanged();
    for (int i = 0; i < 3100; ++i) {
        twitch->publish(msg(QString::number(i)));
    }
    QCOMPARE(p->view->messages.size(), 3000);
    QCOMPARE(twitch->history.size(), 3000);
    auto a = app->cfg.appearance();
    a["compact_spacing"] = true;
    app->cfg.setAppearance(a);
    QCOMPARE(p->view->messages.size(), 3000);
}

void NativeTests::layoutMixSplitAndLegacySession() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    auto *w = app->addWorkspace({twitch});
    auto *first = w->focusedPane.data();
    w->add({yt}, first, Qt::Vertical);
    QCOMPARE(w->panes().size(), 2);
    auto saved = w->serialize();
    auto *other = new Workspace(&app->hub);
    other->restore(saved);
    QCOMPARE(other->panes().size(), 2);
    delete other;
    QJsonObject legacy{
        {"id", "old"},
        {"layout", QJsonObject{{"orient", "vertical"},
                               {"ratio", .3},
                               {"children", QJsonArray{QJsonObject{{"sources", QJsonArray{twitch->key}}},
                                                       QJsonObject{{"sources", QJsonArray{yt->key}}}}}}}};
    other = new Workspace(&app->hub);
    other->restore(legacy);
    QCOMPARE(other->panes().size(), 2);
    auto *split = qobject_cast<QSplitter *>(other->layout->itemAt(0)->widget());
    QVERIFY(split);
    QCOMPARE(split->orientation(), Qt::Vertical);
    delete other;
    first->setSources({twitch, yt});
    QVERIFY(first->view->mixed);
}

void NativeTests::emptyWindowKeepsAddControls() {
    app->show();
    QTest::qWait(30);
    QVERIFY(app->addButton->isVisible());
    QVERIFY(app->gear->isVisible());
    QVERIFY(app->tabs->count() > 0);
}

void NativeTests::threadWindowLiveUpdatesAndDeletedTarget() {
    twitch->publish(msg("a"));
    twitch->publish(replyMessage("b", "a", "a"));
    auto *p = app->addWorkspace({twitch})->focusedPane.data();
    Q_UNUSED(p)
    app->showThread(twitch, twitch->history.last());
    QCOMPARE(app->threads.size(), 1);
    auto thread = app->threads.begin().value();
    QVERIFY(thread);
    QCOMPARE(thread->pane->view->messages.size(), 2);
    twitch->publish(replyMessage("c", "b", "a"));
    QCOMPARE(thread->pane->view->messages.size(), 3);
    QCOMPARE(thread->pane->reply.id, "a");
    twitch->redact({{"message_id", "a"}});
    QVERIFY(thread->pane->reply.id != "a");
    thread->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(app->threads.isEmpty());
}

void NativeTests::settingsSearchLanguageAndFontControls() {
    app->addWorkspace({twitch});
    app->showSettings("Display");
    auto *settings = app->settings.data();
    QVERIFY(settings);
    auto *body = settings->findChild<QSpinBox *>("message_font_size");
    auto *name = settings->findChild<QSpinBox *>("username_font_size");
    QVERIFY(body && name);
    body->setValue(16);
    settings->showPage("Messages");
    settings->showPage("Display");
    QCOMPARE(body->value(), 16);
    settings->search->setText("fonte nome");
    QVERIFY(settings->results->count() > 0);
    settings->search->setText("timestamp");
    QVERIFY(settings->results->count() > 0);
    QVERIFY(!settings->results->item(0)->text().contains("clock"));
    app->cfg.data["language"] = "pt-BR";
    emit app->cfg.languageChanged();
    QCOMPARE(app->cfg.T("Appearance"), "Aparência");
    QCOMPARE(app->currentPane()->sendButton->text(), "Enviar");
    QCOMPARE(app->currentPane()->input->placeholderText(), "Enviar uma mensagem…");
    settings->search->setText("horário");
    QCOMPARE(settings->results->item(0)->data(Qt::UserRole).toString(), "Display");
}

void NativeTests::twitchSendReplyAndDrop() {
    QString sent;
    app->net.transport = [&](QUrl u, QByteArray method, QByteArray body, QMap<QByteArray, QByteArray>,
                             BytesCallback done) {
        QCOMPARE(u.path(), "/helix/chat/messages");
        QCOMPARE(method, QByteArray("POST"));
        auto d = QJsonDocument::fromJson(body).object();
        sent = str(d["reply_parent_message_id"]);
        done("{\"data\":[{\"is_sent\":true,\"message_id\":\"sent\"}]}", {});
    };
    QString error;
    auto parent = msg("parent", "hello");
    app->hub.send(twitch, "test", parent, [&](QJsonObject, QString e) { error = e; });
    QVERIFY(error.isEmpty());
    QCOMPARE(sent, "parent");
    QVERIFY(twitch->history.last().own);
    QCOMPARE(twitch->history.last().id, "sent");
}

void NativeTests::deletedReplyCannotBecomeTopLevelSend() {
    bool sent = false;
    app->net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback) {
        sent = true;
    };
    auto parent = msg("parent");
    parent.deleted = true;
    QString error;
    app->hub.send(twitch, "test", parent, [&](QJsonObject, QString e) { error = e; });
    QVERIFY(!sent);
    QVERIFY(!error.isEmpty());
}

void NativeTests::twitchModerationPermissionsAndDuration() {
    QJsonObject posted;
    app->net.transport = [&](QUrl u, QByteArray, QByteArray body, QMap<QByteArray, QByteArray>,
                             BytesCallback done) {
        if (u.path().endsWith("/channels")) {
            done("{\"data\":[{\"broadcaster_id\":\"12\"}]}", {});
            return;
        }
        posted = QJsonDocument::fromJson(body).object();
        done("{}", {});
    };
    QString error;
    app->hub.moderate(twitch, msg("victim", "text", "99"), "timeout", 600,
                      [&](QJsonObject, QString e) { error = e; });
    QVERIFY(error.isEmpty());
    QCOMPARE(posted["data"].toObject()["duration"].toInt(), 600);
    QCOMPARE(posted["data"].toObject()["user_id"].toString(), "99");
}

void NativeTests::twitchBroadcasterDeletionProtected() {
    app->accounts.twitch["user_id"] = "12";
    bool requested = false;
    app->net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback) {
        requested = true;
    };
    QString error;
    app->hub.moderate(twitch, msg("host", "hello", "12"), "delete", 0,
                      [&](QJsonObject, QString e) { error = e; });
    QVERIFY(!requested);
    QVERIFY(error.contains("broadcaster"));
}

void NativeTests::youtubeSendingCostsAndPayload() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    QJsonObject posted;
    app->net.transport = [&](QUrl u, QByteArray method, QByteArray body, QMap<QByteArray, QByteArray>,
                             BytesCallback done) {
        if (u.path().endsWith("/videos")) {
            QCOMPARE(method, QByteArray("GET"));
            done("{\"items\":[{\"liveStreamingDetails\":{\"activeLiveChatId\":\"chat\"}}]}", {});
            return;
        }
        posted = QJsonDocument::fromJson(body).object();
        done("{\"id\":\"sent\"}", {});
    };
    QString error;
    app->hub.send(yt, "hello", {}, [&](QJsonObject, QString e) { error = e; });
    QVERIFY(error.isEmpty());
    QCOMPARE(app->quota.used(), 21);
    QCOMPARE(str(at(posted, {"snippet", "textMessageDetails", "messageText"})), "hello");
    QCOMPARE(yt->history.last().apiId, "sent");
}

void NativeTests::youtubeModerationMapsWebMessageId() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    yt->header["live_chat_id"] = "chat";
    auto m = msg("web-id", "delete me", "UCtarget");
    QString deletion;
    app->net.transport = [&](QUrl u, QByteArray method, QByteArray, QMap<QByteArray, QByteArray>,
                             BytesCallback done) {
        if (method == "DELETE") {
            deletion = QUrlQuery(u).queryItemValue("id");
            done({}, {});
            return;
        }
        QJsonObject response{
            {"items",
             QJsonArray{QJsonObject{
                 {"id", "api-id"},
                 {"authorDetails", QJsonObject{{"channelId", m.uid}}},
                 {"snippet", QJsonObject{{"publishedAt", m.stamp.toUTC().toString(Qt::ISODate)},
                                         {"textMessageDetails", QJsonObject{{"messageText", m.text}}}}}}}}};
        done(QJsonDocument(response).toJson(), {});
    };
    QString error;
    app->hub.moderate(yt, m, "delete", 0, [&](QJsonObject, QString e) { error = e; });
    QVERIFY(error.isEmpty());
    QCOMPARE(deletion, "api-id");
    QCOMPARE(app->quota.used(), 25);
}

void NativeTests::youtubeBanCostsAndDuration() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    yt->header["live_chat_id"] = "chat";
    QJsonObject body;
    app->net.transport = [&](QUrl, QByteArray, QByteArray b, QMap<QByteArray, QByteArray>,
                             BytesCallback done) {
        body = QJsonDocument::fromJson(b).object();
        done("{\"id\":\"ban-id\"}", {});
    };
    QString e;
    app->hub.moderate(yt, msg("m", "x", "UCtarget"), "timeout", 60,
                      [&](QJsonObject, QString error) { e = error; });
    QVERIFY(e.isEmpty());
    QCOMPARE(body["snippet"].toObject()["banDurationSeconds"].toInt(), 60);
    QCOMPARE(app->quota.used(), 200);
}

void NativeTests::titleUpdatesPreserveYouTubeSnippet() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    QJsonObject posted;
    app->net.transport = [&](QUrl, QByteArray method, QByteArray body, QMap<QByteArray, QByteArray>,
                             BytesCallback done) {
        if (method == "GET") {
            done("{\"items\":[{\"snippet\":{\"title\":\"Old\",\"categoryId\":\"20\",\"description\":"
                 "\"Keep\"}}]}",
                 {});
            return;
        }
        posted = QJsonDocument::fromJson(body).object();
        done("{}", {});
    };
    QString error;
    app->hub.editChannel(yt, "New", {}, [&](QJsonObject, QString e) { error = e; });
    QVERIFY(error.isEmpty());
    QCOMPARE(str(at(posted, {"snippet", "description"})), "Keep");
    QCOMPARE(str(at(posted, {"snippet", "title"})), "New");
    QCOMPARE(app->quota.used(), 51);
}

void NativeTests::twitchTitleRequiresBroadcaster() {
    QString error;
    app->hub.editChannel(twitch, "New", "game", [&](QJsonObject, QString e) { error = e; });
    QVERIFY(error.contains("broadcaster"));
    QVERIFY(unexpectedRequests.isEmpty());
}

void NativeTests::youtubePollContinuesUnfocusedAndManualPauseStops() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    yt->continuation = "token";
    yt->context = {{"client", QJsonObject{{"clientVersion", "1"}}}};
    int calls = 0;
    app->net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        ++calls;
        done("{\"continuationContents\":{\"liveChatContinuation\":{\"continuations\":[{"
             "\"timedContinuationData\":{\"continuation\":\"next\",\"timeoutMs\":4000}}]}}}",
             {});
    };
    app->addWorkspace({yt});
    app->addWorkspace({twitch});
    yt->youtubePoll();
    QCOMPARE(calls, 1);
    QVERIFY(yt->poll.isActive());
    yt->setPaused(true);
    QVERIFY(!yt->poll.isActive());
    yt->youtubePoll();
    QCOMPARE(calls, 1);
    yt->setPaused(false);
    QVERIFY(yt->poll.isActive());
}

void NativeTests::sourceSessionAndWindowGeometry() {
    app->addWorkspace({twitch});
    app->resize(760, 600);
    app->move(120, 90);
    app->show();
    QTest::qWait(30);
    app->saveSession();
    auto session = app->cfg.data["session"].toObject();
    QVERIFY(!session["window"].toObject()["qt_geometry"].toString().isEmpty());
    QCOMPARE(session["tabs"].toArray().size(), 1);
    QString home = app->cfg.home;
    app.reset();
    app = std::make_unique<MainWindow>(home, true);
    QCOMPARE(app->tabs->count(), 1);
    QVERIFY(app->hub.sources.contains("twitch:guicraft_"));
    QCOMPARE(app->size(), QSize(760, 600));
}

void NativeTests::updateVersionAndAssetValidation() {
    QJsonObject release{
        {"tag_name", "v2.0.1"},
        {"published_at", "2026-10-24T10:00:00Z"},
        {"assets", QJsonArray{QJsonObject{{"name", "YT-Chatterino-Windows11-2.0.1.zip"},
                                          {"browser_download_url",
                                           "https://github.com/GuiCraftBR-eu/YT-Chatterino/releases/"
                                           "download/v2.0.1/YT-Chatterino-Windows11-2.0.1.zip"},
                                          {"digest", "sha256:" + QString(64, 'a')},
                                          {"size", 123}}}}};
    QString error;
    auto info = Updater::releaseInfo(release, "2.0.0", "windows", &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(info["version"].toString(), "2.0.1");
    QVERIFY(Updater::releaseInfo(release, "2.0.1", "windows", &error).isEmpty());
    auto assets = release["assets"].toArray();
    auto bad = assets[0].toObject();
    bad["browser_download_url"] = "https://evil.test/update.zip";
    assets[0] = bad;
    release["assets"] = assets;
    QVERIFY(Updater::releaseInfo(release, "2.0.0", "windows", &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(versionNewer("v2.0.10", "2.0.2"));
    QVERIFY(!validVersion("2.0.1-rc1"));
}

void NativeTests::updateArchivePaths() {
    for (const auto &bad : QStringList{"../escape", "/absolute", "C:/drive", "safe/../escape",
                                       "safe\\..\\escape", "line\nbreak"}) {
        QVERIFY2(!Updater::safePath(bad), qPrintable(bad));
    }
    QVERIFY(Updater::safePath("YT-Chatterino/platforms/qwindows.dll"));
}

void NativeTests::secureNamespaceAndNoTokensInSettings() {
    Vault a(&app->cfg);
    QVERIFY(a.target("twitch").startsWith("YT-Chatterino/Twitch OAuth/"));
    QVERIFY(!app->cfg.data.contains("twitch_auth"));
    auto text = QJsonDocument(app->cfg.data).toJson();
    QVERIFY(!text.contains("fake-token"));
    QVERIFY(!text.contains("fake-google-token"));
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (QFileInfo::exists(QCoreApplication::applicationDirPath() + "/test-update-child")) {
        auto args = app.arguments();
        int index = args.indexOf("--updated");
        if (index < 0) {
            return 0;
        }
        QString ready = args.value(index + 1), container = QFileInfo(ready).absolutePath();
        writeObject(container + "/child-running", {{"running", true}});
        writeObject(ready, {{"pid", QCoreApplication::applicationPid()}});
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &app, [&] {
            if (QFileInfo::exists(container + "/done.json")) {
                QFile::remove(container + "/child-running");
                app.quit();
            }
        });
        poll.start(50);
        QTimer::singleShot(5000, &app, &QApplication::quit);
        return app.exec();
    }
    NativeTests tests;
    return QTest::qExec(&tests, argc, argv);
}
