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
    twitch->header["channel_id"] = "42"; pane->refreshModeration();
    QVERIFY(!pane->moderationButton->isHidden());
    pane->moderationButton->setChecked(true);
    Message message; message.seq = 777; message.id = "id1"; message.source = twitch->key;
    message.name = "Viewer"; message.login = "viewer"; message.text = "Hello";
    pane->view->setMessages({message});
    auto doc = pane->view->document(0); QVERIFY(doc);
    QVERIFY(doc->toHtml().contains("ytmoderate:0"));
    message.own = true; message.seq = 778; pane->view->setMessages({message});
    QVERIFY(!pane->view->document(0)->toHtml().contains("ytmoderate:"));
    app->accounts.twitch = {}; pane->refreshModeration();
    QVERIFY(pane->moderationButton->isHidden()); QVERIFY(!pane->moderationButton->isChecked());
}
