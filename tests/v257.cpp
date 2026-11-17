#include "native_tests.hpp"

void NativeTests::roleChangesAndCommands() {
    twitch->header["channel_id"] = "42";
    auto scopes = app->accounts.twitch["scope"].toArray();
    scopes.append("channel:manage:moderators"); scopes.append("channel:manage:vips");
    app->accounts.twitch["scope"] = scopes;
    Message user; user.id = "msg"; user.uid = "99"; user.login = "viewer"; user.name = "Viewer";
    twitch->publish(user);
    QList<QString> requests;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray body, QMap<QByteArray, QByteArray>, BytesCallback done) {
        requests << QString::fromUtf8(method) + " " + url.path();
        if (url.path() == "/helix/users") {
            if (QUrlQuery(url).queryItemValue("login") == "absent") done(R"({"data":[]})", {});
            else done(R"({"data":[{"id":"99","login":"offlineviewer","display_name":"OfflineViewer"}]})", {});
            return;
        }
        if (url.path().startsWith("/helix/")) {
            QCOMPARE(QUrlQuery(url).queryItemValue("broadcaster_id"), QString("42"));
            if (url.path() != "/helix/moderation/bans") QCOMPARE(QUrlQuery(url).queryItemValue("user_id"), QString("99"));
            else QCOMPARE(str(at(QJsonDocument::fromJson(body).object(), {"data", "user_id"})), QString("99"));
        }
        done({}, {});
    };
    QString error;
    auto done = [&](QJsonObject, QString e) { error = e; };
    app->hub.send(twitch, "/mod @viewer", {}, done);
    QVERIFY2(error.isEmpty(), qPrintable(error)); QCOMPARE(requests.last(), QString("POST /helix/moderation/moderators"));
    app->hub.send(twitch, "/unvip viewer", {}, done);
    QVERIFY(error.isEmpty()); QCOMPARE(requests.last(), QString("DELETE /helix/channels/vips"));
    app->hub.send(twitch, "/timeout viewer 60", {}, done);
    QVERIFY(error.isEmpty()); QCOMPARE(requests.last(), QString("POST /helix/moderation/bans"));
    app->hub.send(twitch, "/vip offlineviewer", {}, done);
    QVERIFY(error.isEmpty()); QCOMPARE(requests.last(), QString("POST /helix/channels/vips"));
    int before = requests.size();
    app->hub.send(twitch, "/timeout viewer invalid", {}, done); QVERIFY(!error.isEmpty()); QCOMPARE(requests.size(), before);
    app->hub.send(twitch, "/ban absent", {}, done); QVERIFY(!error.isEmpty()); QCOMPARE(requests.size(), before + 1);
    before = requests.size();
    Message duplicate = user; duplicate.uid = "100"; duplicate.id = "other"; twitch->publish(duplicate);
    app->hub.send(twitch, "/ban viewer", {}, done); QVERIFY(!error.isEmpty()); QCOMPARE(requests.size(), before);
    twitch->header["channel_id"] = "12";
    app->hub.setUserRole(twitch, user, "moderator", true, done); QVERIFY(!error.isEmpty()); QCOMPARE(requests.size(), before);
    twitch->header["channel_id"] = "42";
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    pane->input->setText("/unv"); QTest::keyClick(pane->input, Qt::Key_Tab);
    QCOMPARE(pane->input->text(), QString("/unvip "));

    auto *youtube = app->hub.source("youtube", "abcdefghijk");
    youtube->header["channel_id"] = "UCtest"; youtube->header["live_chat_id"] = "chat123";
    user.uid = "UCviewer";
    int page = 0;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray body, QMap<QByteArray, QByteArray>, BytesCallback reply) {
        QCOMPARE(url.path(), QString("/youtube/v3/liveChat/moderators"));
        if (method == "POST") {
            QCOMPARE(str(at(QJsonDocument::fromJson(body).object(), {"snippet", "moderatorDetails", "channelId"})), QString("UCviewer"));
            reply("{}", {}); return;
        }
        if (method == "DELETE") {
            QCOMPARE(QUrlQuery(url).queryItemValue("id"), QString("moderator-resource")); reply({}, {}); return;
        }
        QCOMPARE(method, QByteArray("GET"));
        if (page++ == 0) reply(R"({"items":[],"nextPageToken":"next"})", {});
        else {
            QCOMPARE(QUrlQuery(url).queryItemValue("pageToken"), QString("next"));
            reply(R"({"items":[{"id":"moderator-resource","snippet":{"moderatorDetails":{"channelId":"UCviewer"}}}]})", {});
        }
    };
    const int used = app->quota.used();
    app->hub.setUserRole(youtube, user, "moderator", true, done); QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(app->quota.used() - used, 200);
    app->hub.setUserRole(youtube, user, "moderator", false, done); QVERIFY2(error.isEmpty(), qPrintable(error)); QCOMPARE(page, 2);
    QCOMPARE(app->quota.used() - used, 500);
    app->hub.setUserRole(youtube, user, "vip", true, done); QVERIFY(!error.isEmpty());
}

void NativeTests::roleButtonsTrackChannelOwnership() {
    twitch->header["channel_id"] = "42";
    auto scopes = app->accounts.twitch["scope"].toArray(); scopes.append("channel:manage:moderators");
    app->accounts.twitch["scope"] = scopes;
    int mutations = 0;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        if (url.path() == "/helix/moderation/moderators" && method == "POST") {
            ++mutations; done({}, {}); return;
        }
        done(R"({"data":[]})", {});
    };
    Message user; user.id = "msg"; user.uid = "99"; user.name = "Viewer"; user.login = "viewer"; user.source = twitch->key;
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    app->show(); app->showUser(pane, twitch, user);
    auto card = app->userCards.value(twitch->key + ":99"); QVERIFY(card);
    auto *row = card->findChild<QWidget *>("user-card-roles"); QVERIFY(row); QVERIFY(!row->isHidden());
    auto *button = card->findChild<QPushButton *>("role-mod"); QVERIFY(button); button->click();
    QCOMPARE(mutations, 1);
    auto *status = card->findChild<QLabel *>("user-card-role-status"); QVERIFY(status);
    QCOMPARE(status->text(), QString("Role updated."));
    twitch->header["channel_id"] = "12"; emit twitch->headerChanged(); QVERIFY(row->isHidden());
}

void NativeTests::kickOgBadgeVisible() {
    const QJsonObject identity{{"badges", QJsonArray{QJsonObject{{"type", "og"}, {"text", "OG"}}}}};
    const auto message = parseKick({{"id", "x"}, {"sender", QJsonObject{{"id", 1}, {"username", "User"}, {"identity", identity}}}});
    QCOMPARE(message.badges.size(), 1); QCOMPARE(message.badges.first().role, QString("og"));
    auto *source = app->hub.source("kick", "badges"); source->publish(message);
    auto *pane = app->addWorkspace({source})->focusedPane.data();
    pane->view->setMessages(source->history);
    const auto doc = pane->view->document(0); QVERIFY(doc);
    QVERIFY(doc->toHtml().contains("badge:og"));
    auto *vod = app->hub.vodSource(QUrl("https://kick.com/badges/videos/00000000-0000-0000-0000-000000000001"));
    QVERIFY(vod); vod->publish(message); pane->setSources({vod}); pane->view->setMessages(vod->history);
    QVERIFY(pane->view->document(0)->toHtml().contains("badge:og"));
}
