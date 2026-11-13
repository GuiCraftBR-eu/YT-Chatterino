#include "native_tests.hpp"

void NativeTests::closeButtonOnlyOnActiveTab() {
    auto *youtube = app->hub.source("youtube", "abcdefghijk");
    auto *first = app->addWorkspace({twitch});
    auto *second = app->addWorkspace({youtube});
    app->show(); QTest::qWait(35);
    auto *bar = app->tabs->tabBar();
    const auto closeButton = [bar](int i) {
        auto *button = bar->tabButton(i, QTabBar::RightSide);
        if (!button) button = bar->tabButton(i, QTabBar::LeftSide);
        return button;
    };
    int a = app->tabs->indexOf(first), b = app->tabs->indexOf(second);
    QVERIFY(a >= 0 && b >= 0); QVERIFY(closeButton(a)); QVERIFY(closeButton(b));
    QVERIFY(closeButton(a)->isHidden()); QVERIFY(!closeButton(b)->isHidden());
    app->tabs->setCurrentIndex(a);
    QVERIFY(!closeButton(a)->isHidden()); QVERIFY(closeButton(b)->isHidden());
    bar->moveTab(a, b);
    a = app->tabs->indexOf(first); b = app->tabs->indexOf(second);
    QVERIFY(!closeButton(a)->isHidden()); QVERIFY(closeButton(b)->isHidden());
    app->closeTab(a); QCoreApplication::processEvents();
    b = app->tabs->indexOf(second); QVERIFY(b >= 0); QVERIFY(!closeButton(b)->isHidden());
}

void NativeTests::obsDockUsesOneMixedChatAndSeparateAccounts() {
    const auto profile = temp.path() + "/obs-test";
    auto obs = std::make_unique<MainWindow>(profile, true, true);
    QVERIFY(obs->obsDockMode); QVERIFY(obs->obsPane); QCOMPARE(obs->tabs->count(), 1);
    QVERIFY(obs->addButton->isHidden()); QVERIFY(!obs->tabs->tabsClosable());
    QVERIFY(obs->findChild<QToolButton *>("obs-refresh-channels"));
    QVERIFY(obs->accounts.vault.target("twitch") != app->accounts.vault.target("twitch"));
    obs->accounts.twitch = {{"access_token", "fixture"}, {"login", "MyTwitch"}};
    obs->accounts.kick = {{"access_token", "fixture"}, {"name", "MyKick"}};
    const QString channel = "UCabcdefghijklmnopqrstuv";
    obs->accounts.google = {{"access_token", "fixture"}, {"channel_id", channel}};
    obs->obsYoutubeChannel = channel; obs->obsYoutubeVideo = "abcdefghijk";
    obs->syncObsAccounts();
    QCOMPARE(obs->obsPane->sources.size(), 3);
    QCOMPARE(obs->obsPane->sources[0]->key, QString("twitch:mytwitch"));
    QCOMPARE(obs->obsPane->sources[1]->key, QString("abcdefghijk"));
    QCOMPARE(obs->obsPane->sources[2]->key, QString("kick:mykick"));
    QVERIFY(obs->obsPane->view->mixed);
    if (qEnvironmentVariableIsSet("YTCHATTERINO_OBS_MODE_SCREENSHOT")) {
        const QStringList names{"Twitch viewer", "YouTube viewer", "Kick viewer"};
        for (int i = 0; i < 3; ++i) {
            Message m; m.id = QString::number(i + 1); m.name = names[i]; m.login = names[i];
            m.text = "A message in the mixed OBS chat"; m.stamp = QDateTime::currentDateTime();
            obs->obsPane->sources[i]->publish(m);
        }
        obs->resize(400, 650); obs->show(); QTest::qWait(50);
        QVERIFY(obs->grab().save(qEnvironmentVariable("YTCHATTERINO_OBS_MODE_SCREENSHOT")));
    }
    obs->openChats({"twitch:someoneelse"}); obs->closeTab(0);
    QCOMPARE(obs->tabs->count(), 1); QCOMPARE(obs->obsPane->sources.size(), 3);
    obs->accounts.kick = {}; obs->syncObsAccounts();
    QCOMPARE(obs->obsPane->sources.size(), 2);
    QCOMPARE(obs->obsPane->sources[1]->key, QString("abcdefghijk"));
    QMenu menu; obs->populatePaneMenu(menu, obs->obsPane);
    for (auto *action : menu.actions())
        if (action->text().contains("Close tab") || action->text() == "Layout") QVERIFY(!action->isVisible());
}

void NativeTests::obsDockRefreshesYoutubeAndRejectsStaleResults() {
    auto obs = std::make_unique<MainWindow>(temp.path() + "/obs-youtube", true, true);
    const QString firstChannel = "UCabcdefghijklmnopqrstuv", secondChannel = "UCzyxwvutsrqponmlkjihgfe";
    obs->accounts.google = {{"access_token", "fixture"}, {"channel_id", firstChannel}};
    QList<BytesCallback> pending;
    obs->net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray,QByteArray>, BytesCallback done) {
        pending << std::move(done);
    };
    obs->syncObsAccounts(); QCOMPARE(pending.size(), 1);
    obs->accounts.google["channel_id"] = secondChannel; obs->syncObsAccounts(); QCOMPARE(pending.size(), 2);
    const QByteArray live = R"(ytInitialData = {"contents":[{"videoRenderer":{"videoId":"abcdefghijk","title":{"simpleText":"Live"},"badges":[{"style":"LIVE"}]}}]};)";
    pending[0](live, {}); QVERIFY(obs->obsYoutubeVideo.isEmpty());
    pending[1](live, {}); QCOMPARE(obs->obsYoutubeVideo, QString("abcdefghijk"));
    QCOMPARE(obs->obsPane->sources.size(), 1); QCOMPARE(obs->obsPane->sources[0]->platform, QString("youtube"));
    pending.clear(); obs->refreshObsYoutube(); QCOMPARE(pending.size(), 1);
    pending[0]({}, "HTTP 429"); QCOMPARE(obs->obsYoutubeVideo, QString("abcdefghijk"));
    QVERIFY(obs->obsYoutubeTimer.isActive());
    obs->accounts.google = {}; obs->syncObsAccounts(); QVERIFY(obs->obsYoutubeVideo.isEmpty());
    QVERIFY(obs->obsPane->sources.isEmpty()); QVERIFY(!obs->obsYoutubeTimer.isActive());
}
