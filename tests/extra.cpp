#include "native_tests.hpp"

void NativeTests::failedEmotesRetryAndNativeCodesDecode() {
    QCOMPARE(emoteName("&lt;3"), "<3");
    QCOMPARE(emoteName("&gt;("), ">(");
    QImage image(72, 72, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::yellow);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(image.save(&buffer, "PNG"));
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    QString url = "https://fonts.gstatic.com/emoji.png";
    bool fail = true;
    int requests = 0;
    app->net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        ++requests;
        auto bytes = fail ? QByteArray() : png;
        auto error = fail ? QString("temporary fixture failure") : QString();
        QTimer::singleShot(0, &app->net, [done, bytes, error] { done(bytes, error); });
    };
    Message m;
    m.id = "image";
    m.name = "Tester";
    m.text = ":two_hearts:";
    Span span;
    span.start = 0;
    span.end = m.text.size();
    span.code = m.text;
    span.urls = {url};
    m.emotes = {span};
    twitch->publish(m);
    pane->view->document(0);
    QTRY_VERIFY(app->images.failures.contains(url));
    pane->view->document(0);
    QVERIFY(!app->images.cache.contains(url));
    fail = false;
    app->images.failures[url] = QDateTime::currentSecsSinceEpoch() - 1;
    app->images.retryExpired();
    pane->view->document(0);
    QTRY_VERIFY(app->images.cache.contains(url));
    auto doc = pane->view->document(0);
    QVERIFY(doc->toPlainText().contains(QChar::ObjectReplacementCharacter));
    QCOMPARE(requests, 2);
}

void NativeTests::threadCapKeepsRoot() {
    app->cfg.data["message_limit"] = 100;
    Message root;
    root.id = "root";
    root.name = "Parent";
    root.login = "parent";
    root.text = "Original message";
    twitch->publish(root);
    Message reply;
    reply.id = "r1";
    reply.name = "Reply";
    reply.text = "First reply";
    reply.source = twitch->key;
    reply.reply = {{"id", "root"}, {"thread_id", "root"}, {"name", "Parent"}, {"text", "Original message"}};
    twitch->publish(reply);
    app->showThread(twitch, reply);
    auto thread = app->threads.begin().value();
    QVERIFY(thread);
    for (int i = 2; i < 155; ++i) {
        reply.id = "r" + QString::number(i);
        twitch->publish(reply);
    }
    QCOMPARE(thread->pane->view->messages.size(), 100);
    QCOMPARE(thread->pane->view->messages.first().id, "root");
    QCOMPARE(thread->pane->view->messages.last().id, "r154");
    emit app->cfg.appearanceChanged();
    QCOMPARE(thread->pane->view->messages.first().id, "root");
}

void NativeTests::singleWordIrcAndSerializedRefresh() {
    auto p = parseIrc("@display-name=Test;id=one :test!test@host PRIVMSG #guicraft_ hello");
    QCOMPARE(p.channel, "guicraft_");
    QCOMPARE(p.message.name, "Test");
    QCOMPARE(p.message.text, "hello");
    app->accounts.twitch["expires_at"] = 0;
    app->accounts.twitch["refresh_token"] = "fake-refresh";
    int calls = 0, ready = 0;
    BytesCallback response;
    app->net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        ++calls;
        response = done;
    };
    for (int i = 0; i < 5; ++i) {
        app->accounts.twitchToken([&](QString token, QString error) {
            QVERIFY(error.isEmpty());
            QCOMPARE(token, "refreshed");
            ++ready;
        });
    }
    QCOMPARE(calls, 1);
    response("{\"access_token\":\"refreshed\",\"expires_in\":3600}", {});
    QCOMPARE(ready, 5);
    QVERIFY(!app->accounts.twitchRefreshing);
}

void NativeTests::animatedImagesCompositeAndResume() {
    // Two-frame GIF fixture, disposal=2, transparent second frame. No live HTTP.
    QByteArray gif = QByteArray::fromHex(
        "47494638396102000200800000ff000000000021ff0b4e45545343415045322e30030100000021f904080a0000002c000000"
        "000200020000020284510021f904090a0001002c00000000020002000002028c53003b");
    QString url = "https://cdn.betterttv.net/emote/test/3x";
    app->net.transport = [gif](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>,
                               BytesCallback done) { done(gif, {}); };
    QSignalSpy ready(&app->images, &Images::ready), frames(&app->images, &Images::frame);
    app->images.get({url});
    QCOMPARE(ready.count(), 1);
    QVERIFY(app->images.cache.contains(url));
    auto entry = app->images.cache.value(url);
    QVERIFY(entry->movie);
    QCOMPARE(entry->movie->frameCount(), 2);
    auto first = app->images.get({url});
    QCOMPARE(first.size(), QSize(2, 2));
    QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 2, 500);
    entry->visibleAt = 0;
    QTRY_COMPARE_WITH_TIMEOUT(entry->movie->state(), QMovie::Paused, 1500);
    app->images.get({url});
    QCOMPARE(entry->movie->state(), QMovie::Running);
    QVERIFY(app->images.total > 0);
    QVERIFY(app->images.total < 1024);
    app->images.get({"https://evil.test/image.gif"});
    QVERIFY(app->images.pending.isEmpty());
}

void NativeTests::completionAndPickerPagination() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    for (int i = 0; i < 530; ++i) {
        Span e;
        e.code = QString("Test%1").arg(i, 3, 10, QChar('0'));
        twitch->emotes[e.code] = e;
    }
    pane->input->setText("Test00");
    pane->input->setCursorPosition(6);
    QTest::keyClick(pane->input, Qt::Key_Tab);
    QCOMPARE(pane->input->text(), "Test000 ");
    QTest::keyClick(pane->input, Qt::Key_Tab);
    QCOMPARE(pane->input->text(), "Test001 ");
    QTest::keyClick(pane->input, Qt::Key_Backtab);
    QCOMPARE(pane->input->text(), "Test000 ");
    app->accounts.twitch["scope"] = QJsonArray{"chat:read"};
    pane->openEmotes();
    auto dialogs = pane->findChildren<QDialog *>();
    QVERIFY(!dialogs.isEmpty());
    auto *d = dialogs.last();
    auto *grid = d->findChild<QListWidget *>();
    QVERIFY(grid);
    QCOMPARE(grid->count(), 200);
    QPushButton *next = nullptr;
    for (auto *b : d->findChildren<QPushButton *>()) {
        if (b->text() == "Next") {
            next = b;
        }
    }
    QVERIFY(next);
    next->click();
    QCOMPARE(grid->count(), 200);
    next->click();
    QCOMPARE(grid->count(), 130);
    QVERIFY(!next->isEnabled());
    auto search = d->findChild<QLineEdit *>();
    search->setText("Test529");
    QCOMPARE(grid->count(), 1);
    d->close();
}

void NativeTests::liveAlertsBatchClosedWatchesAndInitialSilence() {
    auto f = app->cfg.features(), a = f["alerts"].toObject();
    a["enabled"] = true;
    a["watch"] = QJsonArray{QJsonObject{{"platform", "twitch"}, {"id", "another_channel"}}};
    f["alerts"] = a;
    app->cfg.data["features"] = f;
    QSignalSpy sound(&app->hub, &Hub::alertPlayed);
    int calls = 0;
    bool live = false;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>,
                             BytesCallback done) {
        ++calls;
        QCOMPARE(url.path(), "/helix/streams");
        auto users = QUrlQuery(url).allQueryItemValues("user_login");
        QCOMPARE(users.size(), 2);
        QVERIFY(users.contains("another_channel"));
        QJsonArray data;
        if (live) {
            data.append(QJsonObject{{"user_login", "another_channel"}, {"viewer_count", 17}});
        }
        done(QJsonDocument(QJsonObject{{"data", data}}).toJson(), {});
    };
    app->hub.pollLive();
    QCOMPARE(sound.count(), 0);
    live = true;
    app->hub.pollLive();
    QCOMPARE(sound.count(), 1);
    app->hub.pollLive();
    QCOMPARE(sound.count(), 1);
    QCOMPARE(calls, 3);
    app->hub.source("youtube", "0sfDth4lMLc");
    QCOMPARE(sound.count(), 1);
}

void NativeTests::reconnectIgnoresStaleYouTubeReply() {
    auto *s = app->hub.source("youtube", "0sfDth4lMLc");
    s->continuation = "old";
    s->context = {{"client", QJsonObject{{"clientVersion", "1"}}}};
    BytesCallback held;
    app->net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        held = done;
    };
    s->youtubePoll();
    QVERIFY(s->requesting);
    s->reconnect();
    QVERIFY(!s->requesting);
    held("{\"continuationContents\":{\"liveChatContinuation\":{\"actions\":[]}}}", {});
    QVERIFY(s->continuation.isEmpty());
    QVERIFY(!s->poll.isActive());
}

void NativeTests::rejectedTwitchAuthStopsRetry() {
    app->hub.twitch.channels["guicraft_"] = twitch;
    app->hub.twitch.line(":tmi.twitch.tv NOTICE * :Login authentication failed");
    QVERIFY(app->hub.twitch.authRejected);
    QVERIFY(!app->hub.twitch.retry.isActive());
    QVERIFY(twitch->history.last().text.contains("sign-in expired"));
    emit app->accounts.ready();
    QVERIFY(!app->hub.twitch.authRejected);
}

void NativeTests::zoomSelectionAndLastTabControls() {
    auto *w = app->addWorkspace({twitch});
    auto *p = w->focusedPane.data();
    auto *q = w->add({twitch}, p);
    w->toggleZoom(q);
    QVERIFY(!w->zoomHidden.isEmpty());
    w->toggleZoom(q);
    QVERIFY(w->zoomHidden.isEmpty());
    Message a;
    a.seq = 1;
    a.name = "A";
    a.text = "first";
    Message b = a;
    b.seq = 2;
    b.text = "second";
    p->view->setMessages({a, b});
    p->view->selectionStart = {1, 0};
    p->view->selectionEnd = {1, 3};
    p->view->setMessages({b});
    QCOMPARE(p->view->selectionStart.first, 0);
    p->view->setMessages({a});
    QCOMPARE(p->view->selectionStart.first, -1);
    app->show();
    app->closeTab(0);
    QCoreApplication::processEvents();
    QVERIFY(app->addButton->isVisible());
    QVERIFY(app->gear->isVisible());
}

static QJsonObject transaction(const QString &base) {
    QString target = base + "/app", container = base + "/.ytchatterino-update-fixture",
            stage = container + "/extracted/YT-Chatterino";
    QDir().mkpath(target);
    QDir().mkpath(stage);
    return {{"kind", "windows"},
            {"target", target},
            {"stage", stage},
            {"container", container},
            {"backup", base + "/.ytchatterino-backup-fixture"},
            {"version", APP_VERSION},
            {"parent_pid", 2147483647},
            {"profile", base + "/profile"}};
}

void NativeTests::updateTransactionSuccessAndPreservation() {
    QTemporaryDir dirs(temp.path() + "/update-XXXXXX");
    QVERIFY(dirs.isValid());
    auto m = transaction(dirs.path());
    auto target = str(m["target"]), stage = str(m["stage"]), container = str(m["container"]);
    QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), stage + "/YT-Chatterino.exe"));
    QFile marker(stage + "/test-update-child");
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.close();
    writeObject(target + "/update-manifest.json",
                {{"version", "1.9.9"}, {"files", QJsonArray{"update-manifest.json"}}});
    writeObject(stage + "/update-manifest.json",
                {{"version", APP_VERSION},
                 {"files", QJsonArray{"YT-Chatterino.exe", "test-update-child", "update-manifest.json"}}});
    writeObject(target + "/private.json", {{"keep", true}});
    QDir().mkpath(target + "/empty-user-folder");
    writeObject(container + "/update.json", m);
    QVERIFY(Updater::validTransaction(m, container + "/update.json"));
    QCOMPARE(Updater::applyUpdate(container + "/update.json", false), 0);
    QVERIFY(readObject(target + "/private.json")["keep"].toBool());
    QVERIFY(QFileInfo(target + "/empty-user-folder").isDir());
    QVERIFY(QFileInfo::exists(str(m["backup"]) + "/private.json"));
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(container + "/child-running"), 3000);
}

void NativeTests::updateTransactionRollbackAndInvalidPaths() {
    QTemporaryDir dirs(temp.path() + "/rollback-XXXXXX");
    QVERIFY(dirs.isValid());
    auto m = transaction(dirs.path());
    auto target = str(m["target"]), stage = str(m["stage"]), container = str(m["container"]);
    writeObject(target + "/update-manifest.json",
                {{"version", "1.9.9"}, {"files", QJsonArray{"update-manifest.json"}}});
    writeObject(stage + "/update-manifest.json",
                {{"version", APP_VERSION}, {"files", QJsonArray{"update-manifest.json"}}});
    writeObject(target + "/private.json", {{"keep", true}});
    writeObject(container + "/update.json", m);
    QCOMPARE(Updater::applyUpdate(container + "/update.json", false), 1);
    QVERIFY(readObject(target + "/private.json")["keep"].toBool());
    QCOMPARE(str(readObject(target + "/update-manifest.json")["version"]), "1.9.9");
    auto bad = m;
    bad["target"] = dirs.path();
    QVERIFY(!Updater::validTransaction(bad, container + "/update.json"));
    writeObject(dirs.path() + "/bad.json", {});
    QCOMPARE(Updater::applyUpdate(dirs.path() + "/bad.json", false), 1);
    QVERIFY(!QFileInfo::exists(dirs.path() + "/failure.json"));
}

void NativeTests::resizeBenchmark() {
    auto *p = app->addWorkspace({twitch})->focusedPane.data();
    for (int i = 0; i < 2000; ++i) {
        Message m;
        m.id = QString::number(i);
        m.name = "ResizeTester";
        m.text = "Native retained chat resize benchmark. A long line to wrap without laying out two thousand "
                 "documents.";
        twitch->publish(m);
    }
    app->show();
    QCoreApplication::processEvents();
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 80; ++i) {
        app->resize(500 + i % 20 * 12, 500 + i % 13 * 9);
        QCoreApplication::processEvents();
    }
    QTest::qWait(60);
    qInfo() << "RESIZE_80_MS" << timer.elapsed() << "DOCUMENT_CACHE" << p->view->documents.size();
    QCOMPARE(p->view->messages.size(), 2000);
    QVERIFY(p->view->documents.size() <= 128);
}
