#include "native_tests.hpp"

static QString kickEnvelope(QString event, QJsonObject data) {
    QJsonObject payload{{"event", event}, {"data", QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact))}};
    QJsonObject frame{{"push", QJsonObject{{"pub", QJsonObject{{"data", payload}}}}}};
    return QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact));
}

void NativeTests::kickRealtimeEventsAndAlerts() {
    auto *source = app->hub.source("kick", "test");
    QJsonObject chat{{"id", "live-1"}, {"content", "Hello [emote:123:Wave]"},
        {"sender", QJsonObject{{"id", 5}, {"username", "Alice"}}}};
    source->kickSocketMessage(kickEnvelope("App\\Events\\ChatMessageEvent", chat));
    source->kickSocketMessage(kickEnvelope("App\\Events\\ChatMessageEvent", chat));
    QCOMPARE(source->history.size(), 1);
    QCOMPARE(source->history.first().text, QString("Hello Wave"));
    QCOMPARE(source->history.first().emotes.size(), 1);
    source->kickSocketMessage(kickEnvelope("App\\Events\\SubscriptionEvent", {{"id", "sub-1"}, {"username", "Alice"}, {"months", 14}}));
    QCOMPARE(source->history.last().special, QString("alert"));
    QVERIFY(source->history.last().text.contains("14"));
    source->kickSocketMessage(kickEnvelope("RewardRedeemedEvent", {{"id", "reward-1"}, {"username", "Bob"}, {"reward_title", "Hydrate"}, {"user_input", "Now"}}));
    QCOMPARE(source->history.last().name, QString("Bob"));
    QCOMPARE(source->history.last().text, QString("Hydrate — Now"));
    source->kickSocketMessage(kickEnvelope("GiftedSubscriptionsEvent", {{"id", "gift-1"}, {"gifter_username", "Jane"}, {"gifted_usernames", QJsonArray{"One", "Two"}}}));
    QVERIFY(source->history.last().text.contains("2"));
    ChatView view(&app->hub);
    view.setMessages({source->history[1]});
    const auto rendered = view.document(0)->toPlainText();
    QVERIFY(rendered.contains("Alice"));
    QVERIFY(rendered.contains("\n"));
    QVERIFY(rendered.contains(QChar::ObjectReplacementCharacter));
    QJsonObject celebration{{"id", "celebration-1"}, {"type", "celebration"}, {"content", "Thank you!"},
        {"metadata", QJsonObject{{"celebration", QJsonObject{{"type", "subscription_renewed"}, {"total_months", 20}}}}},
        {"sender", QJsonObject{{"id", 55}, {"username", "Subscriber"}}}};
    source->kickSocketMessage(kickEnvelope("App\\Events\\ChatMessageEvent", celebration));
    QCOMPARE(source->history.last().special, QString("alert"));
    source->kickSocketMessage(kickEnvelope("App\\Events\\MessageDeletedEvent", {{"message", QJsonObject{{"id", "live-1"}}}}));
    QVERIFY(source->history.first().deleted);
}

void NativeTests::kickEmoteCatalogCompletesAndDisplays() {
    auto *source = app->hub.source("kick", "test");
    source->kickChannelId = "123";
    app->net.transport = [](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        if (url.host() == "files.kick.com") { done({}, "fixture image unavailable"); return; }
        QCOMPARE(url.path(), QString("/emotes/test"));
        done(R"([{"id":123,"emotes":[{"id":10,"name":"ChannelWave"}]},{"id":"Global","emotes":[{"id":20,"name":"GlobalWave"}]}])", {});
    };
    source->kickLoadEmotes();
    QCOMPARE(source->channelEmotes.value("ChannelWave").urls.first(), QString("https://files.kick.com/emotes/10/fullsize"));
    QVERIFY(source->kickGlobalEmotes.contains("GlobalWave"));
    QVERIFY(source->emotes.contains("GlobalWave"));
    auto *pane = app->addWorkspace({source})->focusedPane.data();
    pane->input->setText("GlobalW");
    emit pane->input->completeRequested();
    QVERIFY(pane->input->candidates.contains("GlobalWave"));
    pane->openEmotes();
    auto *grid = pane->findChild<QListWidget *>("emote-grid");
    auto *tabs = pane->findChild<QTabBar *>("emote-tabs");
    QVERIFY(grid && tabs);
    tabs->setCurrentIndex(1);
    QVERIFY(grid->findItems("ChannelWave", Qt::MatchExactly).size() == 1);
    tabs->setCurrentIndex(2);
    QVERIFY(grid->findItems("GlobalWave", Qt::MatchExactly).size() == 1);
}

void NativeTests::kickLargeBadgeAndLegacyRoles() {
    QJsonObject identity{{"badges_v2", QJsonArray{QJsonObject{{"name", "kick_founder22"}, {"badge_type", "global"},
        {"image_url", "https://ext.cdn.kick.com/chat/badges/founder.png"}, {"selected", true}},
        QJsonObject{{"name", "Hidden"}, {"selected", false}}}},
        {"badges", QJsonArray{QJsonObject{{"type", "subscriber"}, {"text", "Subscriber"}, {"count", 5}},
                              QJsonObject{{"type", "moderator"}, {"text", "Moderator"}}}}};
    auto message = parseKick({{"id", "x"}, {"sender", QJsonObject{{"id", 1}, {"username", "User"}, {"identity", identity}}}});
    QCOMPARE(message.badges.size(), 3);
    QCOMPARE(message.badges.first().id, QString("kick_founder22"));
    QVERIFY(roles(message).contains("subscriber"));
    QVERIFY(roles(message).contains("moderator"));
    auto *source = app->hub.source("kick", "badges");
    source->kickSubscriberBadges[1] = "https://files.kick.com/sub-1.png";
    source->kickSubscriberBadges[3] = "https://files.kick.com/sub-3.png";
    source->kickSubscriberBadges[6] = "https://files.kick.com/sub-6.png";
    source->publish(message);
    QString subscriberUrl;
    for (const auto &badge : source->history.last().badges)
        if (badge.role == "subscriber") subscriberUrl = badge.urls.value(0);
    QCOMPARE(subscriberUrl, QString("https://files.kick.com/sub-3.png"));
    QImage original(3087, 3087, QImage::Format_ARGB32);
    original.fill(Qt::green);
    QByteArray bytes; QBuffer buffer(&bytes); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(original.save(&buffer, "PNG"));
    app->net.transport = [bytes](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) { done(bytes, {}); };
    const auto urls = message.badges.first().urls;
    app->images.get(urls);
    const auto scaled = app->images.get(urls);
    QVERIFY(!scaled.isNull());
    QVERIFY(scaled.width() <= 256 && scaled.height() <= 256);
}

void NativeTests::kickModerationAndChannelEditing() {
    auto *source = app->hub.source("kick", "mine");
    source->header["channel_id"] = 42;
    app->accounts.kick = {{"user_id", 42}, {"name", "Mine"}, {"access_token", "fixture"},
        {"scope", "user:read chat:write channel:write moderation:ban moderation:chat_message:manage"},
        {"expires_at", double(QDateTime::currentSecsSinceEpoch() + 600)}};
    QVERIFY(app->hub.canEditChannel(source));
    QVERIFY(app->hub.hasModerationRole(source));
    QList<QPair<QString, QJsonObject>> requests;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray body, QMap<QByteArray, QByteArray>, BytesCallback done) {
        requests << qMakePair(QString::fromUtf8(method) + " " + url.path(), QJsonDocument::fromJson(body).object());
        done("{}", {});
    };
    Message m; m.id = "message-1"; m.uid = "77"; m.name = "Viewer";
    QString error = "not called";
    auto done = [&](QJsonObject, QString e) { error = e; };
    app->hub.moderate(source, m, "delete", 0, done); QVERIFY(error.isEmpty());
    QCOMPARE(requests.last().first, QString("DELETE /public/v1/chat/message-1"));
    app->hub.moderate(source, m, "timeout", 90, done); QVERIFY(error.isEmpty());
    QCOMPARE(requests.last().second["duration"].toInt(), 2);
    QCOMPARE(requests.last().second["broadcaster_user_id"].toInt(), 42);
    QCOMPARE(requests.last().second["user_id"].toInt(), 77);
    app->hub.moderate(source, m, "unban", 0, done); QVERIFY(error.isEmpty());
    QCOMPARE(requests.last().first, QString("DELETE /public/v1/moderation/bans"));
    app->hub.editChannel(source, "New title", "123", done); QVERIFY(error.isEmpty());
    QCOMPARE(requests.last().first, QString("PATCH /public/v1/channels"));
    QCOMPARE(requests.last().second["category_id"].toInt(), 123);
    app->hub.editChannel(source, "", "456", done); QVERIFY(error.isEmpty());
    QCOMPARE(requests.last().second["category_id"].toInt(), 456);
    QVERIFY(!requests.last().second.contains("stream_title"));
    auto previous = requests.size();
    app->hub.editChannel(source, "", "", done); QVERIFY(!error.isEmpty());
    QCOMPARE(requests.size(), previous);
    m.uid = "42"; app->hub.moderate(source, m, "ban", 0, done);
    QVERIFY(!error.isEmpty()); QCOMPARE(requests.size(), previous);
    app->accounts.kick["scope"] = "user:read chat:write";
    QVERIFY(!app->hub.canEditChannel(source)); QVERIFY(!app->hub.hasModerationRole(source));
}

void NativeTests::kickAddChatByName() {
    app->addChats();
    QDialog *dialog = nullptr;
    for (auto *candidate : app->findChildren<QDialog *>()) if (candidate->windowTitle() == "Add chats") dialog = candidate;
    QVERIFY(dialog);
    auto *platform = dialog->findChild<QComboBox *>("add-chat-platform");
    auto *input = dialog->findChild<QLineEdit *>("add-chat-input");
    QVERIFY(platform && input);
    platform->setCurrentIndex(2);
    input->setText("Test_Channel");
    QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(app->hub.sources.contains("kick:test_channel"));
}

void NativeTests::kickIllustratedGuideAndPermissions() {
    app->showSettings("Tutorial");
    auto *settings = app->settings.data();
    auto *action = settings->findChild<QPushButton *>("kick-tutorial-guide-action");
    QVERIFY(action); action->click();
    auto *guide = settings->findChild<QDialog *>("kick-illustrated-guide");
    QVERIFY(guide);
    auto *preview = guide->findChild<QLabel *>("kick-guide-preview");
    auto *next = guide->findChild<QPushButton *>("kick-guide-next");
    auto *counter = guide->findChild<QLabel *>("kick-guide-counter");
    QVERIFY(preview && next && counter);
    for (int i = 1; i <= 3; ++i) {
        QCOMPARE(counter->text(), QString("Step %1 of 3").arg(i));
        QVERIFY(!preview->pixmap().isNull());
        if (i < 3) next->click();
    }
    app->cfg.data["language"] = "pt-BR"; emit app->cfg.languageChanged();
    QCOMPARE(counter->text(), QString("Etapa 3 de 3"));
}

void NativeTests::kickEmoteSendingUsesNativeEncoding() {
    auto *source = app->hub.source("kick", "example");
    source->header["channel_id"] = 456;
    Span native; native.code = "asmonSmash"; native.urls = {"https://files.kick.com/emotes/3753119/fullsize"};
    Span third; third.code = "OMEGALUL"; third.urls = {"https://cdn.betterttv.net/emote/example/3x"};
    source->emotes[native.code] = native; source->emotes[third.code] = third;
    app->accounts.kick = {{"user_id", 42}, {"name", "TestUser"}, {"access_token", "fixture"},
        {"expires_at", double(QDateTime::currentSecsSinceEpoch() + 600)}};
    QJsonObject sent;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray body, QMap<QByteArray, QByteArray>, BytesCallback done) {
        QCOMPARE(url.path(), QString("/public/v1/chat")); QCOMPARE(method, QByteArray("POST"));
        sent = QJsonDocument::fromJson(body).object();
        done(R"({"data":{"is_sent":true,"message_id":"test"}})", {});
    };
    QString error;
    app->hub.send(source, "hello asmonSmash OMEGALUL", {}, [&](QJsonObject, QString e) { error=e; });
    QVERIFY(error.isEmpty());
    QCOMPARE(str(sent["content"]), QString("hello [emote:3753119:asmonSmash] OMEGALUL"));
    QCOMPARE(source->history.last().text, QString("hello asmonSmash OMEGALUL"));
}

void NativeTests::kickCategorySearchIgnoresStaleResults() {
    auto *source = app->hub.source("kick", "mine");
    source->header["channel_id"] = 42;
    source->header["category"] = "Original";
    app->accounts.kick = {{"user_id", 42}, {"access_token", "fixture"}, {"scope", "channel:write"},
        {"expires_at", double(QDateTime::currentSecsSinceEpoch() + 600)}};
    QList<BytesCallback> callbacks;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        QCOMPARE(url.path(), QString("/public/v1/categories")); callbacks << done;
    };
    app->editChannel(source);
    QDialog *dialog = nullptr;
    for (auto *candidate : app->findChildren<QDialog *>()) if (candidate->windowTitle() == "Change title / category") dialog = candidate;
    QVERIFY(dialog);
    auto fields = dialog->findChildren<QLineEdit *>();
    QCOMPARE(fields.size(), 2);
    auto *category = fields.last(); auto *results = dialog->findChild<QComboBox *>();
    QVERIFY(results);
    category->setText("First"); QMetaObject::invokeMethod(category, "textEdited", Q_ARG(QString, "First"));
    QTRY_COMPARE_WITH_TIMEOUT(callbacks.size(), 1, 1500);
    category->setText("Second"); QMetaObject::invokeMethod(category, "textEdited", Q_ARG(QString, "Second"));
    QTRY_COMPARE_WITH_TIMEOUT(callbacks.size(), 2, 1500);
    callbacks[0](R"({"data":[{"id":1,"name":"First"}]})", {});
    QCOMPARE(results->count(), 0);
    callbacks[1](R"({"data":[{"id":2,"name":"Second"}]})", {});
    QCOMPARE(results->count(), 1);
    QCOMPARE(results->currentData().toString(), QString("2"));
    dialog->close();
}

void NativeTests::incrementalAppendKeepsOffsetsCorrect() {
    ChatView incremental(&app->hub), baseline(&app->hub);
    incremental.resize(350, 200); baseline.resize(350, 200);
    incremental.messageCap = 100; baseline.messageCap = 100;
    for (int i = 0; i < 120; ++i) {
        Message message; message.id = QString::number(i); message.seq = i + 1;
        message.name = "Viewer"; message.text = i % 2 ? "A short message" : "A longer message that wraps when the pane is narrow";
        if (i == 42) message.special = "alert";
        incremental.append(message);
        if (i == 0 || i == 42 || i == 99 || i == 119) {
            baseline.setMessages(incremental.messages);
            QCOMPARE(incremental.heights, baseline.heights);
            QCOMPARE(incremental.offsets, baseline.offsets);
        }
    }
    QCOMPARE(incremental.messages.size(), 100);
    QCOMPARE(incremental.messages.first().id, QString("20"));
}

void NativeTests::kickDisconnectImmediatelyStartsRecovery() {
    auto *source = app->hub.source("kick", "busy");
    source->kickSocketActive = true;
    source->poll.start(30000);
    app->hub.testMode = false;
    source->kickSocketFailed();
    QVERIFY(!source->kickSocketActive);
    QVERIFY(source->poll.isActive());
    QVERIFY(source->poll.remainingTime() <= 1000);
    QVERIFY(source->kickSocketRetry.isActive());
    source->stop();
    app->hub.testMode = true;
}

void NativeTests::kickReconnectRespectsRateLimit() {
    auto *source = app->hub.source("kick", "rate-limited");
    app->hub.testMode = false;
    source->kickError("HTTP 429: limited");
    QVERIFY(source->poll.remainingTime() > 110000);
    source->kickSocketActive = true;
    source->kickSocketFailed();
    QVERIFY(source->poll.remainingTime() > 110000);
    source->kickSocketMessage(R"({"id":2,"subscribe":{}})");
    QVERIFY(source->poll.remainingTime() > 110000);
    int calls = 0;
    app->net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback) { ++calls; };
    source->kickPoll();
    QCOMPARE(calls, 0);
    QVERIFY(source->poll.remainingTime() > 110000);
    source->stop(); app->hub.testMode = true;
}
