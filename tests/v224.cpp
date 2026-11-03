#include "native_tests.hpp"

void NativeTests::twitchConnectionIsIdempotent() {
    auto &connection = app->hub.twitch;
    connection.channels["guicraft_"] = twitch;
    app->accounts.loaded = true;
    app->hub.testMode = false;
    app->net.transport = [](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        done("{\"data\":[]}", {});
    };
    const auto identity = str(app->accounts.twitch["login"]).toUtf8() + '\0' +
                          str(app->accounts.twitch["access_token"]).toUtf8();
    connection.connectionIdentity = QCryptographicHash::hash(identity, QCryptographicHash::Sha256);
    connection.retry.start(5000);
    connection.line(":tmi.twitch.tv 001 tester :Welcome");
    QVERIFY(connection.registered);
    QVERIFY(!connection.retry.isActive());
    connection.line("@badges=;display-name=Tester;mod=0 :tmi.twitch.tv USERSTATE #guicraft_");
    twitch->connectionNotices["Connected to Twitch"] -= 120000;
    const auto attempt = connection.connectionAttempt;
    Message chat; chat.id = "one"; chat.name = "Viewer"; chat.text = "between notices";
    twitch->publish(chat);
    const int before = twitch->history.size();
    connection.line(":tmi.twitch.tv 001 tester :Welcome again");
    connection.line("@badges=;display-name=Tester;mod=0 :tmi.twitch.tv USERSTATE #guicraft_");
    connection.connectServer();
    emit app->accounts.ready();
    QVERIFY(connection.registered);
    QCOMPARE(connection.connectionAttempt, attempt);
    int connected = 0;
    for (const auto &message : twitch->history)
        if (message.system && message.text == "Connected to Twitch") ++connected;
    QCOMPARE(connected, 1);
    QVERIFY(twitch->history.size() >= before);
    connection.intentionalDisconnect = true;
    QVERIFY(QMetaObject::invokeMethod(&connection.socket, "disconnected", Qt::DirectConnection));
    QVERIFY(!connection.retry.isActive());
    connection.intentionalDisconnect = false;
    QVERIFY(QMetaObject::invokeMethod(&connection.socket, "disconnected", Qt::DirectConnection));
    QVERIFY(connection.retry.isActive());
    connection.retry.stop();
    app->hub.testMode = true;
}

void NativeTests::mixedHistoryDoesNotBlockLiveMessages() {
    auto *other = app->hub.source("twitch", "another");
    auto *workspace = app->addWorkspace({twitch, other});
    auto *pane = workspace->focusedPane.data();
    QCOMPARE(app->tabs->count(), 1);
    QCOMPARE(app->tabs->widget(0), static_cast<QWidget *>(workspace));
    Workspace reopened(&app->hub);
    reopened.restore(workspace->serialize());
    auto *restoredPane = reopened.focusedPane.data();
    QVERIFY(restoredPane);
    QCOMPARE(restoredPane->sources.size(), 2);
    app->hub.twitch.channels["guicraft_"] = twitch;
    app->hub.twitch.channels["another"] = other;
    QMap<QString, BytesCallback> pending;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        pending[url.path()] = done;
    };
    app->hub.testMode = false;
    twitch->loadHistory();
    other->loadHistory();
    QVERIFY(twitch->historyLoading && other->historyLoading);
    app->hub.twitch.line("@id=live-a;display-name=A :a!a@a PRIVMSG #guicraft_ :live A");
    app->hub.twitch.line("@id=live-b;display-name=B :b!b@b PRIVMSG #another :live B");
    QCOMPARE(pane->view->messages.size(), 2);
    QCOMPARE(pane->view->messages[0].text, "live A");
    QCOMPARE(pane->view->messages[1].text, "live B");
    const QJsonArray oldA{"@id=old-a;display-name=A;tmi-sent-ts=1700000001000 :a!a@a PRIVMSG #guicraft_ :old A",
                          "@id=live-a;display-name=A;tmi-sent-ts=1700000002000 :a!a@a PRIVMSG #guicraft_ :duplicate"};
    const QJsonArray oldB{"@id=old-b;display-name=B;tmi-sent-ts=1700000000000 :b!b@b PRIVMSG #another :old B"};
    pending["/api/v2/recent-messages/guicraft_"](QJsonDocument(QJsonObject{{"messages", oldA}}).toJson(), {});
    pending["/api/v2/recent-messages/another"](QJsonDocument(QJsonObject{{"messages", oldB}}).toJson(), {});
    QCOMPARE(pane->view->messages.size(), 4);
    QCOMPARE(pane->view->messages[0].text, "old B");
    QCOMPARE(pane->view->messages[1].text, "old A");
    QCOMPARE(pane->view->messages[2].text, "live A");
    QCOMPARE(pane->view->messages[3].text, "live B");
    QCOMPARE(restoredPane->view->messages.size(), 4);
    QCOMPARE(restoredPane->view->messages[0].text, "old B");
    QCOMPARE(restoredPane->view->messages[1].text, "old A");
    QVERIFY(twitch->historyLoaded && other->historyLoaded);
    app->hub.testMode = true;
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    pane->setSources({twitch, other, yt});
    Message youtubeHistory;
    youtubeHistory.id = "yt-old";
    youtubeHistory.name = "YouTube viewer";
    youtubeHistory.text = "old YouTube";
    youtubeHistory.stamp = QDateTime::fromMSecsSinceEpoch(1699999999000);
    youtubeHistory.history = true;
    yt->publish(youtubeHistory);
    QTRY_COMPARE(pane->view->messages.size(), 5);
    QCOMPARE(pane->view->messages[0].text, "old YouTube");
    QCOMPARE(pane->view->messages[1].text, "old B");
    QCOMPARE(pane->view->messages[2].text, "old A");
}

void NativeTests::twitchHistoryFailureCanRetry() {
    app->hub.testMode = false;
    BytesCallback pending;
    app->net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        pending = done;
    };
    twitch->loadHistory();
    QVERIFY(pending);
    pending({}, "HTTP 408: Request Timeout");
    QVERIFY(!twitch->historyLoaded);
    QVERIFY(!twitch->historyLoading);
    QVERIFY(twitch->historyRetry.isActive());
    twitch->historyRetry.stop();
    twitch->loadHistory();
    pending("{\"messages\":[]}", {});
    QVERIFY(twitch->historyLoaded);
    QVERIFY(!twitch->historyRetry.isActive());
    app->hub.testMode = true;
}

void NativeTests::twitchUserStateSuppliesOwnBadges() {
    app->hub.twitch.channels["guicraft_"] = twitch;
    app->hub.twitch.line("@badge-info=subscriber/12;badges=moderator/1,subscriber/12;color=#123456;"
                         "display-name=Tester;mod=1 :tmi.twitch.tv USERSTATE #guicraft_");
    QCOMPARE(twitch->ownTwitchUserId, "42");
    QCOMPARE(twitch->ownTwitchBadges.size(), 2);
    QVERIFY(twitch->twitchModeratorKnown && twitch->twitchModerator);
    bool done = false;
    app->net.transport = [](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback callback) {
        QCOMPARE(url.path(), "/helix/chat/messages");
        callback("{\"data\":[{\"is_sent\":true,\"message_id\":\"sent\"}]}", {});
    };
    app->hub.send(twitch, "hello", {}, [&](QJsonObject, QString error) { QVERIFY(error.isEmpty()); done = true; });
    QVERIFY(done);
    const auto own = twitch->history.last();
    QVERIFY(own.own);
    QCOMPARE(own.name, "Tester");
    QCOMPARE(own.color, "#123456");
    QCOMPARE(own.badges.size(), 2);
    QCOMPARE(own.badges[1].id, "subscriber");
    QCOMPARE(own.badges[1].version, "12");
    QCOMPARE(own.badgeInfo, "subscriber/12");

    twitch->resetTwitchUserState();
    Message late; late.id = "late"; late.uid = "42"; late.name = "tester"; late.text = "pending"; late.own = true;
    twitch->publish(late);
    app->hub.twitch.line("@badges=subscriber/6;color=#abcdef;display-name=Tester;mod=0 "
                         ":tmi.twitch.tv USERSTATE #guicraft_");
    QCOMPARE(twitch->history.last().badges.size(), 1);
    QCOMPARE(twitch->history.last().badges[0].version, "6");
}

void NativeTests::permissionAwareMenusAndUserCards() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    Message victim; victim.source = twitch->key; victim.id = "m"; victim.uid = "99";
    victim.name = "Viewer"; victim.login = "viewer"; victim.text = "hello";
    QMenu viewerMenu, viewerMessages;
    app->populatePaneMenu(viewerMenu, pane);
    app->populateMessageMenu(viewerMessages, pane, victim);
    QVERIFY(!viewerMenu.findChild<QAction *>("channel-edit"));
    QVERIFY(!viewerMessages.findChild<QMenu *>("moderation-menu"));
    app->show();
    app->showUser(pane, twitch, victim);
    auto card = app->userCards.value(twitch->key + ":99");
    QVERIFY(card);
    auto *row = card->findChild<QWidget *>("user-card-moderation");
    QVERIFY(row && row->isHidden());
    twitch->updateTwitchUserState({{"badges", "moderator/1"}, {"mod", "1"}});
    QMenu modMessages, modChannel;
    app->populateMessageMenu(modMessages, pane, victim);
    app->populatePaneMenu(modChannel, pane);
    QVERIFY(modMessages.findChild<QMenu *>("moderation-menu"));
    QVERIFY(!modChannel.findChild<QAction *>("channel-edit"));
    QVERIFY(!row->isHidden());
    twitch->header["channel_id"] = "42";
    QMenu ownerChannel;
    app->populatePaneMenu(ownerChannel, pane);
    auto *twEdit = ownerChannel.findChild<QAction *>("channel-edit");
    QVERIFY(twEdit && twEdit->text().contains("category"));

    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    yt->header["channel_id"] = "UCother";
    pane->setSources({yt});
    victim.source = yt->key;
    QMenu ytViewer;
    app->populateMessageMenu(ytViewer, pane, victim);
    QVERIFY(!ytViewer.findChild<QMenu *>("moderation-menu"));
    Message moderator; moderator.id = "own"; moderator.uid = "UCtest"; moderator.name = "Tester";
    moderator.text = "present in chat"; Badge badge; badge.role = "moderator"; moderator.badges << badge;
    yt->publish(moderator);
    QMenu ytModerator;
    app->populateMessageMenu(ytModerator, pane, victim);
    QVERIFY(ytModerator.findChild<QMenu *>("moderation-menu"));
    QMenu ytModChannel;
    app->populatePaneMenu(ytModChannel, pane);
    QVERIFY(!ytModChannel.findChild<QAction *>("channel-edit"));
    yt->header["channel_id"] = "UCtest";
    QMenu ytOwner;
    app->populatePaneMenu(ytOwner, pane);
    auto *ytEdit = ytOwner.findChild<QAction *>("channel-edit");
    QVERIFY(ytEdit);
    QCOMPARE(ytEdit->text(), "Change title…");
}

void NativeTests::twitchModerationPagination() {
    int requests = 0;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        QCOMPARE(url.path(), "/helix/moderation/channels");
        ++requests;
        QJsonObject response;
        if (QUrlQuery(url).queryItemValue("after").isEmpty())
            response = {{"data", QJsonArray{QJsonObject{{"broadcaster_id", "other"}}}},
                        {"pagination", QJsonObject{{"cursor", "next"}}}};
        else
            response = {{"data", QJsonArray{QJsonObject{{"broadcaster_id", twitch->room()}}}},
                        {"pagination", QJsonObject{}}};
        done(QJsonDocument(response).toJson(), {});
    };
    bool allowed = false;
    QString error;
    app->hub.canModerate(twitch, [&](bool ok, QString e) { allowed = ok; error = e; });
    QVERIFY(error.isEmpty());
    QVERIFY(allowed);
    QCOMPARE(requests, 2);
}

void NativeTests::singleChannelIconAndConnectionNotices() {
    const QString url = "https://static-cdn.jtvnw.net/user.png";
    auto entry = QSharedPointer<Images::Entry>::create();
    entry->image = QImage(24, 24, QImage::Format_ARGB32);
    entry->image.fill(Qt::green);
    app->images.cache[url] = entry;
    twitch->header["avatar"] = url;
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    QCOMPARE(pane->headerIcon->accessibleName(), twitch->name());
    QCOMPARE(pane->headerIcon->pixmap().toImage().pixelColor(10, 10), QColor(Qt::green));
    QMenu menu;
    app->populatePaneMenu(menu, pane);
    auto *channel = menu.actions().first()->menu();
    QVERIFY(channel && !channel->icon().isNull());
    QCOMPARE(channel->icon().pixmap(20, 20).toImage().pixelColor(10, 10), QColor(Qt::green));

    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    yt->connectionStatus("Connected to YouTube · no API key · 0 reading units");
    Message m; m.id = "1"; m.name = "Viewer"; m.text = "chat"; yt->publish(m);
    yt->connectionStatus("Connected to YouTube · no API key · 0 reading units");
    int notices = 0;
    for (const auto &entry : yt->history) if (entry.system) ++notices;
    QCOMPARE(notices, 1);
}
