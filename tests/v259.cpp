#include "native_tests.hpp"

void NativeTests::sevenTvFallbackAndExplicitEmotes() {
    int primary = 0, fallback = 0;
    twitch->header["channel_id"] = "123";
    QImage image(16, 16, QImage::Format_ARGB32); image.fill(Qt::red);
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); QVERIFY(image.save(&buffer, "PNG"));
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        if (url.host() == "7tv.io") { ++primary; done({}, "HTTP 503: temporary"); }
        else if (url.host() == "api.7tv.app") {
            ++fallback;
            done(R"({"emote_set":{"emotes":[{"id":"explicit1","name":"ExplicitEmote","data":{"flags":65536,"host":{"url":"//cdn.7tv.app/emote/explicit1","files":[{"name":"1x.png","format":"PNG","width":16}]}}}]}})", {});
        } else if (url.host() == "cdn.7tv.app") done(png, {});
        else if (url.host() == "api.betterttv.net") done(R"({"channelEmotes":[],"sharedEmotes":[]})", {});
        else done(R"({"data":[]})", {});
    };
    twitch->reloadChannelEmotes();
    QCOMPARE(primary, 1); QCOMPARE(fallback, 1);
    QVERIFY(twitch->emotes.contains("ExplicitEmote"));
    QVERIFY(twitch->emotes.value("ExplicitEmote").explicit7tv);
    const auto urls = twitch->emotes.value("ExplicitEmote").urls;
    QVERIFY(!urls.isEmpty());
    app->images.get(urls);
    QTRY_VERIFY(!app->images.get(urls).isNull());
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    Message message; message.id = "explicit-test"; message.name = "Tester"; message.text = "ExplicitEmote";
    twitch->publish(message);
    auto displayedText = [pane] {
        for (int i = 0; i < pane->view->messages.size(); ++i)
            if (pane->view->messages[i].id == "explicit-test") return pane->view->document(i)->toPlainText();
        return QStringLiteral("message missing");
    };
    QVERIFY(displayedText() != "message missing");
    QVERIFY(!displayedText().contains("ExplicitEmote"));
    app->showSettings("Messages");
    auto *setting = app->settings->findChild<QCheckBox *>("show-explicit-7tv");
    QVERIFY(setting); QVERIFY(setting->isChecked());
    setting->setChecked(false);
    QVERIFY(displayedText().contains("ExplicitEmote"));
    setting->setChecked(true);
    QVERIFY(!displayedText().contains("ExplicitEmote"));

    int requests = 0;
    app->net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        ++requests; done({}, "HTTP 404: not found");
    };
    QString returnedError;
    app->net.sevenTv("users/twitch/123", false, [&](QJsonObject, QString error) { returnedError = error; });
    QCOMPARE(requests, 1); QVERIFY(returnedError.startsWith("HTTP 404"));
}

void NativeTests::similarChannelVariantsAndConcurrency() {
    auto *source = app->hub.source("twitch", "cellbit");
    auto *pane = app->addWorkspace({source})->focusedPane.data();
    QSet<QString> seen; int active = 0, maxActive = 0;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        ++active; maxActive = qMax(maxActive, active); seen.insert(url.path());
        const bool match = url.host() == "www.youtube.com" && url.path() == "/@livesdo_cellbit/streams";
        QTimer::singleShot(0, &app->net, [&, done, match] {
            --active;
            if (match) done(R"(ytInitialData = {"contents":[{"videoRenderer":{"videoId":"abcdefghijk","title":{"simpleText":"Live"},"badges":[{"style":"LIVE"}]}}]};)", {});
            else done({}, "HTTP 404");
        });
    };
    app->suggestSimilarLive(source, pane, true);
    QTRY_VERIFY(app->findChild<QListWidget *>("similar-live-channels"));
    auto *list = app->findChild<QListWidget *>("similar-live-channels");
    QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->checkState(), Qt::Unchecked);
    for (const auto &handle : {"livedo_cellbit", "livesdo_cellbit", "live_cellbit", "lives_cellbit", "cellbit_live", "cellbit_lives"})
        QVERIFY2(seen.contains(QString("/@%1/streams").arg(handle)), handle);
    QVERIFY(maxActive <= 6); QVERIFY(maxActive > 1);
    QCOMPARE(pane->sources.size(), 1);
    qobject_cast<QDialog *>(list->window())->reject();
}
