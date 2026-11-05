#include "native_tests.hpp"

static void dragHeader(QWidget *handle, int pixels) {
    const QPoint start(2, 2), global = handle->mapToGlobal(start);
    QMouseEvent press(QEvent::MouseButtonPress, start, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &press);
    const QPoint end = start + QPoint(0, pixels), endGlobal = global + QPoint(0, pixels);
    QMouseEvent move(QEvent::MouseMove, end, endGlobal, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, end, endGlobal, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &release);
    QCoreApplication::processEvents();
}

void NativeTests::headerDragUsesVisibleHeight() {
    twitch->header["title"] = QString("A long title with enough words to wrap. ").repeated(5);
    app->resize(440, 650);
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    app->show(); pane->refresh(); QTest::qWait(50);
    pane->headerLines = 40; // A saved limit much larger than this particular title.
    pane->applyHeaderLineLimit(); QTest::qWait(50);
    const int lineHeight = QFontMetrics(pane->headerText->font()).lineSpacing();
    const int oldHeight = pane->headerText->height();
    const int oldFont = pane->headerText->font().pointSize();
    QVERIFY(oldHeight > 2 * lineHeight);
    dragHeader(pane->headerResize, -lineHeight);
    QVERIFY2(pane->headerText->height() < oldHeight, "One line of dragging should shrink the title immediately");
    QVERIFY(pane->headerLines < 40);
    QCOMPARE(pane->headerText->font().pointSize(), oldFont);
    const int limitedLines = pane->headerLines;
    dragHeader(pane->headerResize, lineHeight);
    QVERIFY(pane->headerLines > limitedLines);

    auto *other = app->hub.source("kick", "fixture");
    other->header = {{"channel", "Fixture"}, {"title", "Short"}};
    auto *mixed = app->addWorkspace({twitch, other})->focusedPane.data();
    mixed->headerLines = 40;
    mixed->applyHeaderLineLimit(); mixed->refresh(); QTest::qWait(50);
    const int mixedHeight = mixed->mixedTitles[0]->height();
    QVERIFY(mixedHeight > 2 * lineHeight);
    dragHeader(mixed->headerResize, -lineHeight);
    QVERIFY2(mixed->mixedTitles[0]->height() < mixedHeight, "One line of dragging should shrink the tallest mixed title");
    QVERIFY(mixed->headerLines < 40);
}

void NativeTests::compactChatStatusPresentation() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    app->resize(440, 500); app->show();
    twitch->updateChatModes({{"followers", true}, {"follow_minutes", 15}, {"slow", true}, {"slow_seconds", 10}});
    pane->filter["include_terms"] = QJsonArray{"hello"}; pane->refresh(); pane->phoneButton->show();
    QTest::qWait(50);
    auto *strip = pane->findChild<QWidget *>("chat-status-strip");
    auto *caption = pane->findChild<QLabel *>("chat-modes-caption");
    QVERIFY(strip && caption && caption->isVisible());
    QVERIFY(pane->chatModesLabel->text().contains("Followers only"));
    QVERIFY(pane->filterButton->isVisible() && pane->phoneButton->isVisible());
    QVERIFY(caption->geometry().right() < pane->chatModesLabel->geometry().left());
    QVERIFY(pane->chatModesLabel->geometry().right() < pane->filterButton->geometry().left());
    const auto background = strip->grab().toImage().pixelColor(1, 1);
    QCOMPARE(background.red(), background.green());
    QCOMPARE(background.green(), background.blue());
}
