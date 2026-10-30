#include "native_tests.hpp"

static void focusComposer(MainWindow *app, Composer *input) {
    app->show();
    app->activateWindow();
    input->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
}

void NativeTests::completionPopupSuggestsEmotesAndUsers() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    for (const auto &code : {QString("Kappa"), QString("KappaHD")}) {
        Span emote;
        emote.code = code;
        twitch->emotes[code] = emote;
    }
    Message first;
    first.name = "Falconer222";
    first.login = "falconer222";
    first.avatar = "https://static-cdn.jtvnw.net/avatar-a.png";
    Message second;
    second.name = "Falconer223";
    second.login = "falconer223";
    second.avatar = "https://static-cdn.jtvnw.net/avatar-b.png";
    twitch->history << first << second;
    for (const auto &url : {first.avatar, second.avatar}) {
        auto entry = QSharedPointer<Images::Entry>::create();
        entry->image = QImage(24, 24, QImage::Format_ARGB32);
        entry->image.fill(Qt::green);
        app->images.cache[url] = entry;
    }
    focusComposer(app.get(), pane->input);
    QTest::keyClicks(pane->input, "Kap");
    QTRY_VERIFY(pane->input->suggestionPopup && pane->input->suggestionPopup->isVisible());
    QCOMPARE(pane->input->text(), "Kap");
    QCOMPARE(pane->input->suggestionPopup->count(), 2);
    QCOMPARE(pane->input->suggestionPopup->item(0)->text(), "Kappa");
    QVERIFY(pane->input->hasFocus());
    QTest::keyClick(pane->input, Qt::Key_Tab);
    QCOMPARE(pane->input->text(), "Kappa ");
    QTest::keyClick(pane->input, Qt::Key_Tab);
    QCOMPARE(pane->input->text(), "KappaHD ");
    QTest::keyClick(pane->input, Qt::Key_Backtab);
    QCOMPARE(pane->input->text(), "Kappa ");
    pane->input->clear();
    QTest::keyClicks(pane->input, "@fal");
    QTRY_VERIFY(pane->input->suggestionPopup->isVisible());
    QCOMPARE(pane->input->suggestionPopup->count(), 2);
    QCOMPARE(pane->input->suggestionPopup->item(0)->text(), "@Falconer222");
    QVERIFY(!pane->input->suggestionPopup->item(0)->icon().isNull());
    QTest::keyClick(pane->input, Qt::Key_Down);
    QTest::keyClick(pane->input, Qt::Key_Tab);
    QCOMPARE(pane->input->text(), "@Falconer223 ");
    QTest::keyClick(pane->input, Qt::Key_Escape);
    QVERIFY(!pane->input->suggestionPopup->isVisible());
}

void NativeTests::completionPopupPreservesTextAndTarget() {
    auto *other = app->hub.source("youtube", "0sfDth4lMLc");
    Span emote;
    emote.code = "Kappa";
    twitch->emotes[emote.code] = emote;
    Message first;
    first.name = first.login = "twitchuser";
    twitch->history << first;
    Message second;
    second.name = second.login = "youtubeuser";
    other->history << second;
    auto *pane = app->addWorkspace({twitch, other})->focusedPane.data();
    focusComposer(app.get(), pane->input);
    pane->input->setText("hello Kaplo world");
    pane->input->setCursorPosition(9);
    QTRY_VERIFY(pane->input->suggestionPopup && pane->input->suggestionPopup->isVisible());
    QTest::keyClick(pane->input, Qt::Key_Tab);
    QCOMPARE(pane->input->text(), "hello Kappa world");
    pane->input->setText("@");
    pane->input->setCursorPosition(1);
    QTRY_VERIFY(pane->input->suggestionPopup->isVisible());
    QCOMPARE(pane->input->suggestionPopup->item(0)->text(), "@twitchuser");
    pane->target->setCurrentIndex(1);
    QTRY_VERIFY(pane->input->suggestionPopup->isVisible());
    QCOMPARE(pane->input->suggestionPopup->item(0)->text(), "@youtubeuser");
    pane->input->clearFocus();
    QVERIFY(!pane->input->suggestionPopup->isVisible());
}

void NativeTests::completionPopupBatchesTwitchAvatars() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    for (const auto &name : {QString("falconer1"), QString("falconer2")}) {
        Message m;
        m.name = m.login = name;
        twitch->history << m;
    }
    const QString avatar = "https://static-cdn.jtvnw.net/avatar-cached.png";
    auto entry = QSharedPointer<Images::Entry>::create();
    entry->image = QImage(24, 24, QImage::Format_ARGB32);
    entry->image.fill(Qt::blue);
    app->images.cache[avatar] = entry;
    int lookups = 0;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        QCOMPARE(url.path(), "/helix/users");
        QCOMPARE(QUrlQuery(url).allQueryItemValues("login").size(), 2);
        ++lookups;
        done(QJsonDocument(QJsonObject{{"data", QJsonArray{
                 QJsonObject{{"login", "falconer1"}, {"profile_image_url", avatar}},
                 QJsonObject{{"login", "falconer2"}, {"profile_image_url", avatar}}}}}).toJson(), {});
    };
    focusComposer(app.get(), pane->input);
    QTest::keyClicks(pane->input, "@fal");
    QTRY_VERIFY(pane->input->suggestionPopup && pane->input->suggestionPopup->isVisible());
    QTRY_COMPARE_WITH_TIMEOUT(lookups, 1, 2000);
    QTRY_COMPARE(pane->input->candidateVisuals.value("@falconer1").urls.value(0), avatar);
    pane->input->setText("@falcon");
    QTRY_VERIFY(pane->input->suggestionPopup->isVisible());
    QTest::qWait(500);
    QCOMPARE(lookups, 1);
}

void NativeTests::youtubeIllustratedGuideShowsSafeSteps() {
    app->showSettings("Tutorial");
    auto *settings = app->settings.data();
    auto *button = settings->built.value("Tutorial")->findChild<QPushButton *>("youtube-tutorial-guide-action");
    QVERIFY(button);
    button->click();
    auto *guide = settings->findChild<QDialog *>("youtube-illustrated-guide");
    QVERIFY(guide && guide->isVisible());
    auto *counter = guide->findChild<QLabel *>("youtube-guide-counter");
    auto *heading = guide->findChild<QLabel *>("youtube-guide-heading");
    auto *preview = guide->findChild<QLabel *>("youtube-guide-preview");
    auto *next = guide->findChild<QPushButton *>("youtube-guide-next");
    auto *previous = guide->findChild<QPushButton *>("youtube-guide-previous");
    auto *zoom = guide->findChild<QPushButton *>("youtube-guide-zoom");
    QVERIFY(counter && heading && preview && next && previous && zoom);
    QCOMPARE(counter->text(), "Step 1 of 7");
    QVERIFY(!previous->isEnabled());
    for (int step = 1; step < 7; ++step) {
        next->click();
        QCOMPARE(counter->text(), QString("Step %1 of 7").arg(step + 1));
        QVERIFY(!preview->pixmap().isNull());
        if (step == 3) QVERIFY(heading->text().contains("test user"));
    }
    QVERIFY(!next->isEnabled());
    zoom->click();
    QVERIFY(guide->findChild<QDialog *>("youtube-guide-full-image"));
    app->cfg.data["language"] = "pt-BR";
    emit app->cfg.languageChanged();
    QCOMPARE(counter->text(), "Etapa 7 de 7");
}
