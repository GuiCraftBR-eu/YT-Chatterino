#include "native_tests.hpp"
#include "phone.hpp"

void NativeTests::hoverHighlightedAlertImages() {
    const QString url = "https://cdn.betterttv.net/emote/fixture/3x";
    auto image = QSharedPointer<Images::Entry>::create();
    image->image = QImage(28, 28, QImage::Format_ARGB32); image->image.fill(Qt::green);
    app->images.cache[url] = image;
    auto parsed = parseIrc("@id=one;msg-id=highlighted-message;display-name=Viewer :viewer!v@v PRIVMSG #guicraft_ :hello Wave");
    auto message = parsed.message; message.seq = 9320; message.source = twitch->key;
    message.emotes = {{6, 10, "Wave", {url}, false}};
    ChatView view(&app->hub); view.resize(450, 240); view.show(); view.setMessages({message});
    QCoreApplication::processEvents(); view.viewport()->repaint();
    auto doc = view.document(0); (void)doc->size();
    const auto details = view.details.value(message.seq);
    QCOMPARE(details.emoteImages.size(), 1);
    const auto pos = details.emoteImages.constBegin().key();
    const auto block = doc->findBlock(pos); auto line = block.layout()->lineForTextPosition(pos - block.position());
    const auto bounds = doc->documentLayout()->blockBoundingRect(block);
    const auto x = (line.cursorToX(pos-block.position())+line.cursorToX(pos-block.position()+1))/2;
    const QPoint point(qRound(6+bounds.left()+x), qRound(3+bounds.top()+line.y()+line.height()/2));
    QVERIFY2(view.showEmoteTooltip(point, view.viewport()->mapToGlobal(point)), qPrintable(QString("Tooltip failed at %1,%2").arg(point.x()).arg(point.y())));
    QCOMPARE(view.emoteTooltipName->text(), QString("Wave"));
    QVERIFY(!view.emotePreview->pixmap().isNull());
    view.hideEmoteTooltip(); view.viewport()->repaint();
    const auto screenshot = view.viewport()->grab().toImage();
    int pixels = 0, hits = 0;
    for (int y = 0; y < screenshot.height(); y += 2) for (int x = 0; x < screenshot.width(); x += 2) {
        const QColor c = screenshot.pixelColor(x, y);
        if (c.green() > 200 && c.red() < 40 && c.blue() < 40) {
            ++pixels; if (view.showEmoteTooltip(QPoint(x,y), view.viewport()->mapToGlobal(QPoint(x,y)))) ++hits;
        }
    }
    qInfo() << "HOVER_VISIBLE_PIXELS" << hits << "/" << pixels;
    QVERIFY(pixels > 10); QVERIFY(hits * 100 >= pixels * 90);
    const auto crown = doc->resource(QTextDocument::ImageResource, QUrl("icon:alert-other")).value<QImage>();
    QVERIFY(!crown.isNull());
    QVERIFY(crown.pixelColor(0, 0).alpha() < 255);
}

void NativeTests::youtubeVideoLinksSkipChannelSearch() {
    int discovery = 0;
    app->net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray,QByteArray>, BytesCallback done) {
        ++discovery; done({}, "Unexpected channel lookup");
    };
    const QList<QPair<int,QString>> cases{{0,"https://youtu.be/0sfDth4lMLc?t=2"},
        {1,"https://www.youtube.com/live/4t4AQ15yj1M?feature=share"},
        {2,"https://www.youtube.com/watch?v=aqz-KE-bpKQ"},
        {3,"https://www.youtube.com/watch?v=21X5lGlDOfg&feature=share"}};
    for (const auto &item : cases) {
        app->addChats(); QDialog *dialog = nullptr;
        for (auto *d : app->findChildren<QDialog *>()) if (d->isVisible() && d->windowTitle() == "Add chats") dialog = d;
        QVERIFY(dialog);
        dialog->findChild<QComboBox *>("add-chat-platform")->setCurrentIndex(item.first);
        auto *field = dialog->findChild<QLineEdit *>("add-chat-input"); field->setText(item.second);
        QTest::keyClick(field, Qt::Key_Return);
        QVERIFY(app->hub.sources.contains(videoId(item.second)));
    }
    QCOMPARE(discovery, 0);
    BytesCallback pending;
    app->net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray,QByteArray>, BytesCallback done) {
        pending = std::move(done);
    };
    app->addChats(); QDialog *dialog = nullptr;
    for (auto *d : app->findChildren<QDialog *>()) if (d->isVisible() && d->windowTitle() == "Add chats") dialog = d;
    QVERIFY(dialog);
    dialog->findChild<QComboBox *>("add-chat-platform")->setCurrentIndex(3);
    auto *field=dialog->findChild<QLineEdit *>("add-chat-input"); field->setText("LofiGirl");
    QTest::keyClick(field, Qt::Key_Return);
    QVERIFY(bool(pending));
    QVERIFY(dialog->findChild<QLabel *>("add-chat-status")->isVisible());
    QVERIFY(!dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Open)->isEnabled());
    dialog->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    pending({}, "Cancelled lookup");
}

void NativeTests::phoneWindowCanCloseAndReopen() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    app->show(); app->showPhone(pane);
    auto *first = app->phoneDialog.data(); QVERIFY(first);
    auto *network = first->findChild<QComboBox *>("phone-network");
    auto *start = first->findChild<QPushButton *>("phone-start");
    QVERIFY(network && start);
    network->addItem("Loopback test", "127.0.0.1"); network->setCurrentIndex(network->count()-1);
    start->setEnabled(true); start->click();
    QVERIFY(app->phoneServer && app->phoneServer->isActive());
    QVERIFY(!pane->phoneButton->isHidden());
    const auto address = app->phoneServer->address();
    app->phoneServer->sessions["fixture"] = {QDateTime::currentSecsSinceEpoch()+60,false,0};
    first->close(); QCoreApplication::processEvents();
    QVERIFY(app->phoneServer->isActive()); QCOMPARE(app->phoneServer->address(), address);
    QVERIFY(app->phoneServer->sessions.contains("fixture"));
    pane->phoneButton->click(); QCoreApplication::processEvents();
    auto *again = app->phoneDialog.data(); QVERIFY(again && again->isVisible());
    QVERIFY(app->phoneServer->sessions.contains("fixture"));
    QVERIFY(again->findChild<QPushButton *>("phone-stop")->isEnabled());
    QVERIFY(!again->findChild<QLabel *>("phone-qr")->pixmap().isNull());
    again->findChild<QPushButton *>("phone-stop")->click();
    QVERIFY(!app->phoneServer->isActive()); QVERIFY(pane->phoneButton->isHidden());
    QVERIFY(app->phoneServer->sessions.isEmpty());
}

void NativeTests::pollAndPredictionResults() {
    app->hub.testMode = false;
    twitch->header["channel_id"] = "42";
    QString predictionStatus = "ACTIVE", pollStatus = "ACTIVE";
    const auto now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray,QByteArray>, BytesCallback done) {
        QJsonArray rows;
        if (url.path().endsWith("/polls")) rows.append(QJsonObject{{"status",pollStatus}, {"title","Play?"}, {"ended_at",now},
            {"choices",QJsonArray{QJsonObject{{"title","Yes"},{"votes",12}},QJsonObject{{"title","No"},{"votes",4}}}}});
        else if (url.path().endsWith("/predictions")) rows.append(QJsonObject{{"status",predictionStatus},{"title","Win?"},
            {"ended_at",now},{"winning_outcome_id","a"},{"outcomes",QJsonArray{QJsonObject{{"id","a"},{"title","Yes"},{"channel_points",50}},
            QJsonObject{{"id","b"},{"title","No"},{"channel_points",10}}}}});
        done(QJsonDocument(QJsonObject{{"data",rows}}).toJson(), {});
    };
    twitch->refreshDecorations(); QCOMPARE(str(twitch->activePrediction["text"]), QString("Win?"));
    QVERIFY(!twitch->activePrediction["ended"].toBool());
    predictionStatus="RESOLVED"; pollStatus="COMPLETED"; twitch->refreshDecorations();
    QVERIFY(twitch->activePrediction["ended"].toBool());
    QVERIFY(str(twitch->activePrediction["options"]).contains(QString::fromUtf8("✓ Yes (50 points)")));
    QVERIFY(twitch->activePoll["ended"].toBool());
    QVERIFY(str(twitch->activePoll["options"]).contains("Yes (12)"));
    pollStatus="TERMINATED"; twitch->refreshDecorations();
    QVERIFY(twitch->activePoll["ended"].toBool());
    Pane pane(&app->hub, {twitch}); pane.show();
    auto labels=pane.decorations->findChildren<QLabel *>("chat-decoration");
    QVERIFY(labels.size()>=2); QVERIFY(labels[0]->text().contains("Poll result"));
    QVERIFY(labels[1]->text().contains("Prediction result"));
    auto *yt=app->hub.source("youtube","0sfDth4lMLc");
    QJsonObject poll{{"question",QJsonObject{{"simpleText","Choose"}}},
                     {"choices",QJsonArray{QJsonObject{{"text",QJsonObject{{"simpleText","A"}}}},
                                           QJsonObject{{"text",QJsonObject{{"simpleText","B"}}}}}}};
    yt->youtubeBannerActions({QJsonObject{{"showLiveChatActionPanelAction",QJsonObject{{"panelToShow",QJsonObject{
        {"liveChatActionPanelRenderer",QJsonObject{{"contents",QJsonObject{{"pollRenderer",poll}}}}}}}}}}});
    const QJsonObject entity{{"pollChoiceStates",QJsonArray{QJsonObject{{"value",QJsonObject{{"voteRatio",0.7}}}},
                                                                QJsonObject{{"value",QJsonObject{{"voteRatio",0.3}}}}}}};
    yt->youtubeWebPollState({{"frameworkUpdates",QJsonObject{{"payload",QJsonObject{{"liveChatPollStateEntity",entity}}}}}});
    QVERIFY(str(yt->activePoll["options"]).contains("A (70%)"));
    yt->youtubeBannerActions({QJsonObject{{"closeLiveChatActionPanelAction",QJsonObject{{"targetPanelId","p"}}}}});
    QVERIFY(yt->activePoll["ended"].toBool()); QVERIFY(yt->activePoll["last_observed"].toBool());
    const QJsonObject result{{"icon",QJsonObject{{"iconType","POLL"}}},
                             {"message",QJsonObject{{"simpleText","A (72%) · B (28%)"}}}};
    yt->youtubeBannerActions({QJsonObject{{"addChatItemAction",QJsonObject{{"item",QJsonObject{
        {"liveChatViewerEngagementMessageRenderer",result}}}}}}});
    QVERIFY(yt->activePoll["ended"].toBool()); QVERIFY(!yt->activePoll["last_observed"].toBool());
    QVERIFY(str(yt->activePoll["options"]).contains("72%"));
    QJsonObject metadata{{"questionText","API poll"},{"status","closed"},
                         {"options",QJsonArray{QJsonObject{{"optionText","Yes"},{"tally","7"}}}}};
    yt->youtubeApiPollState({{"activePollItem",QJsonObject{{"snippet",QJsonObject{
        {"pollDetails",QJsonObject{{"metadata",metadata}}}}}}}});
    QVERIFY(yt->activePoll["ended"].toBool()); yt->youtubeApiPollState({});
    QVERIFY(yt->activePoll["ended"].toBool());
    QVERIFY(QMetaObject::invokeMethod(&yt->pollResultTimer,"timeout",Qt::DirectConnection));
    QVERIFY(yt->activePoll.isEmpty());
    app->hub.testMode = true;
}
