#include "native_tests.hpp"

static QPoint textPoint(ChatView *view, int row, int pos) {
    auto doc = view->document(row);
    (void)doc->size();
    auto block = doc->findBlock(pos);
    auto line = block.layout()->lineForTextPosition(pos - block.position());
    auto bounds = doc->documentLayout()->blockBoundingRect(block);
    const qreal a = line.cursorToX(pos - block.position());
    const qreal b = line.cursorToX(pos - block.position() + 1);
    return QPoint(qRound(6 + bounds.left() + (a + b) / 2),
                  qRound(3 + view->offsets[row] - view->verticalScrollBar()->value() +
                         bounds.top() + line.y() + line.height() / 2));
}

void NativeTests::mixedChannelLogosAndPreviews() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    const QString twUrl = "https://static-cdn.jtvnw.net/channel.png";
    const QString ytUrl = "https://yt3.ggpht.com/channel.png";
    twitch->header["avatar"] = twUrl;
    yt->header = {{"channel", "YouTuber"}, {"avatar", ytUrl}, {"title", "Video"}};
    for (const auto &url : {twUrl, ytUrl}) {
        auto entry = QSharedPointer<Images::Entry>::create();
        entry->image = QImage(32, 32, QImage::Format_ARGB32);
        entry->image.fill(url == twUrl ? Qt::red : Qt::green);
        app->images.cache[url] = entry;
    }
    auto *pane = app->addWorkspace({twitch, yt})->focusedPane.data();
    app->show();
    QCoreApplication::processEvents();
    QVERIFY(pane->mixedHeader->isVisible());
    QVERIFY(!pane->mixedIcons[0]->pixmap().isNull());
    QCOMPARE(pane->mixedIcons[0]->accessibleName(), "GuiCraft_");
    QCOMPARE(pane->mixedIcons[1]->accessibleName(), "YouTuber");
    Message m; m.id = "mixed"; m.name = "Viewer"; m.text = "hello";
    twitch->publish(m);
    auto *view = pane->view;
    auto doc = view->document(0);
    (void)doc->size();
    const auto channelImages = view->details.value(view->messages[0].seq).channelImages;
    QCOMPARE(channelImages.size(), 1);
    QVERIFY(!doc->toPlainText().contains("[GuiCraft_]"));
    QVERIFY(view->messageText(0).contains("[GuiCraft_]"));
    const QPoint point = textPoint(view, 0, channelImages.constBegin().key());
    QVERIFY(view->showEmoteTooltip(point, view->viewport()->mapToGlobal(point)));
    QCOMPARE(view->emoteTooltipName->text(), "GuiCraft_");
    QCOMPARE(view->emoteTooltipProvider->text(), "Twitch · Channel");
    QVERIFY(!view->emotePreview->pixmap().isNull());
    view->showChannelTooltip(yt, pane->mixedIcons[1]->mapToGlobal(QPoint(5, 5)));
    QCOMPARE(view->emoteTooltipName->text(), "YouTuber");
    QCOMPARE(view->emoteTooltipProvider->text(), "YouTube · Channel");
    const QString lateUrl = "https://yt3.ggpht.com/late-channel.png";
    BytesCallback pending;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        QCOMPARE(url.toString(), lateUrl);
        pending = done;
    };
    yt->header["avatar"] = lateUrl;
    emit yt->changed();
    QVERIFY(pending);
    QImage late(32, 32, QImage::Format_ARGB32);
    late.fill(Qt::blue);
    QByteArray png;
    QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
    QVERIFY(late.save(&buffer, "PNG"));
    pending(png, {});
    QCOMPARE(pane->mixedIcons[1]->pixmap().toImage().pixelColor(10, 10), QColor(Qt::blue));
}

void NativeTests::sendLocksInputAndKeepsHistory() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    BytesCallback response;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        QCOMPARE(url.path(), "/helix/chat/messages");
        response = done;
    };
    auto finishSend = [&](const QString &text, const QString &id) {
        pane->input->setText(text);
        pane->send();
        QVERIFY(pane->input->isReadOnly());
        QTest::keyClicks(pane->input, "ignored");
        QCOMPARE(pane->input->text(), text);
        QVERIFY(response);
        response(QJsonDocument(QJsonObject{{"data", QJsonArray{QJsonObject{{"is_sent", true}, {"message_id", id}}}}}).toJson(), {});
        QVERIFY(!pane->input->isReadOnly());
        QVERIFY(pane->input->text().isEmpty());
    };
    finishSend("first", "one");
    finishSend("second", "two");
    pane->input->setText("my draft");
    QTest::keyClick(pane->input, Qt::Key_Up);
    QCOMPARE(pane->input->text(), "second");
    QTest::keyClick(pane->input, Qt::Key_Up);
    QCOMPARE(pane->input->text(), "first");
    QTest::keyClick(pane->input, Qt::Key_Down);
    QCOMPARE(pane->input->text(), "second");
    QTest::keyClick(pane->input, Qt::Key_Down);
    QCOMPARE(pane->input->text(), "my draft");
    QCOMPARE(pane->input->sentHistory.size(), 2);
    pane->input->setText("failed send");
    pane->send();
    QVERIFY(pane->input->isReadOnly());
    QTimer::singleShot(0, [] {
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *box = qobject_cast<QMessageBox *>(widget)) box->accept();
    });
    response({}, "temporary failure");
    QVERIFY(!pane->input->isReadOnly());
    QCOMPARE(pane->input->text(), "failed send");
    QCOMPARE(pane->input->sentHistory.size(), 2);
    pane->input->setText("pending during change");
    pane->send();
    QVERIFY(pane->input->isReadOnly());
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    pane->setSources({yt});
    QVERIFY(!pane->input->isReadOnly());
    response("{\"data\":[{\"is_sent\":true,\"message_id\":\"late\"}]}", {});
    QCOMPARE(pane->input->text(), "pending during change");
}

void NativeTests::pickerStaysOpenForMultipleEmotes() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    for (const auto &code : {QString("Alpha"), QString("Beta")}) {
        Span emote; emote.code = code;
        twitch->channelEmotes[code] = emote;
    }
    app->accounts.twitch["scope"] = QJsonArray{"chat:read"};
    app->show();
    pane->openEmotes();
    auto *dialog = pane->findChildren<QDialog *>().last();
    auto *grid = dialog->findChild<QListWidget *>("emote-grid");
    QCOMPARE(grid->count(), 2);
    QTest::mouseClick(grid->viewport(), Qt::LeftButton, Qt::NoModifier, grid->visualItemRect(grid->item(0)).center());
    QVERIFY(dialog->isVisible());
    QTest::mouseClick(grid->viewport(), Qt::LeftButton, Qt::NoModifier, grid->visualItemRect(grid->item(1)).center());
    QVERIFY(dialog->isVisible());
    QCOMPARE(pane->input->text(), "Alpha Beta ");
}

void NativeTests::mentionsOpenCorrectUserHistory() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    app->show();
    Message target; target.id = "target"; target.uid = "77"; target.login = "someuser";
    target.name = "SomeUser"; target.text = "earlier message";
    twitch->publish(target);
    Message mention; mention.id = "mention"; mention.uid = "88"; mention.login = "other";
    mention.name = "Other"; mention.text = "Hi @SomeUser, see https://example.com/@not-a-mention";
    mention.reply = {{"id", "root"}, {"name", "Parent"}, {"text", "earlier"}};
    twitch->publish(mention);
    QCoreApplication::processEvents();
    auto *view = pane->view;
    auto doc = view->document(1); (void)doc->size();
    const auto found = view->details.value(view->messages[1].seq).mentions;
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().name, "SomeUser");
    const QPoint point = textPoint(view, 1, found.first().start + 2);
    QSignalSpy spy(view, &ChatView::userClicked);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QCOMPARE(spy.size(), 1);
    const auto selected = qvariant_cast<Message>(spy.first().first());
    QCOMPARE(selected.uid, "77");
    auto card = app->userCards.value(twitch->key + ":77");
    QVERIFY(card);
    auto *history = card->findChild<ChatView *>();
    QVERIFY(history);
    QCOMPARE(history->messages.size(), 1);
    QCOMPARE(history->messages.first().text, "earlier message");
}

void NativeTests::youtubeChannelAvatarFromOwnerMessage() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    yt->header = {{"channel_id", "UCowner"}, {"channel", "Channel"}};
    Message other; other.uid = "UCother"; other.avatar = "https://yt3.ggpht.com/other";
    yt->publish(other);
    QVERIFY(str(yt->header["avatar"]).isEmpty());
    Message owner; owner.uid = "UCowner"; owner.avatar = "https://yt3.ggpht.com/owner";
    yt->publish(owner);
    QCOMPARE(str(yt->header["avatar"]), owner.avatar);
}

void NativeTests::youtubeStructuredMentionAndChannelAvatar() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    app->hub.testMode = false;
    const QByteArray html = R"html(ytInitialPlayerResponse = {"videoDetails":{"videoId":"0sfDth4lMLc","channelId":"UCowner","author":"Owner","isLive":true}}; ytInitialData = {"videoOwnerRenderer":{"navigationEndpoint":{"browseEndpoint":{"browseId":"UCowner"}},"thumbnail":{"thumbnails":[{"url":"https://yt3.ggpht.com/owner","width":88}]}}};)html";
    app->net.transport = [html](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        done(html, {});
    };
    yt->refreshHeader();
    QCOMPARE(str(yt->header["avatar"]), "https://yt3.ggpht.com/owner");
    app->hub.testMode = true;
    const auto json = QJsonDocument::fromJson(R"json({"id":"mention","authorName":{"simpleText":"Other"},"authorExternalChannelId":"UCother","message":{"runs":[{"text":"Hello "},{"text":"@Some User","navigationEndpoint":{"browseEndpoint":{"browseId":"UCtarget"}}}]}})json").object();
    Message mention = parseYouTube(json, "liveChatTextMessageRenderer");
    QCOMPARE(mention.mentions.size(), 1);
    QCOMPARE(mention.mentions.first().name, "Some User");
    QCOMPARE(mention.mentions.first().uid, "UCtarget");
    QCOMPARE(Message::fromJson(mention.json()).mentions.first().uid, "UCtarget");
    Message target; target.id = "target"; target.uid = "UCtarget"; target.login = target.name = "Different Display";
    target.text = "Earlier YouTube message";
    auto *pane = app->addWorkspace({yt})->focusedPane.data();
    app->show();
    yt->publish(target);
    yt->publish(mention);
    QCoreApplication::processEvents();
    auto *view = pane->view;
    auto doc = view->document(1); (void)doc->size();
    const auto spans = view->details.value(view->messages[1].seq).mentions;
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans.first().name, "Some User");
    QCOMPARE(spans.first().uid, "UCtarget");
    QSignalSpy spy(view, &ChatView::userClicked);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, textPoint(view, 1, spans.first().start + 6));
    QCOMPARE(spy.size(), 1);
    QCOMPARE(qvariant_cast<Message>(spy.first().first()).uid, "UCtarget");
    const auto card = app->userCards.value(yt->key + ":UCtarget");
    QVERIFY(card);
    auto *history = card->findChild<ChatView *>();
    QVERIFY(history);
    QCOMPARE(history->messages.size(), 1);
    QCOMPARE(history->messages.first().text, "Earlier YouTube message");
}
