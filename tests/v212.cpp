#include "native_tests.hpp"

void NativeTests::emoteHoverShowsCorrectPreview() {
    ChatView view(&app->hub);
    view.resize(480, 200);
    view.show();
    const QString firstUrl = "https://cdn.betterttv.net/emote/first/3x";
    const QString secondUrl = "https://cdn.7tv.app/emote/second/3x";
    for (const auto &url : {firstUrl, secondUrl}) {
        auto entry = QSharedPointer<Images::Entry>::create();
        entry->image = QImage(24, 24, QImage::Format_ARGB32);
        entry->image.fill(Qt::green);
        app->images.cache[url] = entry;
    }
    Message m;
    m.seq = 9212;
    m.name = "Tester";
    m.source = twitch->key;
    m.text = "Alpha Beta";
    m.emotes = {{0, 5, "Alpha", {firstUrl}, false}, {6, 10, "Beta", {secondUrl}, false}};
    view.setMessages({m});
    auto doc = view.document(0);
    (void)doc->size();
    auto emotes = view.details.value(m.seq).emoteImages;
    QCOMPARE(emotes.size(), 2);
    auto pointFor = [&](int pos) {
        auto block = doc->findBlock(pos);
        auto line = block.layout()->lineForTextPosition(pos - block.position());
        auto bounds = doc->documentLayout()->blockBoundingRect(block);
        const qreal a = line.cursorToX(pos - block.position());
        const qreal b = line.cursorToX(pos - block.position() + 1);
        return QPoint(qRound(6 + bounds.left() + (a + b) / 2),
                      qRound(3 + bounds.top() + line.y() + line.height() / 2));
    };
    for (auto i = emotes.cbegin(); i != emotes.cend(); ++i) {
        const auto point = pointFor(i.key());
        QVERIFY(view.showEmoteTooltip(point, view.viewport()->mapToGlobal(point)));
        QCOMPARE(view.emoteTooltipName->text(), i.value().first);
        QVERIFY(!view.emotePreview->pixmap().isNull());
        QCOMPARE(view.emoteTooltipProvider->text(), i.value().first == "Alpha" ? "BTTV · Emote" : "7TV · Emote");
    }
    QVERIFY(!view.showEmoteTooltip(QPoint(470, 180), view.viewport()->mapToGlobal(QPoint(470, 180))));
    QVERIFY(!view.emoteTooltip->isVisible());
}

void NativeTests::youtubeRateLimitCoordinatesChats() {
    auto *one = app->hub.source("youtube", "0sfDth4lMLc");
    auto *two = app->hub.source("youtube", "pumlxj4Ct_g");
    two->continuation = "continuation";
    one->youtubeError("HTTP 429: rate limit");
    QCOMPARE(one->poll.interval(), 120000);
    QVERIFY(app->hub.youtubeCooldownRemaining() > 119000);
    two->youtubePoll();
    QVERIFY(two->poll.isActive());
    QVERIFY(two->poll.interval() > 119000);
    QVERIFY(unexpectedRequests.isEmpty());
    app->hub.testMode = false;
    two->refreshHeader();
    QVERIFY(unexpectedRequests.isEmpty());
    app->hub.testMode = true;
    one->youtubeError("HTTP 429: rate limit");
    QCOMPARE(one->poll.interval(), 240000);
    one->setPaused(true);
    two->setPaused(true);
}

void NativeTests::youtubeApiFallbackReadsAndAccountsQuota() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    yt->apiFallbackEnabled = true;
    QStringList paths;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray,
                             QMap<QByteArray, QByteArray> headers, BytesCallback done) {
        paths << url.path();
        QCOMPARE(method, QByteArray("GET"));
        QVERIFY(headers.value("Authorization").startsWith("Bearer "));
        if (url.path().endsWith("/videos")) {
            done("{\"items\":[{\"liveStreamingDetails\":{\"activeLiveChatId\":\"chat\"}}]}", {});
        } else {
            const auto query = QUrlQuery(url);
            QCOMPARE(query.queryItemValue("liveChatId"), "chat");
            done("{\"nextPageToken\":\"page\",\"pollingIntervalMillis\":4000,\"items\":[{\"id\":\"m1\",\"snippet\":{\"type\":\"textMessageEvent\",\"displayMessage\":\"hello\",\"publishedAt\":\"2026-10-26T12:00:00Z\"},\"authorDetails\":{\"channelId\":\"u1\",\"displayName\":\"Viewer\",\"isChatModerator\":true}}]}", {});
        }
    };
    yt->youtubeError("HTTP 429: rate limit");
    QVERIFY(yt->apiActive);
    QCOMPARE(paths.size(), 2);
    QCOMPARE(app->quota.used(), 6);
    QCOMPARE(yt->apiPageToken, "page");
    QCOMPARE(yt->poll.interval(), 18250);
    QCOMPARE(yt->history.last().text, "hello");
    QCOMPARE(yt->history.last().apiId, "m1");
    QCOMPARE(yt->history.last().badges.first().role, "moderator");
    QVERIFY(yt->history.last().history);
}

void NativeTests::youtubeApiFallbackRespectsQuotaAndSignIn() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    yt->apiFallbackEnabled = true;
    app->accounts.google = {};
    yt->youtubeError("HTTP 429: rate limit");
    QVERIFY(!yt->apiActive);
    QVERIFY(yt->history.last().text.contains("Retrying"));
    QVERIFY(unexpectedRequests.isEmpty());
    authenticate();
    yt->apiActive = true;
    yt->header["live_chat_id"] = "cached";
    app->cfg.data["youtube_daily_limit"] = 4;
    yt->youtubeApiPoll();
    QVERIFY(yt->history.last().text.contains("quota is exhausted"));
    QVERIFY(unexpectedRequests.isEmpty());
}

void NativeTests::youtubeFallbackMenuRequiresConfirmation() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    auto *pane = app->addWorkspace({yt})->focusedPane.data();
    QMenu menu;
    app->populatePaneMenu(menu, pane);
    auto *action = menu.findChild<QAction *>("youtube-api-fallback");
    QVERIFY(action);
    QVERIFY(!action->isChecked());
    QTimer::singleShot(0, [] {
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                box->button(QMessageBox::No)->click();
            }
        }
    });
    action->trigger();
    QVERIFY(!yt->apiFallbackEnabled);
    action->setChecked(false);
    QTimer::singleShot(0, [] {
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                box->button(QMessageBox::Yes)->click();
            }
        }
    });
    action->trigger();
    QVERIFY(yt->apiFallbackEnabled);
    QVERIFY(app->cfg.data["chat_settings"].toObject()[yt->key].toObject()["youtube_api_fallback"].toBool());
    QVERIFY(unexpectedRequests.isEmpty());
    action->trigger();
    QVERIFY(!yt->apiFallbackEnabled);
    QVERIFY(!app->cfg.data["chat_settings"].toObject()[yt->key].toObject()["youtube_api_fallback"].toBool());
}
