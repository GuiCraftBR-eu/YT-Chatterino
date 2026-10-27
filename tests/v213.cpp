#include "native_tests.hpp"

void NativeTests::emoteHoverRespondsToMouseMovement() {
    ChatView view(&app->hub);
    view.resize(480, 200);
    view.show();
    const QString url = "https://cdn.betterttv.net/emote/fixture/3x";
    auto entry = QSharedPointer<Images::Entry>::create();
    entry->image = QImage(24, 24, QImage::Format_ARGB32);
    entry->image.fill(Qt::green);
    app->images.cache[url] = entry;
    Message m;
    m.seq = 9313;
    m.name = "Tester";
    m.source = twitch->key;
    m.text = "Emote";
    m.emotes = {{0, 5, "Emote", {url}, false}};
    view.setMessages({m});
    auto doc = view.document(0);
    (void)doc->size();
    auto info = view.details.value(m.seq).emoteImages;
    QCOMPARE(info.size(), 1);
    int pos = info.constBegin().key();
    auto block = doc->findBlock(pos);
    auto line = block.layout()->lineForTextPosition(pos - block.position());
    auto bounds = doc->documentLayout()->blockBoundingRect(block);
    QPoint point(qRound(6 + bounds.left() + (line.cursorToX(pos - block.position()) +
                                             line.cursorToX(pos - block.position() + 1)) / 2),
                 qRound(3 + bounds.top() + line.y() + line.height() / 2));
    QTest::mouseMove(view.viewport(), point);
    QTRY_VERIFY_WITH_TIMEOUT(view.emoteTooltip && view.emoteTooltip->isVisible(), 2000);
    QCOMPARE(view.emoteTooltipName->text(), "Emote");
    QVERIFY(!view.emotePreview->pixmap().isNull());
    QTest::mouseMove(view.viewport(), QPoint(470, 180));
    QTRY_VERIFY_WITH_TIMEOUT(!view.emoteTooltip->isVisible(), 1000);
    QTest::qWait(500);
    QVERIFY(!view.emoteTooltip->isVisible());
    QVERIFY(view.messageText(0).contains("Emote"));
}

void NativeTests::youtubeVerifiedBadgeDrawsBlueCheck() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    auto data = QJsonDocument::fromJson(
        R"json({"id":"verified-message","authorName":{"simpleText":"Channel"},"authorExternalChannelId":"UCtest","message":{"runs":[{"text":"hello"}]},"authorBadges":[{"liveChatAuthorBadgeRenderer":{"icon":{"iconType":"VERIFIED"},"tooltip":"Verified"}}]})json").object();
    auto m = parseYouTube(data, "liveChatTextMessageRenderer");
    QCOMPARE(m.badges.size(), 1);
    QCOMPARE(m.badges.first().role, "verified");
    m.source = yt->key;
    m.seq = 9314;
    ChatView view(&app->hub);
    view.resize(400, 150);
    view.setMessages({m});
    auto doc = view.document(0);
    auto badge = doc->resource(QTextDocument::ImageResource, QUrl("badge:verified")).value<QImage>();
    QVERIFY(!badge.isNull());
    QVERIFY(badge.pixelColor(badge.width() / 2, badge.height() / 5).blue() > 150);
    QVERIFY(!doc->toPlainText().contains("Verified"));
    QVERIFY(view.messageText(0).contains("verified"));
}

void NativeTests::settingsGroupsAndLanguagePersist() {
    app->showSettings("Display");
    auto *settings = app->settings.data();
    QCOMPARE(settings->navigation->topLevelItemCount(), 3);
    QCOMPARE(settings->navigation->topLevelItem(0)->text(0), "Connections");
    QCOMPARE(settings->navigation->topLevelItem(1)->text(0), "Appearance");
    QCOMPARE(settings->navigation->topLevelItem(2)->text(0), "Application");
    QCOMPARE(settings->navigation->currentItem()->data(0, Qt::UserRole).toString(), "Display");
    app->cfg.data["language"] = "pt-BR";
    emit app->cfg.languageChanged();
    settings->rebuildLanguage();
    QCOMPARE(settings->navigation->topLevelItem(0)->text(0), "Conexões");
    QCOMPARE(settings->navigation->topLevelItem(2)->text(0), "Aplicativo");
    QCOMPARE(settings->currentPage, "Display");
    QVERIFY(settings->search->isClearButtonEnabled());
}
