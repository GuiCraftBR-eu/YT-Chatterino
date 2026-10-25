#include "native_tests.hpp"

static Message sample210(const QString &source, QString id = "one") {
    Message message;
    message.source = source;
    message.id = id;
    message.name = "Tester";
    message.login = "tester";
    message.text = "A retained message for interaction checks.";
    message.stamp = QDateTime::currentDateTime();
    return message;
}

void NativeTests::legacyTwitchNames_data() {
    QTest::addColumn<QString>("raw");
    QTest::addColumn<QString>("expected");
    const QList<QPair<QString, QString>> values{{R"(\:-?\))", ":)"},
                                                {R"(\:-?\()", ":("},
                                                {R"(\:-?D)", ":D"},
                                                {R"(\&gt\;\()", ">("},
                                                {R"(\:-?[z|Z|\|])", ":Z"},
                                                {R"([oO](_|\.)[oO])", "O_o"},
                                                {R"(B-?\))", "B)"},
                                                {R"(\:-?(o|O))", ":O"},
                                                {R"(\&lt\;3)", "<3"},
                                                {R"(\:-?[\\/])", ":/"},
                                                {R"(\;-?\))", ";)"},
                                                {R"(\:-?(p|P))", ":P"},
                                                {R"(\;-?(p|P))", ";P"},
                                                {R"(R-?\))", "R)"},
                                                {R"(:\)", R"(:\)"},
                                                {"alanzOK", "alanzOK"},
                                                {"&lt;3", "<3"}};
    int row = 0;
    for (const auto &value : values) {
        QTest::newRow(qPrintable(QString::number(row++))) << value.first << value.second;
    }
}

void NativeTests::legacyTwitchNames() {
    QFETCH(QString, raw);
    QFETCH(QString, expected);
    QCOMPARE(emoteName(raw), expected);
}

void NativeTests::accountControlsReflectPermissions() {
    app->showSettings("Accounts");
    auto *settings = app->settings.data();
    auto *tw = settings->findChild<QPushButton *>("twitch-permissions");
    auto *yt = settings->findChild<QPushButton *>("youtube-permissions");
    QVERIFY(tw && yt);
    QVERIFY(!tw->isEnabled());
    QVERIFY(!yt->isEnabled());
    QCOMPARE(tw->text(), "All Twitch permissions enabled");
    QCOMPARE(yt->text(), "YouTube permissions enabled");
    app->accounts.twitch["scope"] = QJsonArray{"chat:read"};
    emit app->accounts.changed();
    QVERIFY(tw->isEnabled());
    QCOMPARE(tw->text(), "Enable missing Twitch permissions…");
    QVERIFY(
        settings->findChild<QLabel *>("twitch-permission-status")->text().contains("○ Subscriber emotes"));
    app->accounts.google["scope"] = "https://www.googleapis.com/auth/youtube.readonly";
    emit app->accounts.changed();
    QVERIFY(yt->isEnabled());
    QCOMPARE(yt->text(), "Enable YouTube sending / moderation…");
    app->accounts.twitch = {};
    app->accounts.google = {};
    emit app->accounts.changed();
    QCOMPARE(tw->text(), "Sign in to Twitch");
    QCOMPARE(yt->text(), "Sign in to YouTube");
    app->accounts.googleBusy = true;
    app->accounts.twitchRefreshing = true;
    emit app->accounts.changed();
    QVERIFY(!tw->isEnabled());
    QVERIFY(!yt->isEnabled());
    app->accounts.googleBusy = false;
    app->accounts.twitchRefreshing = false;
    authenticate();
    app->accounts.google["expires_at"] = 0;
    emit app->accounts.changed();
    QCOMPARE(yt->text(), "Reconnect YouTube…");
}

void NativeTests::googleRefreshUpdatesPermissionState() {
    const auto clientPath = app->cfg.home + "/desktop-client.json";
    QVERIFY(writeObject(clientPath, {{"installed", QJsonObject{{"client_id", "fixture-desktop"}}}}));
    auto features = app->cfg.features();
    features["google_client_file"] = clientPath;
    app->cfg.data["features"] = features;
    app->accounts.google["expires_at"] = 0;
    app->accounts.google["client_id"] = "fixture-desktop";
    app->accounts.google["refresh_token"] = "fixture-refresh";
    app->showSettings("Accounts");
    auto *button = app->settings->findChild<QPushButton *>("youtube-permissions");
    BytesCallback complete;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray body, QMap<QByteArray, QByteArray>,
                             BytesCallback done) {
        QCOMPARE(url.host(), "oauth2.googleapis.com");
        QCOMPARE(method, "POST");
        QVERIFY(body.contains("refresh_token"));
        complete = done;
    };
    int callbacks = 0;
    app->accounts.googleToken([&](QString token, QString error) {
        ++callbacks;
        QCOMPARE(token, "fresh-fixture");
        QVERIFY(error.isEmpty());
    });
    QVERIFY(app->accounts.googleRefreshing);
    QVERIFY(!button->isEnabled());
    complete("{\"access_token\":\"fresh-fixture\",\"expires_in\":3600,\"scope\":\"https://www.googleapis.com/"
             "auth/youtube.force-ssl\"}",
             {});
    QCOMPARE(callbacks, 1);
    QVERIFY(!app->accounts.googleRefreshing);
    QVERIFY(app->accounts.googleAllowsWriting());
    QCOMPARE(button->text(), "YouTube permissions enabled");
    QVERIFY(!button->isEnabled());
    QCOMPARE(str(app->accounts.google.value("refresh_token")), "fixture-refresh");
}

void NativeTests::twitchStartupWaitsForSavedAccounts() {
    app->accounts.twitch = {};
    app->accounts.loaded = false;
    app->hub.testMode = false;
    app->hub.twitch.channels[twitch->id] = twitch;
    app->hub.twitch.connectServer();
    twitch->refreshHeader();
    QVERIFY(twitch->history.isEmpty());
    QVERIFY(unexpectedRequests.isEmpty());
    app->accounts.loaded = true;
    app->hub.twitch.connectServer();
    QVERIFY(!twitch->history.isEmpty());
    QVERIFY(twitch->history.last().text.startsWith("Sign in to Twitch"));
}

void NativeTests::pickerCatalogsAndExistingAuthorization() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    app->hub.provider(nullptr, QJsonArray{QJsonObject{{"id", "g"}, {"name", "Shared"}}}, "twitch", "global");
    app->hub.provider(twitch,
                      QJsonArray{QJsonObject{{"id", "4"}, {"name", R"(\&gt\;\()"}},
                                 QJsonObject{{"id", "c"}, {"name", "Shared"}}},
                      "twitch", "channel");
    int emoteCalls = 0, authCalls = 0;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>,
                             BytesCallback done) {
        if (url.path().endsWith("/chat/emotes/user")) {
            ++emoteCalls;
            QCOMPARE(QUrlQuery(url).queryItemValue("broadcaster_id"), "12");
            const bool second = QUrlQuery(url).queryItemValue("after") == "next";
            done(QJsonDocument(
                     QJsonObject{{"data", QJsonArray{QJsonObject{{"id", second ? "s2" : "s1"},
                                                                 {"name", second ? "SubTwo" : "SubOne"},
                                                                 {"emote_type", "subscriptions"}},
                                                     QJsonObject{{"id", "1"},
                                                                 {"name", R"(\:-?\))"},
                                                                 {"emote_type", "smilies"}}}},
                                 {"pagination", second ? QJsonObject{} : QJsonObject{{"cursor", "next"}}}})
                     .toJson(),
                 {});
        } else {
            if (url.host() == "id.twitch.tv") {
                ++authCalls;
            }
            done({}, "fixture image unavailable");
        }
    };
    pane->openEmotes();
    auto *dialog = pane->findChildren<QDialog *>().last();
    auto *tabs = dialog->findChild<QTabBar *>("emote-tabs");
    auto *grid = dialog->findChild<QListWidget *>("emote-grid");
    auto *authorize = dialog->findChild<QPushButton *>("subscriber-emotes-action");
    QCOMPARE(grid->count(), 2);
    QCOMPARE(emoteCalls, 2);
    QCOMPARE(authorize->text(), "Refresh subscriber emotes");
    authorize->click();
    QCOMPARE(emoteCalls, 4);
    QCOMPARE(authCalls, 0);
    tabs->setCurrentIndex(1);
    QCOMPARE(grid->count(), 2);
    QStringList channel;
    for (int i = 0; i < grid->count(); ++i) {
        channel << grid->item(i)->text();
    }
    QVERIFY(channel.contains(">("));
    QVERIFY(!channel.contains("SubOne"));
    tabs->setCurrentIndex(2);
    QCOMPARE(grid->count(), 1);
    QCOMPARE(grid->item(0)->text(), "Shared");
    tabs->setCurrentIndex(1);
    dialog->findChild<QLineEdit *>("emote-search")->setText("SubOne");
    QCOMPARE(grid->count(), 0);
    tabs->setCurrentIndex(0);
    QCOMPARE(grid->count(), 1);
    QCOMPARE(grid->item(0)->data(Qt::UserRole).toString(), "SubOne");
    emit grid->itemActivated(grid->item(0));
    QCOMPARE(pane->input->text(), "SubOne ");
    QCOMPARE(authCalls, 0);
}

void NativeTests::pickerReadErrorCanRetry() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    bool fail = true;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>,
                             BytesCallback done) {
        if (url.path().endsWith("/chat/emotes/user")) {
            if (fail) {
                done({}, "Temporary emote request failure");
            } else {
                done("{\"data\":[{\"id\":\"ok\",\"name\":\"GoodEmote\",\"emote_type\":\"subscriptions\"}]}",
                     {});
            }
        } else {
            done({}, "fixture image unavailable");
        }
    };
    pane->openEmotes();
    auto *dialog = pane->findChildren<QDialog *>().last();
    auto *notice = dialog->findChild<QLabel *>("emote-status");
    auto *authorize = dialog->findChild<QPushButton *>("subscriber-emotes-action");
    QVERIFY(notice->text().contains("Temporary emote request failure"));
    QVERIFY(authorize->isEnabled());
    fail = false;
    authorize->click();
    QCOMPARE(dialog->findChild<QListWidget *>("emote-grid")->count(), 1);
    QVERIFY(notice->text().contains("Subscriber emotes enabled"));
}

void NativeTests::mouseReplyAndControlContextMenu() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    disconnect(pane, &Pane::messageMenu, app.get(), &MainWindow::messageMenu);
    auto *view = pane->view;
    view->setMessages({sample210(twitch->key)});
    app->show();
    QTest::qWait(35);
    QSignalSpy replies(view, &ChatView::replyRequested);
    QSignalSpy menus(view, &ChatView::messageMenuRequested);
    const QPoint pos(70, 10);
    QTest::mousePress(view->viewport(), Qt::RightButton, Qt::NoModifier, pos);
    QCOMPARE(replies.count(), 1);
    QCOMPARE(pane->reply.id, "one");
    QContextMenuEvent suppressed(QContextMenuEvent::Mouse, pos, view->viewport()->mapToGlobal(pos));
    QCoreApplication::sendEvent(view->viewport(), &suppressed);
    QCOMPARE(menus.count(), 0);
    QTest::mouseRelease(view->viewport(), Qt::RightButton, Qt::NoModifier, pos);
    QTest::mousePress(view->viewport(), Qt::RightButton, Qt::ControlModifier, pos);
    QContextMenuEvent controlled(QContextMenuEvent::Mouse, pos, view->viewport()->mapToGlobal(pos),
                                 Qt::ControlModifier);
    QCoreApplication::sendEvent(view->viewport(), &controlled);
    QCOMPARE(menus.count(), 1);
    QCOMPARE(replies.count(), 1);
    QTest::mouseRelease(view->viewport(), Qt::RightButton, Qt::ControlModifier, pos);
    QTest::mouseClick(view->viewport(), Qt::RightButton, Qt::NoModifier,
                      QPoint(50, view->viewport()->height() - 5));
    QCOMPARE(replies.count(), 1);
    auto deleted = sample210(twitch->key);
    deleted.deleted = true;
    QVERIFY(!view->canReply(deleted));
    deleted.deleted = false;
    deleted.system = true;
    QVERIFY(!view->canReply(deleted));
    QVERIFY(unexpectedRequests.isEmpty());
}

void NativeTests::autoscrollAndWheelSensitivity() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    auto *view = pane->view;
    QVector<Message> messages;
    for (int i = 0; i < 2000; ++i) {
        messages << sample210(twitch->key, QString::number(i));
    }
    view->setMessages(messages);
    app->show();
    QTest::qWait(35);
    auto *bar = view->verticalScrollBar();
    view->follow = false;
    bar->setValue(2000);
    const QPoint anchor(80, 100);
    QTest::mouseClick(view->viewport(), Qt::MiddleButton, Qt::NoModifier, anchor);
    QVERIFY(view->autoScrolling);
    QVERIFY(view->autoScrollTimer.isActive());
    view->autoScrollTimer.stop();
    const int start = bar->value();
    view->advanceAutoScroll(anchor + QPoint(0, 90), 0.05);
    QVERIFY(bar->value() > start);
    const int down = bar->value();
    view->advanceAutoScroll(anchor - QPoint(0, 90), 0.05);
    QVERIFY(bar->value() < down);
    const int middle = bar->value();
    view->advanceAutoScroll(anchor + QPoint(0, 3), 0.05);
    QCOMPARE(bar->value(), middle);
    QTest::keyClick(view, Qt::Key_Escape);
    QVERIFY(!view->autoScrolling);
    QTest::mouseClick(view->viewport(), Qt::MiddleButton, Qt::NoModifier, anchor);
    view->hide();
    QVERIFY(!view->autoScrolling);
    QVERIFY(!view->autoScrollTimer.isActive());
    view->show();
    auto wheel = [&](QPoint pixel, QPoint angle) {
        QWheelEvent event(QPointF(anchor), QPointF(view->viewport()->mapToGlobal(anchor)), pixel, angle,
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(view->viewport(), &event);
    };
    auto appearance = app->cfg.appearance();
    appearance["scroll_multiplier"] = 1.0;
    app->cfg.data["appearance"] = appearance;
    bar->setValue(2000);
    wheel({}, {0, -120});
    const int base = bar->value() - 2000;
    QVERIFY(base > 0);
    appearance["scroll_multiplier"] = 2.0;
    app->cfg.data["appearance"] = appearance;
    bar->setValue(2000);
    wheel({}, {0, -120});
    QVERIFY(qAbs((bar->value() - 2000) - base * 2) <= 2);
    bar->setValue(2000);
    wheel({0, -3}, {});
    QCOMPARE(bar->value(), 2003);
}

void NativeTests::organizedPaneMenuAndTabBorder() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    QMenu menu;
    app->populatePaneMenu(menu, pane);
    QStringList root;
    int submenus = 0;
    for (auto *action : menu.actions()) {
        if (!action->isSeparator()) {
            root << action->text();
        }
        if (action->menu()) {
            ++submenus;
        }
    }
    QVERIFY(root.size() <= 7);
    QCOMPARE(submenus, 3);
    QVERIFY(root.contains("Layout"));
    QVERIFY(root.contains("Filters and history"));
    QVERIFY(!root.contains("Split horizontally"));
    QVERIFY(root.last().startsWith("Close tab"));
    QVERIFY(!app->tabs->tabBar()->drawBase());
}

void NativeTests::cachedImageRepaintAndFrameChanges() {
    const QString url = "https://static-cdn.jtvnw.net/emoticons/v2/fixture/default/dark/3.0";
    auto entry = QSharedPointer<Images::Entry>::create();
    entry->image = QImage(12, 12, QImage::Format_ARGB32_Premultiplied);
    entry->image.fill(Qt::green);
    app->images.cache[url] = entry;
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    auto message = sample210(twitch->key);
    message.text = "emote";
    Span emote;
    emote.code = "emote";
    emote.end = 5;
    emote.urls = {url};
    message.emotes = {emote};
    pane->view->setMessages({message});
    app->show();
    QTest::qWait(35);
    auto document = pane->view->document(0);
    auto resource = [&] {
        return document->resource(QTextDocument::ImageResource, QUrl(url)).value<QImage>();
    };
    QCOMPARE(resource().pixelColor(0, 0), QColor(Qt::green));
    const auto cacheKey = resource().cacheKey();
    pane->view->viewport()->repaint();
    pane->view->viewport()->repaint();
    QCOMPARE(resource().cacheKey(), cacheKey);
    entry->image.fill(Qt::red);
    emit app->images.frame(url);
    QCOMPARE(resource().pixelColor(0, 0), QColor(Qt::red));
}
