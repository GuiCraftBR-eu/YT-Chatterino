#include "native_tests.hpp"

static QByteArray livePage(int count = 1) {
    QJsonArray entries;
    for (int i = 0; i < count; ++i) {
        const QString id = i == 0 ? "0sfDth4lMLc" : "4t4AQ15yj1M";
        QJsonObject lockup{{"contentId", id},
            {"metadata", QJsonObject{{"lockupMetadataViewModel", QJsonObject{
                {"title", QJsonObject{{"content", QString("Live %1").arg(i + 1)}}}}}}},
            {"contentImage", QJsonObject{{"badgeStyle", "THUMBNAIL_OVERLAY_BADGE_STYLE_LIVE"}}}};
        entries.append(QJsonObject{{"lockupViewModel", lockup}});
    }
    return "var ytInitialData = " + QJsonDocument(QJsonObject{{"contents", entries}}).toJson(QJsonDocument::Compact) + ";";
}

static QDialog *addDialog(MainWindow *app) {
    for (auto *d : app->findChildren<QDialog *>())
        if (d->windowTitle() == "Add chats" && d->isVisible()) return d;
    return nullptr;
}

void NativeTests::quickAddSavedAndMultipleNames() {
    app->cfg.data["saved_channels"] = QJsonArray{
        QJsonObject{{"platform", "kick"}, {"channel_id", "savedkick"}, {"title", "Saved Kick"}},
        QJsonObject{{"platform", "youtube"}, {"channel_id", "UCabcdefghijklmnopqrstuv"}, {"title", "Saved YouTube"}}};
    app->net.transport = [](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        if (url.host() == "www.youtube.com" && url.path().endsWith("/streams")) done(livePage(), {});
        else done({}, "Not an image in this fixture");
    };
    app->addChats();
    auto *dialog = addDialog(app.get());
    QVERIFY(dialog);
    auto *input = dialog->findChild<QLineEdit *>("add-chat-input");
    auto *platform = dialog->findChild<QComboBox *>("add-chat-platform");
    auto *layout = dialog->findChild<QComboBox *>("add-chat-layout");
    auto *saved = dialog->findChild<QListWidget *>("add-chat-saved");
    QVERIFY(input && platform && layout && saved);
    QCOMPARE(input->placeholderText(), QString("Link or name · separate chats with space"));
    QCOMPARE(saved->count(), 2);
    saved->item(0)->setCheckState(Qt::Checked);
    saved->item(1)->setCheckState(Qt::Checked);
    platform->setCurrentIndex(1);
    layout->setCurrentIndex(3);
    input->setText("Alpha beta");
    QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(app->hub.sources.contains("twitch:alpha"));
    QVERIFY(app->hub.sources.contains("twitch:beta"));
    QVERIFY(app->hub.sources.contains("kick:savedkick"));
    QVERIFY(app->hub.sources.contains("0sfDth4lMLc"));
    QVERIFY(app->currentPane());
    QCOMPARE(app->currentPane()->sources.size(), 4);
    const int tabs = app->tabs->count();
    app->addChats();
    dialog = addDialog(app.get());
    QVERIFY(dialog);
    dialog->findChild<QListWidget *>("add-chat-saved")->item(0)->setCheckState(Qt::Checked);
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Open)->click();
    QCOMPARE(app->tabs->count(), tabs + 1);
    app->addChats();
    dialog = addDialog(app.get());
    QVERIFY(dialog);
    input = dialog->findChild<QLineEdit *>("add-chat-input");
    input->setText("abcdefghijk");
    QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(app->hub.sources.contains("twitch:abcdefghijk"));
}

void NativeTests::twitchAndYoutubeThumbnailsFromMetadata() {
    const QByteArray html = R"html(ytInitialPlayerResponse = {"videoDetails":{"videoId":"0sfDth4lMLc","author":"Owner","isLive":true,"thumbnail":{"thumbnails":[{"url":"https://i.ytimg.com/vi/0sfDth4lMLc/hqdefault.jpg","width":480}]}}}; ytInitialData = {};)html";
    app->net.transport = [html](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        if (url.path() == "/helix/streams") {
            QJsonObject row{{"user_login", "guicraft_"}, {"viewer_count", 10}, {"title", "Live"},
                {"thumbnail_url", "https://static-cdn.jtvnw.net/previews-ttv/live_user_test-{width}x{height}.jpg"}};
            done(QJsonDocument(QJsonObject{{"data", QJsonArray{row}}}).toJson(), {});
        } else if (url.host() == "www.youtube.com") done(html, {});
        else done({}, "fixture");
    };
    QSignalSpy header(twitch, &Source::headerChanged);
    QSignalSpy full(twitch, &Source::changed);
    app->hub.pollLive();
    QCOMPARE(str(twitch->header["thumbnail"]),
             QString("https://static-cdn.jtvnw.net/previews-ttv/live_user_test-320x180.jpg"));
    QCOMPARE(header.size(), 1);
    QCOMPARE(full.size(), 0);
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    app->hub.testMode = false;
    yt->refreshHeader();
    QCOMPARE(str(yt->header["thumbnail"]), QString("https://i.ytimg.com/vi/0sfDth4lMLc/hqdefault.jpg"));
    app->hub.testMode = true;
}

void NativeTests::youtubeChannelDiscoveryAndChooser() {
    QStringList paths;
    bool offline = false;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        if (url.host() != "www.youtube.com") { done({}, "fixture image"); return; }
        paths << url.path();
        if (url.path().endsWith("/streams")) done(livePage(offline ? 0 : 2), {});
        else done(R"(var ytInitialPlayerResponse = {"videoDetails":{"isLive":false}};)", {});
    };
    QJsonObject result;
    QString error;
    app->hub.savedStreams({{"channel_id", "somechannel"}}, [&](QJsonObject d, QString e) { result=d; error=e; });
    QVERIFY(error.isEmpty());
    QCOMPARE(paths.first(), QString("/@somechannel/streams"));
    QCOMPARE(result["streams"].toArray().size(), 2);
    QCOMPARE(str(result["streams"].toArray().first().toObject()["title"]), QString("Live 1"));

    QString notice;
    QTimer modalCloser;
    connect(&modalCloser, &QTimer::timeout, this, [&] {
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (!widget->isVisible()) continue;
            if (auto *choice = qobject_cast<QInputDialog *>(widget)) {
                if (auto *combo = choice->findChild<QComboBox *>()) combo->setCurrentIndex(1);
                choice->accept();
            } else if (auto *message = qobject_cast<QMessageBox *>(widget)) {
                notice = message->text();
                message->accept();
            }
        }
    });
    modalCloser.start(10);
    app->addChats();
    auto *dialog = addDialog(app.get());
    QVERIFY(dialog);
    auto *input = dialog->findChild<QLineEdit *>("add-chat-input");
    dialog->findChild<QComboBox *>("add-chat-platform")->setCurrentIndex(3);
    input->setText("somechannel");
    QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(app->hub.sources.contains("4t4AQ15yj1M"));

    offline = true;
    app->addChats();
    dialog = addDialog(app.get());
    QVERIFY(dialog);
    input = dialog->findChild<QLineEdit *>("add-chat-input");
    dialog->findChild<QComboBox *>("add-chat-platform")->setCurrentIndex(3);
    input->setText("offlinechannel");
    QTest::keyClick(input, Qt::Key_Return);
    QVERIFY(notice.contains("no current live broadcasts"));
    QVERIFY(dialog->isVisible());
    QVERIFY(paths.contains("/@offlinechannel/live"));
}

void NativeTests::kickLiveNotificationsAndCooldown() {
    auto alerts = app->cfg.feature("alerts");
    alerts["enabled"] = true;
    alerts["watch"] = QJsonArray{QJsonObject{{"platform", "kick"}, {"id", "watched"}}};
    app->cfg.setFeature("alerts", alerts);
    auto *source = app->hub.source("kick", "same");
    QSignalSpy sound(&app->hub, &Hub::alertPlayed);
    QSignalSpy notices(&app->hub, &Hub::notice);
    QSignalSpy fullRedraw(source, &Source::changed);
    QSignalSpy headerUpdates(source, &Source::headerChanged);
    QSet<QString> live;
    int calls = 0;
    bool rateLimited = false;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        ++calls;
        const auto name = url.path().section('/', -1);
        if (rateLimited) { done({}, "HTTP 429"); return; }
        QJsonObject data{{"id", 123}, {"user", QJsonObject{{"username", name}}}};
        if (live.contains(name)) data["livestream"] = QJsonObject{{"session_title", "Live"},
            {"thumbnail", QJsonObject{{"url", "https://stream.kick.com/thumbnail.jpg"}}}};
        done(QJsonDocument(data).toJson(), {});
    };
    app->hub.pollKickLive();
    QCOMPARE(calls, 2);
    QCOMPARE(sound.size(), 0);
    live = {"same", "watched"};
    app->hub.pollKickLive();
    QCOMPARE(sound.size(), 2);
    QCOMPARE(notices.size(), 2);
    QCOMPARE(fullRedraw.size(), 0);
    QVERIFY(headerUpdates.size() >= 2);
    QCOMPARE(str(source->header["thumbnail"]), QString("https://stream.kick.com/thumbnail.jpg"));
    app->hub.pollKickLive();
    QCOMPARE(sound.size(), 2);
    app->hub.liveChange("twitch", "same", "Same", false, true);
    app->hub.liveChange("twitch", "same", "Same", true, true);
    QCOMPARE(sound.size(), 3);
    rateLimited = true;
    app->hub.pollKickLive();
    const int afterLimit = calls;
    QVERIFY(app->hub.kickLiveRetryAt > QDateTime::currentMSecsSinceEpoch());
    app->hub.pollKickLive();
    QCOMPARE(calls, afterLimit);
}

void NativeTests::notificationWatchListSupportsKickAndTwitch() {
    app->showSettings("Notifications");
    auto *settings = app->settings.data();
    auto *openKick = settings->findChild<QCheckBox *>("notify-open-kick");
    auto *platform = settings->findChild<QComboBox *>("notification-watch-platform");
    auto *input = settings->findChild<QLineEdit *>("notification-watch-input");
    auto *list = settings->findChild<QListWidget *>("notification-watch-list");
    auto *add = settings->findChild<QPushButton *>("notification-watch-add");
    auto *apply = settings->findChild<QPushButton *>("notification-apply");
    QVERIFY(openKick && platform && input && list && add && apply);
    QVERIFY(openKick->isChecked());
    platform->setCurrentIndex(1); input->setText("GuiCraft"); add->click();
    platform->setCurrentIndex(0); input->setText("Alanzoka"); add->click();
    input->setText("@alanzoka"); add->click();
    QCOMPARE(list->count(), 2);
    apply->click();
    const auto watch = app->cfg.feature("alerts")["watch"].toArray();
    QCOMPARE(watch.size(), 2);
    QCOMPARE(str(watch[0].toObject()["platform"]), QString("kick"));
    QCOMPARE(str(watch[0].toObject()["id"]), QString("guicraft"));
    QCOMPARE(str(watch[1].toObject()["id"]), QString("alanzoka"));
}

void NativeTests::streamThumbnailHoverAndStaleImage() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    auto *kick = app->hub.source("kick", "somechannel");
    const QString ytUrl = "https://i.ytimg.com/vi/0sfDth4lMLc/hqdefault.jpg";
    const QString kickUrl = "https://images.kick.com/video_thumbnails/live.webp";
    yt->header = {{"live", true}, {"title", "YouTube title"}, {"thumbnail", ytUrl}};
    kick->header = {{"live", true}, {"title", "Kick title"}};
    QByteArray png;
    QImage image(320, 180, QImage::Format_ARGB32); image.fill(Qt::blue);
    QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
    BytesCallback delayed, kickMetadata;
    int kickRequests = 0;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        if (url.toString() == ytUrl) delayed = done;
        else if (url.host() == "api.kick.com") { ++kickRequests; kickMetadata = done; }
        else if (url.toString() == kickUrl) done(png, {});
        else done({}, "fixture image");
    };
    auto *pane = app->addWorkspace({yt, kick})->focusedPane.data();
    app->show(); QCoreApplication::processEvents();
    QVERIFY(pane->mixedTitles.size() >= 2);
    auto move = [&](QLabel *title) {
        QMouseEvent event(QEvent::MouseMove, QPointF(4, 4), QPointF(title->mapToGlobal(QPoint(4, 4))),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(title, &event);
    };
    move(pane->mixedTitles[0]);
    QVERIFY(delayed);
    QCOMPARE(pane->view->tooltipUrls.value(0), ytUrl);
    move(pane->mixedTitles[1]);
    QVERIFY(kickMetadata);
    QCOMPARE(kickRequests, 1);
    move(pane->mixedTitles[1]);
    QCOMPARE(kickRequests, 1);
    kickMetadata(QJsonDocument(QJsonObject{{"data", QJsonObject{{"livestream",
        QJsonObject{{"thumbnail_url", kickUrl}}}}}}).toJson(), {});
    QCOMPARE(pane->view->tooltipUrls.value(0), kickUrl);
    QCOMPARE(pane->view->tooltipImageSize, QSize(320, 180));
    QCOMPARE(pane->view->emoteTooltipName->text(), QString("Kick title"));
    QVERIFY(!pane->view->emotePreview->pixmap().isNull());
    delayed(png, {});
    QCOMPARE(pane->view->tooltipUrls.value(0), kickUrl);
    QCOMPARE(pane->view->emoteTooltipName->text(), QString("Kick title"));
    QVERIFY(Images::allowed(QUrl(ytUrl)));
    QVERIFY(Images::allowed(QUrl(kickUrl)));
    QVERIFY(!Images::allowed(QUrl("https://stream.kick.com.evil.test/fake")));
    kick->header["live"] = false;
    QVERIFY(!pane->view->showStreamTooltip(kick, QPoint(10, 10)));
}
