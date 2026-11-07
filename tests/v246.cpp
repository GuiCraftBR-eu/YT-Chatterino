#include "native_tests.hpp"

void NativeTests::alertIconsMatchEvents() {
    const auto irc = [](QString tags) {
        return parseIrc("@id=x;login=alice;display-name=Alice;system-msg=Alice\\ssubscribed!;" + tags +
                        " :tmi.twitch.tv USERNOTICE #guicraft_ :hello").message;
    };
    QCOMPARE(irc("msg-id=sub;msg-param-sub-plan=Prime").alertKind, QString("prime"));
    QCOMPARE(irc("msg-id=resub;msg-param-sub-plan=1000").alertKind, QString("subscription"));
    QCOMPARE(irc("msg-id=subgift;msg-param-sub-plan=1000").alertKind, QString("gift"));
    QCOMPARE(irc("msg-id=raid").alertKind, QString("other"));
    QCOMPARE(parseIrc("@bits=50 :bob!b@b PRIVMSG #guicraft_ :Cheer50").message.alertKind, QString("bits"));
    QCOMPARE(parseIrc("@custom-reward-id=abc :bob!b@b PRIVMSG #guicraft_ :hello").message.alertKind, QString("reward"));
    QCOMPARE(parseYouTube({}, "liveChatPaidMessageRenderer").alertKind, QString("money"));
    QCOMPARE(parseYouTube({}, "liveChatMembershipItemRenderer").alertKind, QString("subscription"));
    QCOMPARE(parseYouTube({}, "liveChatSponsorshipsGiftPurchaseAnnouncementRenderer").alertKind, QString("gift"));
    auto *kick = app->hub.source("kick", "fixture");
    kick->kickEvent("SubscriptionEvent", {{"id", "sub"}, {"username", "A"}});
    kick->kickEvent("GiftedSubscriptionsEvent", {{"id", "gift"}, {"gifter_username", "B"}});
    kick->kickEvent("RewardRedeemedEvent", {{"id", "reward"}, {"username", "C"}, {"reward_title", "Hydrate"}});
    QCOMPARE(kick->history[0].alertKind, QString("subscription"));
    QCOMPARE(kick->history[1].alertKind, QString("gift"));
    QCOMPARE(kick->history[2].alertKind, QString("reward"));
    QCOMPARE(Message::fromJson(kick->history[1].json()).alertKind, QString("gift"));
    const QStringList kinds{"prime", "subscription", "reward", "money", "gift", "bits", "other"};
    for (const auto &kind : kinds) { QVERIFY(!alertIcon(kind).isNull()); QVERIFY(alertIcon(kind).pixelColor(0, 0).alpha() == 0); }
    QVERIFY(alertIcon("prime") != alertIcon("subscription"));
    QVERIFY(alertIcon("reward") != alertIcon("money"));
    QVERIFY(alertIcon("gift") != alertIcon("other"));
}

void NativeTests::hoverPausePreservesMessages() {
    app->cfg.data["pause_on_hover"] = true;
    ChatView view(&app->hub); view.resize(400, 150); view.show();
    for (int i = 0; i < 30; ++i) { Message m; m.seq=i+1; m.name="A"; m.text="message"; view.append(m); }
    QCoreApplication::processEvents(); view.jumpLatest();
    QEvent enter(QEvent::Enter); QApplication::sendEvent(view.viewport(), &enter);
    QVERIFY(view.hoverSuspended); QVERIFY(!view.follow); const int top=view.verticalScrollBar()->value();
    for (int i = 0; i < 20; ++i) { Message m; m.seq=100+i; m.name="B"; m.text="incoming"; view.append(m); }
    QCOMPARE(view.verticalScrollBar()->value(), top); QCOMPARE(view.messages.size(), 50);
    QEvent leave(QEvent::Leave); QApplication::sendEvent(view.viewport(), &leave);
    QVERIFY(!view.hoverSuspended); QVERIFY(view.follow);
    QCOMPARE(view.verticalScrollBar()->value(), view.verticalScrollBar()->maximum());
    view.follow=false; view.verticalScrollBar()->setValue(0);
    QApplication::sendEvent(view.viewport(), &enter); QApplication::sendEvent(view.viewport(), &leave);
    QVERIFY(!view.follow); QCOMPARE(view.verticalScrollBar()->value(), 0);
    view.jumpLatest(); QApplication::sendEvent(view.viewport(), &enter);
    QTest::keyClick(&view, Qt::Key_Home);
    QApplication::sendEvent(view.viewport(), &leave);
    QVERIFY(!view.follow); QCOMPARE(view.verticalScrollBar()->value(), 0);
    view.jumpLatest(); QApplication::sendEvent(view.viewport(), &enter);
    app->showSettings("Messages"); auto *box=app->settings->findChild<QCheckBox *>("pause-on-hover"); QVERIFY(box);
    box->setChecked(false); app->settings->applyButton->click(); QVERIFY(!app->cfg.data["pause_on_hover"].toBool());
    QVERIFY(!view.hoverSuspended); QVERIFY(view.follow);
}

void NativeTests::previewStaysVisibleOnIncomingMessages() {
    const QString url="https://cdn.betterttv.net/emote/fixture/3x";
    auto entry=QSharedPointer<Images::Entry>::create(); entry->image=QImage(28,28,QImage::Format_ARGB32); entry->image.fill(Qt::green); app->images.cache[url]=entry;
    ChatView view(&app->hub); view.resize(450,150); view.show();
    for(int i=0;i<20;++i) { Message m; m.seq=i+1; m.name="Viewer"; m.text="plain"; view.append(m); }
    Message target; target.seq=50; target.name="Viewer"; target.source=twitch->key; target.text="Wave";
    target.emotes={{0,4,"Wave",{url},false}}; view.append(target); QCoreApplication::processEvents(); view.jumpLatest();
    auto doc=view.document(view.messages.size()-1); (void)doc->size();
    const auto pos=view.details.value(50).emoteImages.constBegin().key();
    const auto block=doc->findBlock(pos); const auto line=block.layout()->lineForTextPosition(pos-block.position());
    const auto bounds=doc->documentLayout()->blockBoundingRect(block);
    const auto x=(line.cursorToX(pos-block.position())+line.cursorToX(pos-block.position()+1))/2;
    const QPoint point(qRound(6+bounds.left()+x), qRound(3+bounds.top()+line.y()+line.height()/2+view.offsets[view.messages.size()-1]-view.verticalScrollBar()->value()));
    QVERIFY(view.showEmoteTooltip(point,view.viewport()->mapToGlobal(point))); QVERIFY(view.hoverSuspended);
    const int old=view.verticalScrollBar()->value();
    for(int i=0;i<10;++i) { Message m; m.seq=100+i; m.name="Other"; m.text="new"; view.append(m); }
    QCOMPARE(view.verticalScrollBar()->value(),old); QVERIFY(view.emoteTooltip->isVisible());
    view.messageCap = view.messages.size();
    for(int i=0;i<5;++i) { Message m; m.seq=200+i; m.name="Other"; m.text="new"; view.append(m); }
    QVERIFY(view.emoteTooltip->isVisible()); QCOMPARE(view.tooltipMessage, qint64(50));
    view.hideEmoteTooltip(); QVERIFY(view.follow);
}
