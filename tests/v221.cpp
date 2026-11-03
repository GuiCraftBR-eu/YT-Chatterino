#include "native_tests.hpp"

static Message sample221(qint64 seq, const QString &source, const QString &body) {
    Message m;
    m.seq = seq;
    m.id = QString::number(seq);
    m.source = source;
    m.name = "Viewer";
    m.text = body;
    return m;
}

void NativeTests::stableAlternateRowsAndGeometry() {
    ChatView view(&app->hub);
    view.resize(300, 300);
    view.messageCap = 3;
    view.show();
    Message a = sample221(1, twitch->key, "first");
    Message b = sample221(2, twitch->key, QString(110, 'x'));
    Message c = sample221(3, twitch->key, "third");
    Message d = sample221(4, twitch->key, "fourth");
    view.setMessages({a, b, c});
    QCoreApplication::processEvents();
    (void)view.viewport()->grab();
    const bool color = view.rowStripes.value(b.seq);
    QVERIFY(color != view.rowStripes.value(a.seq));
    QVERIFY(color != view.rowStripes.value(c.seq));
    view.append(d);
    QCOMPARE(view.messages.first().seq, b.seq);
    QCOMPARE(view.rowStripes.value(b.seq), color);
    view.setMessages({b, c, d});
    QCOMPARE(view.rowStripes.value(b.seq), color);
    (void)view.viewport()->grab();
    QCoreApplication::processEvents();
    const QImage painted = view.viewport()->grab().toImage();
    for (int row = 0; row < view.messages.size(); ++row) {
        const int y = view.offsets[row] - view.verticalScrollBar()->value() + view.heights[row] / 2;
        if (y < 0 || y >= painted.height()) continue;
        const QColor expected = view.rowStripes.value(view.messages[row].seq) ? QColor("#0e0e0e") : QColor(Qt::black);
        QCOMPARE(painted.pixelColor(painted.width() - 20, y), expected);
    }
    QVERIFY(view.heights[0] > view.heights[1]);
    QCOMPARE(view.offsets[1], view.heights[0]);
}

void NativeTests::twitchBadgesRetryAndChannelPriority() {
    app->hub.testMode = false;
    QMap<QString, QList<BytesCallback>> requests;
    app->net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        requests[url.path()].append(done);
    };
    const QString globalPath = "/helix/chat/badges/global", channelPath = "/helix/chat/badges";
    twitch->loadBadges();
    QCOMPARE(requests[globalPath].size(), 1);
    QCOMPARE(requests[channelPath].size(), 1);
    const auto payload = [](const QString &url, const QString &title) {
        const QJsonObject version{{"id", "1"}, {"title", title}, {"image_url_4x", url}};
        return QJsonDocument(QJsonObject{{"data", QJsonArray{QJsonObject{
            {"set_id", "sub-gifter"}, {"versions", QJsonArray{version}}}}}}).toJson();
    };
    const QString globalUrl = "https://static-cdn.jtvnw.net/badges/global/3";
    const QString channelUrl = "https://static-cdn.jtvnw.net/badges/channel/3";
    requests[channelPath].takeFirst()({}, "temporary failure");
    QCOMPARE(twitch->channelBadgesLoaded, qint64(0));
    QVERIFY(twitch->badgeRetry.isActive());
    requests[globalPath].takeFirst()(payload(globalUrl, "Global gift"), {});
    QCOMPARE(twitch->badges.value("sub-gifter/1").urls.value(0), globalUrl);
    twitch->loadBadges();
    QCOMPARE(requests[channelPath].size(), 1);
    QVERIFY(requests[globalPath].isEmpty());
    requests[channelPath].takeFirst()(payload(channelUrl, "Channel gift"), {});
    QCOMPARE(twitch->badges.value("sub-gifter/1").urls.value(0), channelUrl);
    QVERIFY(twitch->channelBadgesLoaded > 0);
    twitch->globalBadgesLoaded = 0;
    twitch->loadBadges();
    requests[globalPath].takeFirst()(payload(globalUrl, "Global gift"), {});
    QCOMPARE(twitch->badges.value("sub-gifter/1").urls.value(0), channelUrl);
    app->hub.testMode = true;
}

void NativeTests::badgeHoverShowsImageAndName() {
    ChatView view(&app->hub);
    view.resize(480, 200);
    view.show();
    const QString url = "https://static-cdn.jtvnw.net/badges/channel/3";
    auto image = QSharedPointer<Images::Entry>::create();
    image->image = QImage(24, 24, QImage::Format_ARGB32);
    image->image.fill(Qt::green);
    app->images.cache[url] = image;
    Message m = sample221(23, twitch->key, "hello");
    Badge moderator; moderator.id = moderator.role = "moderator";
    Badge gift; gift.id = gift.role = "sub-gifter"; gift.label = "Gift Leader"; gift.urls = {url};
    m.badges = {moderator, gift};
    view.setMessages({m});
    auto doc = view.document(0);
    (void)doc->size();
    auto badges = view.details.value(m.seq).badgeImages;
    QCOMPARE(badges.size(), 2);
    for (auto i = badges.cbegin(); i != badges.cend(); ++i) {
        const int pos = i.key();
        auto block = doc->findBlock(pos);
        auto line = block.layout()->lineForTextPosition(pos - block.position());
        auto bounds = doc->documentLayout()->blockBoundingRect(block);
        const qreal a = line.cursorToX(pos - block.position());
        const qreal b = line.cursorToX(pos - block.position() + 1);
        const QPoint point(qRound(6 + bounds.left() + (a + b) / 2),
                           qRound(3 + bounds.top() + line.y() + line.height() / 2));
        QVERIFY(view.showEmoteTooltip(point, view.viewport()->mapToGlobal(point)));
        QCOMPARE(view.emoteTooltipName->text(), i.value().first);
        QCOMPARE(view.emoteTooltipProvider->text(), "Twitch · Badge");
        QVERIFY(!view.emotePreview->pixmap().isNull());
    }
}

void NativeTests::mixedHeaderAndCopyStreamLinks() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    yt->header = {{"channel", "Video Channel"}, {"title", "Video title"}, {"live", true}, {"viewers", 123}};
    auto *pane = app->addWorkspace({twitch, yt})->focusedPane.data();
    const QString text = pane->headerText->text();
    QVERIFY(text.contains("TW · GuiCraft_"));
    QVERIFY(text.contains("YT · Video Channel"));
    QVERIFY(text.contains('\n'));
    QVERIFY(text.contains("Offline title"));
    QVERIFY(text.contains("Video title"));
    QCOMPARE(pane->headerText->toolTip(), text);
    QMenu menu;
    app->populatePaneMenu(menu, pane);
    const auto actions = menu.findChildren<QAction *>("copy-stream-link");
    QCOMPARE(actions.size(), 2);
    for (auto *action : actions) {
        auto *channel = qobject_cast<QMenu *>(action->parent());
        QVERIFY(channel);
        const int index = channel->actions().indexOf(action);
        QVERIFY(index > 0);
        QCOMPARE(channel->actions().at(index - 1)->objectName(), "open-stream-browser");
        action->trigger();
        const auto actual = QGuiApplication::clipboard()->text();
        QVERIFY(actual == browserUrl("twitch", "guicraft_", "stream") ||
                actual == browserUrl("youtube", "0sfDth4lMLc", "stream"));
    }
}

void NativeTests::sentTwitchMessageKeepsChannelBadgeInEitherOrder() {
    const QString url = "https://static-cdn.jtvnw.net/badges/channel/3";
    Badge badge; badge.id = badge.role = "sub-gifter"; badge.version = "1";
    Badge full = badge; full.label = "Channel gift"; full.urls = {url};
    twitch->badges["sub-gifter/1"] = full;
    auto image = QSharedPointer<Images::Entry>::create();
    image->image = QImage(24, 24, QImage::Format_ARGB32);
    image->image.fill(Qt::green);
    app->images.cache[url] = image;
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    Message local = sample221(0, {}, "hello");
    local.id = "message-a"; local.own = true;
    twitch->publish(local);
    const qint64 sequence = twitch->history.last().seq;
    Message remote = local; remote.own = false; remote.badges = {badge};
    twitch->publish(remote);
    QCOMPARE(twitch->history.size(), 1);
    QCOMPARE(twitch->history.last().seq, sequence);
    QCOMPARE(twitch->history.last().source, twitch->key);
    QVERIFY(twitch->history.last().own);
    pane->reload();
    auto doc = pane->view->document(0);
    (void)doc->size();
    QCOMPARE(pane->view->details.value(sequence).badgeImages.size(), 1);
    QVERIFY(pane->view->details.value(sequence).urls.contains(url));
    remote.id = "message-b";
    twitch->publish(remote);
    local.id = "message-b";
    twitch->publish(local);
    QCOMPARE(twitch->history.size(), 2);
    QVERIFY(twitch->history.last().own);
    QCOMPARE(twitch->history.last().badges.size(), 1);
    QCOMPARE(twitch->history.last().source, twitch->key);
}
