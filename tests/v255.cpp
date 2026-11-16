#include "native_tests.hpp"

void NativeTests::compactInactiveTabSpace() {
    auto *first = app->addWorkspace({twitch}, "Same name");
    auto *second = app->addWorkspace({twitch}, "Same name");
    app->show(); QTest::qWait(50);
    auto *bar = app->tabs->tabBar();
    bar->setExpanding(false);
    QCoreApplication::processEvents();
    const int a = app->tabs->indexOf(first), b = app->tabs->indexOf(second);
    auto *inactive = bar->tabButton(a, QTabBar::RightSide);
    auto *active = bar->tabButton(b, QTabBar::RightSide);
    if (!inactive) inactive = bar->tabButton(a, QTabBar::LeftSide);
    if (!active) active = bar->tabButton(b, QTabBar::LeftSide);
    QVERIFY(inactive); QVERIFY(active);
    QCOMPARE(inactive->size(), QSize(0, 0));
    QVERIFY(active->width() > 0);
    QVERIFY(bar->tabRect(a).width() < bar->tabRect(b).width());
    app->tabs->setCurrentIndex(a);
    QCOMPARE(active->size(), QSize(0, 0));
    QVERIFY(inactive->width() > 0);
}

void NativeTests::popupKeepsSourceAlive() {
    auto *workspace = app->addWorkspace({twitch});
    app->openPopup(workspace->focusedPane, false);
    QCOMPARE(app->popupWindows.size(), 1);
    auto popup = app->popupWindows.first(); QVERIFY(popup);
    auto *pane = popup->findChild<Pane *>(); QVERIFY(pane);
    QCOMPARE(pane->sources.size(), 1); QCOMPARE(pane->sources.first().data(), twitch);
    app->closeTab(app->tabs->indexOf(workspace)); app->pruneSources();
    QVERIFY(app->hub.sources.contains(twitch->key));
    popup->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void NativeTests::pinnedLinksAreEscaped() {
    auto *workspace = app->addWorkspace({twitch});
    auto *pane = workspace->focusedPane.data();
    twitch->pinned = {{"name", "<viewer>"}, {"text", "Visit https://example.org/a?x=1&y=2. <script>"}};
    pane->refreshDecorations();
    auto *label = pane->decorations->findChild<QLabel *>("chat-decoration"); QVERIFY(label);
    QCOMPARE(label->textFormat(), Qt::RichText);
    QVERIFY(label->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse));
    QVERIFY(label->text().contains("href=\"https://example.org/a?x=1&amp;y=2\""));
    QVERIFY(label->text().contains("&lt;script&gt;"));
}

void NativeTests::moderationModeRespectsPermissions() {
    auto *workspace = app->addWorkspace({twitch});
    auto *pane = workspace->focusedPane.data();
    pane->refreshModeration(); QVERIFY(pane->moderationButton->isHidden());
    twitch->header["channel_id"] = "42"; emit twitch->changed();
    QVERIFY(!pane->moderationButton->isHidden());
    pane->moderationButton->setChecked(true);
    Message message; message.seq = 777; message.id = "id1"; message.source = twitch->key;
    message.name = "Viewer"; message.login = "viewer"; message.text = "Hello";
    pane->view->setMessages({message});
    auto doc = pane->view->document(0); QVERIFY(doc);
    QVERIFY(doc->toHtml().contains("ytmoderate:0"));
    auto *other = app->hub.source("youtube", "abcdefghijk");
    pane->setSources({twitch, other});
    message.source = other->key; message.seq = 779;
    pane->view->setMessages({message});
    QVERIFY(!pane->view->document(0)->toHtml().contains("ytmoderate:"));
    message.source = twitch->key;
    message.own = true; message.seq = 778; pane->view->setMessages({message});
    QVERIFY(!pane->view->document(0)->toHtml().contains("ytmoderate:"));
    app->accounts.twitch = {}; pane->refreshModeration();
    QVERIFY(pane->moderationButton->isHidden()); QVERIFY(!pane->moderationButton->isChecked());
}

void NativeTests::similarLiveSuggestionsRequireSelection() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    app->net.transport = [](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        if (url.host() == "kick.com" && url.path().endsWith("/guicraft_")) {
            done(R"({"livestream":{"session_title":"Live on Kick"}})", {}); return;
        }
        if (url.host() == "www.youtube.com" && url.path() == "/@guicraft_/streams") {
            done(R"(ytInitialData = {"contents":[{"videoRenderer":{"videoId":"abcdefghijk","title":{"simpleText":"Live on YouTube"},"badges":[{"style":"LIVE"}]}}]};)", {}); return;
        }
        done({}, "HTTP 404");
    };
    app->suggestSimilarLive(twitch, pane, true);
    auto *list = app->findChild<QListWidget *>("similar-live-channels"); QVERIFY(list);
    QCOMPARE(list->count(), 2);
    QCOMPARE(list->item(0)->checkState(), Qt::Unchecked);
    QCOMPARE(list->item(1)->checkState(), Qt::Unchecked);
    QCOMPARE(pane->sources.size(), 1);
    list->item(0)->setCheckState(Qt::Checked);
    auto *dialog = qobject_cast<QDialog *>(list->window()); QVERIFY(dialog);
    auto *buttons = dialog->findChild<QDialogButtonBox *>(); QVERIFY(buttons);
    buttons->button(QDialogButtonBox::Ok)->click();
    QCOMPARE(pane->sources.size(), 2);
}

void NativeTests::moderationButtonTargetsOriginalMessage() {
    twitch->header["channel_id"] = "42";
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    app->show(); QTest::qWait(30);
    pane->refreshModeration(); pane->moderationButton->setChecked(true);
    Message message; message.seq = 778; message.id = "message-id"; message.source = twitch->key;
    message.name = "Viewer"; message.login = "viewer"; message.uid = "99"; message.text = "Hello";
    pane->view->setMessages({message});
    int requests = 0;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray body,
                             QMap<QByteArray, QByteArray>, BytesCallback done) {
        QCOMPARE(url.path(), QString("/helix/moderation/bans"));
        QCOMPARE(method, QByteArray("POST"));
        auto data = QJsonDocument::fromJson(body).object()["data"].toObject();
        QCOMPARE(str(data["user_id"]), QString("99"));
        QCOMPARE(data["duration"].toInt(), 300);
        ++requests; done("{}", {});
    };
    auto doc = pane->view->document(0);
    doc->documentLayout()->documentSize();
    auto block = doc->begin(); auto line = block.layout()->lineAt(0);
    QVERIFY(line.isValid());
    const QPoint point(qRound(6 + line.cursorToX(1)), qRound(3 + line.height() / 2));
    QTest::mouseClick(pane->view->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QCOMPARE(requests, 1);
    app->accounts.twitch = {}; pane->runModeration(message, 0);
    QCOMPARE(requests, 1);
}
