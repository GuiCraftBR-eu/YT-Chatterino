#include "native_tests.hpp"
#include "vod.hpp"

void NativeTests::pinnedEmotesAndHypeTrain() {
    const QString first = "https://static-cdn.jtvnw.net/emoticons/v2/25/default/dark/3.0";
    const QString second = "https://cdn.betterttv.net/emote/abc/3x";
    auto cached = QSharedPointer<Images::Entry>::create();
    cached->image = QImage(20, 20, QImage::Format_ARGB32); cached->image.fill(Qt::green);
    app->images.cache[first] = cached;
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    app->startVodBridge(0); QVERIFY(app->vodBridge);
    const QJsonArray spans{
        QJsonObject{{"start", 6}, {"end", 11}, {"code", "Kappa"}, {"urls", QJsonArray{first}}},
        QJsonObject{{"start", 16}, {"end", 19}, {"code", "Pog"}, {"urls", QJsonArray{second}}},
        QJsonObject{{"start", 0}, {"end", 5}, {"code", "Hello"}, {"urls", QJsonArray{"https://evil.example/image.png"}}}};
    const QJsonObject chat{{"available", true},
        {"pin", QJsonObject{{"name", "Alice"}, {"text", "Hello Kappa and Pog!"}, {"native_emotes", spans}}},
        {"hype", QJsonObject{{"text", "Level 3 Hype Train"}, {"options", "50/100 · 2:15 left"}}}};
    const QString client = "a5b27e91-a65a-4e23-85d6-d298f4c1ac12";
    app->vodBridge->processLiveState(client, {{"tab", 7}, {"url", "https://www.twitch.tv/guicraft_"}, {"chat", chat}});
    QCOMPARE(twitch->browserPinned["native_emotes"].toArray().size(), 2);
    QCOMPARE(str(twitch->browserHype["text"]), QString("Level 3 Hype Train"));
    auto labels = pane->decorations->findChildren<QLabel *>("chat-decoration");
    QCOMPARE(labels.size(), 2);
    QCOMPARE(labels[0]->text().count("<img "), 1);
    QVERIFY(labels[0]->text().contains("Pog"));
    QVERIFY(labels[1]->text().contains("50/100"));
    auto other = QSharedPointer<Images::Entry>::create();
    other->image = QImage(20, 20, QImage::Format_ARGB32); other->image.fill(Qt::red);
    app->images.cache[second] = other;
    emit app->images.ready(second);
    QTRY_COMPARE(pane->decorations->findChildren<QLabel *>("chat-decoration").first()->text().count("<img "), 2);
    auto predictionChat = chat;
    predictionChat["prediction"] = QJsonObject{{"text", "Will it happen?"}, {"state", "open"},
        {"choices", QJsonArray{QJsonObject{{"id", "yes"}, {"label", "Yes"}}, QJsonObject{{"id", "no"}, {"label", "No"}}}}};
    app->vodBridge->processLiveState(client, {{"tab", 7}, {"url", "https://www.twitch.tv/guicraft_"}, {"chat", predictionChat}});
    QPointer<QSpinBox> points = pane->findChild<QSpinBox *>("twitch-prediction-points"); QVERIFY(points);
    points->setValue(125);
    auto updated = predictionChat;
    updated["hype"] = QJsonObject{{"text", "Level 3 Hype Train"}, {"options", "60/100 · 2:00 left"}};
    app->vodBridge->processLiveState(client, {{"tab", 7}, {"url", "https://www.twitch.tv/guicraft_"}, {"chat", updated}});
    QVERIFY(points); QCOMPARE(points->value(), 125);
    auto hypeLabel = pane->hypeLabels.value(twitch->key); QVERIFY(hypeLabel);
    QVERIFY(hypeLabel->text().contains("60/100"));
    twitch->setDecorations({{"text", "Another pin"}}, {});
    auto *rebuilt = pane->findChild<QSpinBox *>("twitch-prediction-points"); QVERIFY(rebuilt);
    QCOMPARE(rebuilt->value(), 125);
    auto changedPrediction = updated["prediction"].toObject();
    changedPrediction["text"] = "A different prediction?";
    updated["prediction"] = changedPrediction;
    app->vodBridge->processLiveState(client, {{"tab", 7}, {"url", "https://www.twitch.tv/guicraft_"}, {"chat", updated}});
    auto *newPoints = pane->findChild<QSpinBox *>("twitch-prediction-points"); QVERIFY(newPoints);
    QCOMPARE(newPoints->value(), 10);
    app->vodBridge->removeLivePlayer(client + ":7");
    QVERIFY(twitch->browserHype.isEmpty());
}

void NativeTests::recentActivityGroupsAndFilters() {
    if (!qEnvironmentVariableIsEmpty("YTCHATTERINO_QA_SCREENSHOTS")) configureApplication(*qApp);
    auto *kick = app->hub.source("kick", "testchannel");
    auto *youtube = app->hub.source("youtube", "abcdefghijk");
    auto *pane = app->addWorkspace({twitch, kick, youtube})->focusedPane.data();
    const QString prefix = "@display-name=Gifter;login=gifter;";
    auto bundle = parseIrc(prefix + "id=bundle;msg-id=submysterygift;msg-param-community-gift-id=group1;msg-param-mass-gift-count=2;system-msg=Gifter\\sis\\sgifting\\s2\\ssubs :tmi.twitch.tv USERNOTICE #guicraft_").message;
    auto first = parseIrc(prefix + "id=first;msg-id=subgift;msg-param-community-gift-id=group1;msg-param-recipient-display-name=One;system-msg=Gifter\\sgifted\\sa\\ssub :tmi.twitch.tv USERNOTICE #guicraft_").message;
    auto second = parseIrc(prefix + "id=second;msg-id=subgift;msg-param-community-gift-id=group1;msg-param-recipient-display-name=Two;system-msg=Gifter\\sgifted\\sa\\ssub :tmi.twitch.tv USERNOTICE #guicraft_").message;
    QCOMPARE(str(bundle.activity["type"]), QString("gift_bundle"));
    QCOMPARE(str(first.activity["group"]), QString("group1"));
    QCOMPARE(str(Message::fromJson(first.json()).activity["recipient"]), QString("One"));
    twitch->publish(bundle); twitch->publish(first); twitch->publish(second); twitch->publish(second);
    QCOMPARE(twitch->activityHistory.size(), 3);
    Message cheer; cheer.id="bits"; cheer.name="Cheerer"; cheer.text="Cheer100"; cheer.alertKind="bits";
    cheer.alertText="Cheered 100 Bits"; cheer.activity={{"type","support"}}; twitch->publish(cheer);
    kick->kickEvent("GiftedSubscriptionsEvent", {{"id", "kickgift"}, {"gifter_username", "Jane"},
                    {"gifted_usernames", QJsonArray{"Three", "Four"}}});
    const QJsonObject header{{"authorName", QJsonObject{{"simpleText", "YT Gifter"}}},
                             {"primaryText", QJsonObject{{"simpleText", "Gifted 1 membership"}}}};
    auto ytBundle = parseYouTube({{"id", "ytgift"}, {"header", QJsonObject{{"liveChatSponsorshipsHeaderRenderer", header}}}},
                                 "liveChatSponsorshipsGiftPurchaseAnnouncementRenderer");
    auto ytRecipient = parseYouTube({{"id", "ytchild"}, {"associatedMembershipGiftingMessageId", "ytgift"},
                                     {"authorName", QJsonObject{{"simpleText", "Five"}}},
                                     {"message", QJsonObject{{"simpleText", "Received a membership"}}}},
                                    "liveChatSponsorshipsGiftRedemptionAnnouncementRenderer");
    youtube->publish(ytBundle); youtube->publish(ytRecipient);
    QMenu menu; app->populatePaneMenu(menu, pane);
    auto *action = menu.findChild<QAction *>("recent-activity"); QVERIFY(action); action->trigger();
    auto *dialog = app->findChild<QDialog *>("recent-activity-dialog"); QVERIFY(dialog);
    auto *tree = dialog->findChild<QTreeWidget *>("recent-activity-tree"); QVERIFY(tree);
    QCOMPARE(tree->topLevelItemCount(), 4);
    const QString screenshots = qEnvironmentVariable("YTCHATTERINO_QA_SCREENSHOTS");
    if (!screenshots.isEmpty()) {
        QTest::qWait(50); dialog->grab().save(QDir(screenshots).filePath("activity.png"));
        menu.popup(QPoint(20, 20)); QTest::qWait(50);
        menu.grab().save(QDir(screenshots).filePath("channel-menu.png")); menu.hide();
    }
    QHash<QString, int> children;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        children[tree->topLevelItem(i)->text(0)] = tree->topLevelItem(i)->childCount();
    bool twitchGroup=false, kickGroup=false, ytGroup=false;
    for (auto it=children.cbegin(); it!=children.cend(); ++it) {
        if (it.key().contains("Gifter") && !it.key().contains("YT")) twitchGroup = it.value()==2;
        if (it.key().contains("Jane")) kickGroup = it.value()==2;
        if (it.key().contains("YT Gifter")) ytGroup = it.value()==1;
    }
    QVERIFY(twitchGroup); QVERIFY(kickGroup); QVERIFY(ytGroup);
    QString expandedKey;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto *item = tree->topLevelItem(i);
        if (item->text(0).startsWith("Gifter")) { item->setExpanded(true); expandedKey = item->data(0, Qt::UserRole).toString(); }
    }
    QVERIFY(!expandedKey.isEmpty());
    Message extra; extra.id="bits2"; extra.name="Another"; extra.text="Cheer10";
    extra.alertKind="bits"; extra.alertText="Cheered 10 Bits"; extra.activity={{"type","support"}};
    twitch->publish(extra); QTRY_COMPARE(tree->topLevelItemCount(), 5);
    bool stillExpanded = false;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->data(0, Qt::UserRole).toString() == expandedKey) stillExpanded = tree->topLevelItem(i)->isExpanded();
    QVERIFY(stillExpanded);
    auto *filter=dialog->findChild<QComboBox *>("activity-filter"); QVERIFY(filter);
    filter->setCurrentIndex(filter->findData("support")); QCOMPARE(tree->topLevelItemCount(), 2);
    QVERIFY(tree->topLevelItem(1)->text(0).contains("100 Bits"));
    dialog->close();
}
