#include "native_tests.hpp"

void NativeTests::emotesPreloadInBackground() {
    QImage image(8, 8, QImage::Format_ARGB32); image.fill(Qt::blue);
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); QVERIFY(image.save(&buffer, "PNG"));
    QVector<BytesCallback> requests; QStringList requested;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        requested << url.toString(); requests << done;
    };
    for (int i = 0; i < 6; ++i)
        app->images.preload({QString("https://cdn.betterttv.net/emote/%1/3x").arg(i)});
    app->images.preload({"https://cdn.betterttv.net/emote/0/3x"}); // Duplicate.
    app->images.preload({"https://cdn.betterttv.net/emote/closed/3x"}, [] { return false; });
    QTRY_COMPARE(requests.size(), 3);
    app->images.get({"https://cdn.betterttv.net/emote/foreground/3x"});
    QCOMPARE(requests.size(), 4); // Visible chat requests do not wait for the preload queue.
    auto foreground = requests[3]; foreground(png, {});
    for (int i : {0, 1, 2}) { auto done = requests[i]; done(png, {}); }
    QTRY_COMPARE(requests.size(), 7);
    for (int i : {4, 5, 6}) { auto done = requests[i]; done(png, {}); }
    QTRY_COMPARE(app->images.preloadActive, 0);
    QCOMPARE(requests.size(), 7);
    QVERIFY(!requested.contains("https://cdn.betterttv.net/emote/closed/3x"));
    for (int i = 0; i < 6; ++i)
        QVERIFY(!app->images.get({QString("https://cdn.betterttv.net/emote/%1/3x").arg(i)}, false).isNull());

    const QString animation = "https://cdn.7tv.app/emote/walter/2x.webp";
    const QString still = "https://cdn.7tv.app/emote/walter/1x_static.webp";
    app->images.preload({animation, still});
    QTRY_COMPARE(requests.size(), 8);
    QCOMPARE(requested.last(), still);
    requests[7](png, {});
    QVERIFY(app->images.cache.value(animation)->preview);
    QVERIFY(!app->images.get({animation, still}, false).isNull());
    QCOMPARE(requests.size(), 8);
    app->images.get({animation, still}, true);
    QCOMPARE(requests.size(), 9); // The animation starts only when displayed.
    QCOMPARE(requested.last(), animation);

    app->hub.testMode = false;
    app->hub.provider(twitch, QJsonArray{QJsonObject{{"id", "startup"}, {"code", "OnOpen"}}}, "bttv", "channel");
    app->hub.testMode = true;
    QTRY_COMPARE(requests.size(), 10);
    QCOMPARE(requested.last(), QString("https://cdn.betterttv.net/emote/startup/2x"));
}

void NativeTests::mixedChatSharesChannelEmotes() {
    auto *youtube = app->hub.source("youtube", "abcdefghijk");
    Span walter; walter.code = "WalterVibe"; walter.urls = {"https://cdn.7tv.app/emote/test/1x.webp"};
    twitch->emotes[walter.code] = walter; twitch->channelEmotes[walter.code] = walter;
    auto cached = QSharedPointer<Images::Entry>::create();
    cached->image = QImage(16, 16, QImage::Format_ARGB32); cached->image.fill(Qt::red);
    app->images.cache[walter.urls.first()] = cached;
    auto *pane = app->addWorkspace({twitch, youtube})->focusedPane.data();
    Message m; m.seq = 9001; m.id = "mixed-emote"; m.source = youtube->key; m.name = "Viewer"; m.text = "WalterVibe";
    pane->view->setMessages({m});
    QVERIFY(!pane->view->document(0)->toPlainText().contains("WalterVibe"));
    QVERIFY(pane->view->emoteCatalog(youtube).contains("WalterVibe"));

    Span own = walter; own.urls = {"https://cdn.betterttv.net/emote/own/2x"};
    youtube->emotes[own.code] = own; youtube->channelEmotes[own.code] = own;
    QCOMPARE(pane->view->emoteFor(youtube, "WalterVibe")->urls, own.urls);
    youtube->emotes.clear(); youtube->channelEmotes.clear();

    auto *separate = app->addWorkspace({youtube})->focusedPane.data();
    separate->view->setMessages({m});
    QVERIFY(separate->view->document(0)->toPlainText().contains("WalterVibe"));
    QVERIFY(!separate->view->emoteCatalog(youtube).contains("WalterVibe"));
    pane->setSources({youtube}); pane->view->setMessages({m});
    QVERIFY(pane->view->document(0)->toPlainText().contains("WalterVibe"));

    int sevenRequests = 0;
    youtube->header["channel_id"] = "youtube-channel";
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        if (url.host() == "7tv.io" || url.host() == "api.7tv.app") { ++sevenRequests; done({}, "HTTP 400: invalid platform"); }
        else done(R"({"channelEmotes":[],"sharedEmotes":[]})", {});
    };
    youtube->reloadChannelEmotes();
    QCOMPARE(sevenRequests, 1);
    QVERIFY(!youtube->emoteRetryUntil.contains("youtube-channel:7tv"));
    QVERIFY(std::any_of(youtube->history.cbegin(), youtube->history.cend(), [](const Message &message) {
        return message.system && message.text == "7TV does not provide a channel set for this platform.";
    }));
}

void NativeTests::animatedSevenTvShowsStillWhileLoading() {
    auto png = [](QColor color) {
        QImage image(16, 16, QImage::Format_ARGB32); image.fill(color);
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
        return bytes;
    };
    const QByteArray still = png(Qt::green);
    const QByteArray animatedResponse = QByteArray::fromHex(
        "47494638396102000200800000ff000000000021ff0b4e45545343415045322e30030100000021f904080a0000002c000000"
        "000200020000020284510021f904090a0001002c00000000020002000002028c53003b");
    BytesCallback delayed;
    int previews = 0, animations = 0;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        if (url.path().endsWith("_static.webp")) { ++previews; done(still, {}); }
        else { ++animations; delayed = done; }
    };
    const auto set = QJsonDocument::fromJson(R"([{"id":"real-emote","name":"WalterVibe","data":{"animated":true,"host":{"url":"//cdn.7tv.app/emote/real-emote","files":[{"name":"4x.webp","static_name":"4x_static.webp","format":"WEBP","height":128,"width":228},{"name":"1x.webp","static_name":"1x_static.webp","format":"WEBP","height":32,"width":57},{"name":"2x.webp","static_name":"2x_static.webp","format":"WEBP","height":64,"width":114}]}}}])").array();
    app->hub.provider(twitch, set, "7tv", "channel");
    const auto urls = twitch->emotes.value("WalterVibe").urls;
    QVERIFY(urls.first().endsWith("/2x.webp"));
    QVERIFY(urls.contains("https://cdn.7tv.app/emote/real-emote/1x_static.webp"));
    app->images.get(urls);
    QCOMPARE(previews, 1); QCOMPARE(animations, 1); QVERIFY(bool(delayed));
    QCOMPARE(app->images.get(urls).pixelColor(0, 0), QColor(Qt::green));
    QVERIFY(app->images.pending.contains(urls.first()));
    delayed(animatedResponse, {});
    QCOMPARE(app->images.get(urls).pixelColor(0, 0), QColor(Qt::red));
    QVERIFY(app->images.cache.value(urls.first())->movie);
    QCOMPARE(app->images.cache.value(urls.first())->movie->frameCount(), 2);
    QVERIFY(!app->images.pending.contains(urls.first()));
    QCOMPARE(app->images.total, app->images.cache.value(urls.first())->cost);

    BytesCallback lateStill;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        if (url.path().endsWith("_static.webp")) lateStill = done;
        else done(animatedResponse, {});
    };
    QStringList second = urls;
    for (auto &url : second) url.replace("real-emote", "other-emote");
    app->images.get(second);
    QVERIFY(bool(lateStill)); QVERIFY(app->images.cache.value(second.first())->movie);
    lateStill(still, {});
    QVERIFY(app->images.cache.value(second.first())->movie);

    int attempts = 0;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        if (!url.path().endsWith("_static.webp")) return; // Keep the animation in flight.
        if (++attempts == 1) done({}, "temporary connection failure");
        else done(still, {});
    };
    QStringList third = urls;
    for (auto &url : third) url.replace("real-emote", "retry-emote");
    app->images.get(third);
    QTRY_COMPARE_WITH_TIMEOUT(attempts, 2, 4000);
    QCOMPARE(app->images.get(third).pixelColor(0, 0), QColor(Qt::green));
}
