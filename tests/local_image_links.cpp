#include "native_tests.hpp"

class TestLinkReceiver : public QObject {
    Q_OBJECT
  public:
    QUrl opened;
  public slots:
    void capture(const QUrl &url) { opened = url; }
};

void NativeTests::youtubeFullLinkDestinations() {
    const QString shown = QString::fromUtf8("https://example.com/long…");
    const QString destination = "https://example.com/very-long-path?first=1&second=a%26b#top";
    const QString redirect = "https://www.youtube.com/redirect?event=live_chat&q=" + QString::fromLatin1(QUrl::toPercentEncoding(destination));
    const QJsonObject linkRun{{"text", shown}, {"navigationEndpoint", QJsonObject{{"urlEndpoint", QJsonObject{{"url", redirect}}}}}};
    const QJsonObject renderer{{"id", "link"}, {"authorName", QJsonObject{{"simpleText", "Viewer"}}},
                               {"message", QJsonObject{{"runs", QJsonArray{QJsonObject{{"text", QString::fromUtf8("😀 ")}}, linkRun,
                                                                             QJsonObject{{"text", " and https://example.org/other."}}}}}}};
    auto message = parseYouTube(renderer, "liveChatTextMessageRenderer");
    QCOMPARE(message.links.size(), 1);
    QCOMPARE(message.text.mid(message.links[0].start, message.links[0].end - message.links[0].start), shown);
    QCOMPARE(message.links[0].url, destination);
    auto restored = Message::fromJson(message.json());
    QCOMPARE(restored.links.size(), 1);
    QCOMPARE(restored.links[0].url, destination);
    auto paid = renderer; paid["purchaseAmountText"] = QJsonObject{{"simpleText", "$5"}};
    const auto superchat = parseYouTube(paid, "liveChatPaidMessageRenderer");
    QCOMPARE(superchat.links[0].start, message.links[0].start + 3);
    QCOMPARE(superchat.text.mid(superchat.links[0].start, superchat.links[0].end - superchat.links[0].start), shown);

    auto *youtube = app->hub.source("youtube", "abcdefghijk");
    auto *pane = app->addWorkspace({youtube})->focusedPane.data();
    message.source = youtube->key; message.seq = 45;
    message.reply = {{"id", "parent"}, {"name", "Parent"}, {"text", "Original"}};
    pane->view->setMessages({message});
    app->show(); QCoreApplication::processEvents();
    const auto doc = pane->view->document(0);
    auto selected = doc->find(shown);
    QVERIFY(!selected.isNull());
    selected.setPosition(selected.selectionStart()); selected.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    QCOMPARE(selected.charFormat().anchorHref(), destination);
    auto other = doc->find("https://example.org/other");
    QVERIFY(!other.isNull()); other.setPosition(other.selectionStart()); other.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    QCOMPARE(other.charFormat().anchorHref(), QString("https://example.org/other"));
    auto hit = doc->find(shown); const int position = hit.selectionStart() + 3;
    (void)doc->size();
    const auto block = doc->findBlock(position);
    const auto line = block.layout()->lineForTextPosition(position - block.position());
    QVERIFY(line.isValid());
    const auto bounds = doc->documentLayout()->blockBoundingRect(block);
    const auto left = line.cursorToX(position - block.position());
    const auto right = line.cursorToX(position - block.position() + 1);
    const QPoint point(qRound(6 + bounds.left() + (left + right) / 2),
                       qRound(3 + pane->view->offsets[0] - pane->view->verticalScrollBar()->value() + bounds.top() + line.y() + line.height() / 2));
    TestLinkReceiver receiver; QDesktopServices::setUrlHandler("https", &receiver, "capture");
    QSignalSpy threadClicks(pane->view, &ChatView::threadClicked);
    QTest::mouseClick(pane->view->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QDesktopServices::unsetUrlHandler("https");
    QCOMPARE(receiver.opened.toString(QUrl::FullyEncoded), destination);
    QCOMPARE(threadClicks.size(), 0);
    const auto ordinary = linkSpansFor("See https://example.org/wiki/Topic_(detail)).");
    QCOMPARE(ordinary.size(), 1);
    QCOMPARE(ordinary[0].url, QString("https://example.org/wiki/Topic_(detail)"));
    auto banner = QJsonObject{{"actionId", "pin"}, {"contents", QJsonObject{{"liveChatTextMessageRenderer", renderer}}}};
    youtube->youtubeBannerActions({QJsonObject{{"addBannerToLiveChatCommand", QJsonObject{{"bannerRenderer", QJsonObject{{"liveChatBannerRenderer", banner}}}}}}});
    QCOMPARE(youtube->pinned["links"].toArray().size(), 1);
    pane->refreshDecorations();
    const auto *label = pane->decorations->findChild<QLabel *>("chat-decoration"); QVERIFY(label);
    QVERIFY(label->text().contains("href=\"" + destination.toHtmlEscaped() + "\""));
    QVERIFY(label->text().contains(shown));

    for (const auto &bad : {QString("javascript:alert(1)"), QString("file:///etc/passwd"), QString("https://user@example.com/")}) {
        auto unsafeRun = linkRun;
        unsafeRun["navigationEndpoint"] = QJsonObject{{"urlEndpoint", QJsonObject{{"url", bad}}}};
        auto unsafe = renderer; unsafe["message"] = QJsonObject{{"runs", QJsonArray{unsafeRun}}};
        QVERIFY(parseYouTube(unsafe, "liveChatTextMessageRenderer").links.isEmpty());
    }
    auto unsafe = renderer;
    unsafe["message"] = QJsonObject{{"runs", QJsonArray{QJsonObject{{"text", shown}, {"navigationEndpoint", QJsonObject{{"urlEndpoint", QJsonObject{{"url", "https://www.youtube.com/redirect?q=javascript%3Aalert%281%29"}}}}}}}}};
    QVERIFY(parseYouTube(unsafe, "liveChatTextMessageRenderer").links.isEmpty());
}

void NativeTests::imageSettingsPersistAndApply() {
    const QString screenshots = qEnvironmentVariable("YTCHATTERINO_QA_SCREENSHOTS");
    if (!screenshots.isEmpty()) configureApplication(*qApp);
    app->showSettings("Images and cache");
    auto *settings = app->settings.data(); QVERIFY(settings);
    auto *mode = settings->findChild<QComboBox *>("image-settings-mode"); QVERIFY(mode);
    auto *preset = settings->findChild<QComboBox *>("image-preset"); QVERIFY(preset);
    auto *downloads = settings->findChild<QSpinBox *>("image-download-limit"); QVERIFY(downloads);
    auto *cache = settings->findChild<QSpinBox *>("image-cache-mb"); QVERIFY(cache);
    auto *disk = settings->findChild<QSpinBox *>("image-disk-cache-mb"); QVERIFY(disk);
    auto *clear = settings->findChild<QPushButton *>("clear-image-disk-cache"); QVERIFY(clear);
    QCOMPARE(app->images.maxDownloads, 3);
    mode->setCurrentIndex(mode->findData("advanced")); downloads->setValue(5); cache->setValue(96); disk->setValue(640);
    settings->applyButton->click();
    QCOMPARE(app->images.maxDownloads, 5); QCOMPARE(app->images.cacheLimit, qint64(96) * 1024 * 1024);
    Config reloaded(app->cfg.home);
    QCOMPARE(reloaded.data["image_settings_mode"].toString(), QString("advanced"));
    QCOMPARE(reloaded.data["image_download_limit"].toInt(), 5);
    QCOMPARE(reloaded.data["image_cache_mb"].toInt(), 96);
    QCOMPARE(reloaded.data["image_disk_cache_mb"].toInt(), 640);
    {
        MainWindow restarted(app->cfg.home, true);
        QCOMPARE(restarted.images.maxDownloads, 5);
        QCOMPARE(restarted.images.cacheLimit, qint64(96) * 1024 * 1024);
        QCOMPARE(restarted.images.diskCache.maximumCacheSize(), qint64(640) * 1024 * 1024);
    }
    mode->setCurrentIndex(mode->findData("simple")); preset->setCurrentIndex(preset->findData("fast"));
    settings->applyButton->click(); QCOMPARE(app->images.maxDownloads, 8); QCOMPARE(app->images.cacheLimit, qint64(128) * 1024 * 1024);
    QCOMPARE(app->images.diskCache.maximumCacheSize(), qint64(1024) * 1024 * 1024);
    if (!screenshots.isEmpty()) settings->grab().save(QDir(screenshots).filePath("image-settings-simple.png"));
    mode->setCurrentIndex(mode->findData("advanced"));
    QCOMPARE(downloads->value(), 5); QCOMPARE(cache->value(), 96); QCOMPARE(disk->value(), 640);
    settings->applyButton->click(); QCOMPARE(app->images.maxDownloads, 5);
    const QUrl url("https://cdn.7tv.app/emote/settings-test/1x.webp");
    QImage image(3, 3, QImage::Format_ARGB32); image.fill(Qt::blue);
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); QVERIFY(image.save(&buffer, "PNG"));
    app->net.transport = [png](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) { done(png, {}); };
    app->images.get({url.toString()});
    QVERIFY(app->images.diskCache.metaData(Images::diskKey(url)).isValid());
    clear->click();
    QVERIFY(!app->images.diskCache.metaData(Images::diskKey(url)).isValid());
    app->images.configure(0, 5000);
    QCOMPARE(app->images.maxDownloads, 1); QCOMPARE(app->images.cacheLimit, qint64(1024) * 1024 * 1024);
    if (!screenshots.isEmpty()) { settings->grab().save(QDir(screenshots).filePath("image-settings.png")); }
}

void NativeTests::imageDownloadConcurrencyAndPriority() {
    Network net; Images images(&net); images.configure(2, 48);
    QStringList requested; QVector<BytesCallback> requests; QStringList completed;
    net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        requested << url.path(); requests << done;
    };
    auto queue = [&](QString name, bool foreground) {
        images.imageBytes(QUrl("https://cdn.7tv.app/" + name), [&, name](QByteArray, QString) { completed << name; }, foreground);
    };
    queue("a", false); queue("b", false); queue("c", false); queue("visible", true);
    QCOMPARE(requested, QStringList({"/a", "/b"}));
    { auto done = requests[0]; done("a", {}); }
    QCOMPARE(requested, QStringList({"/a", "/b", "/visible"}));
    images.configure(1, 48);
    { auto done = requests[1]; done("b", {}); }
    QCOMPARE(requests.size(), 3);
    { auto done = requests[2]; done("visible", {}); }
    QCOMPARE(requested.last(), QString("/c"));
    { auto done = requests[3]; done("c", {}); }
    QCOMPARE(images.activeDownloads, 0);
    QCOMPARE(completed, QStringList({"a", "b", "visible", "c"}));
    queue("d", false); queue("e", false); queue("f", true);
    QCOMPARE(requested.last(), QString("/d")); images.configure(3, 48);
    QCOMPARE(requested.mid(4), QStringList({"/d", "/f", "/e"}));
    for (int i : {4, 5, 6}) { auto done = requests[i]; done({}, {}); }
    QVERIFY(images.foregroundDownloads.isEmpty()); QVERIFY(images.backgroundDownloads.isEmpty());
}

void NativeTests::diskImageCacheSurvivesRestart() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QImage image(8, 8, QImage::Format_ARGB32); image.fill(Qt::blue);
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); QVERIFY(image.save(&buffer, "PNG"));
    const QByteArray gif = QByteArray::fromHex(
        "47494638396102000200800000ff000000000021ff0b4e45545343415045322e30030100000021f904080a0000002c000000"
        "000200020000020284510021f904090a0001002c00000000020002000002028c53003b");
    const QString still = qEnvironmentVariable("YTCHATTERINO_DISK_TEST_URL", "https://cdn.betterttv.net/emote/disk/2x");
    const QString animation = "https://cdn.7tv.app/emote/disk/2x.webp";
    const QString preview = "https://cdn.7tv.app/emote/disk/1x_static.webp";
    int downloads = 0;
    const auto processPhase = qEnvironmentVariable("YTCHATTERINO_DISK_TEST_PHASE");
    if (!processPhase.isEmpty()) {
        const auto path = qEnvironmentVariable("YTCHATTERINO_DISK_TEST_PATH"); QVERIFY(!path.isEmpty());
        Network net; Images images(&net); images.setDiskCacheDirectory(path);
        if (processPhase != "live_write") net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
            ++downloads;
            if (processPhase == "write") done(png, {}); else done({}, "Unexpected download after restart");
        };
        images.get({still});
        QTRY_VERIFY_WITH_TIMEOUT(images.cache.contains(still), processPhase == "live_write" ? 25000 : 5000);
        QCOMPARE(downloads, processPhase == "write" ? 1 : 0);
        const auto entry = images.cache.value(still);
        QVERIFY(!entry->image.isNull() || entry->movie);
        if (processPhase.startsWith("live_") && still.endsWith("/1x.webp"))
            QVERIFY(entry->movie && entry->movie->frameCount() > 1);
        if (processPhase == "write" || processPhase == "read")
            QCOMPARE(entry->image.pixelColor(0, 0), QColor(Qt::blue));
        return;
    }
    {
        Network net; Images images(&net); images.configure(3, 48, 64); images.setDiskCacheDirectory(directory.path());
        net.transport = [&](QUrl url, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
            ++downloads; done(url.toString() == animation ? gif : png, {});
        };
        images.get({still}); images.get({animation, preview});
        QCOMPARE(downloads, 3);
        QVERIFY(images.diskCache.metaData(Images::diskKey(QUrl(still))).isValid());
        QVERIFY(images.diskCache.metaData(Images::diskKey(QUrl(animation))).isValid());
        QVERIFY(images.diskCache.metaData(Images::diskKey(QUrl(preview))).isValid());
    }
    {
        Network net; Images images(&net); images.configure(3, 48, 64); images.setDiskCacheDirectory(directory.path());
        net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
            ++downloads; done({}, "Network unavailable");
        };
        images.preload({still});
        QTRY_VERIFY(images.cache.contains(still));
        images.get({animation, preview});
        QTRY_VERIFY(images.cache.contains(animation) && images.cache.value(animation)->movie);
        QCOMPARE(images.cache.value(still)->image.pixelColor(0, 0), QColor(Qt::blue));
        QCOMPARE(images.cache.value(animation)->movie->frameCount(), 2);
        QCOMPARE(downloads, 3); // All three images came from disk after reopening.
    }
    {
        Network firstNet, secondNet;
        auto first = std::make_unique<Images>(&firstNet); first->setDiskCacheDirectory(directory.path());
        Images second(&secondNet); second.setDiskCacheDirectory(directory.path());
        QVERIFY(second.diskCache.cacheDirectory().isEmpty());
        first.reset();
        QVERIFY(second.tryOpenDiskCache());
        QVERIFY(!second.diskCache.cacheDirectory().isEmpty());
    }
    for (const auto &phase : {QStringLiteral("write"), QStringLiteral("read")}) {
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("QT_QPA_PLATFORM", "offscreen");
        environment.insert("YTCHATTERINO_DISK_TEST_PHASE", phase);
        environment.insert("YTCHATTERINO_DISK_TEST_PATH", QDir(directory.path()).filePath("process-cache"));
        child.setProcessEnvironment(environment);
        child.setProgram(QCoreApplication::applicationFilePath());
        child.setArguments({"diskImageCacheSurvivesRestart"});
        child.start(); QVERIFY2(child.waitForStarted(), qPrintable(child.errorString()));
        QVERIFY2(child.waitForFinished(20000), qPrintable(child.errorString()));
        const auto output = child.readAllStandardOutput() + child.readAllStandardError();
        QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, output.constData());
    }
}

void NativeTests::diskImageCacheExpiryAndCorruption() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QImage image(8, 8, QImage::Format_ARGB32); image.fill(Qt::green);
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); QVERIFY(image.save(&buffer, "PNG"));
    const QUrl url("https://cdn.7tv.app/emote/disk-corrupt/1x.webp");
    const auto key = Images::diskKey(url);
    Network net; Images images(&net); images.setDiskCacheDirectory(directory.path());
    int downloads = 0;
    net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) { ++downloads; done(png, {}); };
    images.get({url.toString()}); QCOMPARE(downloads, 1);
    auto meta = images.diskCache.metaData(key); QVERIFY(meta.isValid());
    images.diskCache.remove(key);
    auto *bad = images.diskCache.prepare(meta); QVERIFY(bad);
    bad->write(QByteArray(32, 'x') + png); images.diskCache.insert(bad);
    images.cache.clear(); images.total = 0;
    images.get({url.toString()});
    QTRY_COMPARE(downloads, 2);
    QVERIFY(!images.get({url.toString()}, false).isNull());

    meta = images.diskCache.metaData(key); QVERIFY(meta.isValid());
    images.diskCache.remove(key); meta.setExpirationDate(QDateTime::currentDateTimeUtc().addSecs(-1));
    auto *expired = images.diskCache.prepare(meta); QVERIFY(expired);
    expired->write(QCryptographicHash::hash(png, QCryptographicHash::Sha256) + png); images.diskCache.insert(expired);
    images.cache.clear(); images.total = 0;
    images.get({url.toString()});
    QTRY_COMPARE(downloads, 3);
    QVERIFY(!images.get({url.toString()}, false).isNull());
    QVERIFY(!images.diskCache.metaData(QUrl("https://cdn.7tv.app/emote/disk-corrupt/1x.webp")).isValid());

    images.clearDiskCache(); images.cache.clear(); images.total = 0;
    images.diskCache.setMaximumCacheSize(65536);
    QRandomGenerator random(42);
    QImage noisy(64, 64, QImage::Format_RGB32);
    for (int y = 0; y < noisy.height(); ++y)
        for (int x = 0; x < noisy.width(); ++x) noisy.setPixel(x, y, random.generate() | 0xff000000u);
    QByteArray bigPng; QBuffer bigBuffer(&bigPng); bigBuffer.open(QIODevice::WriteOnly); QVERIFY(noisy.save(&bigBuffer, "PNG"));
    QVERIFY(bigPng.size() > 8000);
    net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) { done(bigPng, {}); };
    for (int i = 0; i < 12; ++i) {
        images.get({QString("https://cdn.7tv.app/emote/disk-limit-%1/1x.webp").arg(i)});
        QTest::qWait(2);
    }
    QVERIFY(images.diskCache.cacheSize() <= images.diskCache.maximumCacheSize());
    QVERIFY(!images.diskCache.metaData(Images::diskKey(QUrl("https://cdn.7tv.app/emote/disk-limit-0/1x.webp"))).isValid());
    QVERIFY(images.diskCache.metaData(Images::diskKey(QUrl("https://cdn.7tv.app/emote/disk-limit-11/1x.webp"))).isValid());
    images.diskCache.setMaximumCacheSize(32768);
    QVERIFY(images.diskCache.cacheSize() <= images.diskCache.maximumCacheSize());
}

void NativeTests::diskImageCacheClearDuringDownload() {
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QImage image(8, 8, QImage::Format_ARGB32); image.fill(Qt::yellow);
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); QVERIFY(image.save(&buffer, "PNG"));
    const QUrl url("https://cdn.betterttv.net/emote/clear-in-flight/2x");
    Network net; Images images(&net); images.setDiskCacheDirectory(directory.path());
    BytesCallback delayed; int downloads = 0;
    net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) { ++downloads; delayed = done; };
    images.get({url.toString()}); QVERIFY(bool(delayed));
    images.clearDiskCache(); delayed(png, {});
    QVERIFY(!images.diskCache.metaData(Images::diskKey(url)).isValid());
    images.cache.clear(); images.total = 0;
    images.get({url.toString()}); QCOMPARE(downloads, 2);
    delayed(png, {});
    QVERIFY(images.diskCache.metaData(Images::diskKey(url)).isValid());
    images.clearDiskCache();
    QVERIFY(!images.diskCache.metaData(Images::diskKey(url)).isValid());
}

void NativeTests::diskCacheTracksChatVisibility() {
    qint64 now = 1800000000;
    app->images.diskClock = [&] { return now; };
    const QString chatUrl = "https://cdn.7tv.app/emote/visible-chat/2x.webp";
    const QString pinUrl = "https://cdn.7tv.app/emote/visible-pin/2x.webp";
    QImage image(16, 16, QImage::Format_ARGB32); image.fill(Qt::green);
    for (const auto &url : {chatUrl, pinUrl}) {
        auto entry = QSharedPointer<Images::Entry>::create(); entry->image = image;
        app->images.cache[url] = entry;
    }
    Span emote; emote.code = "VisibleEmote"; emote.urls = {chatUrl}; emote.gif = true;
    twitch->emotes[emote.code] = emote;
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    Message message; message.id = "visible"; message.seq = 702; message.source = twitch->key;
    message.name = "Viewer"; message.text = emote.code;
    pane->view->setMessages({message});
    QVERIFY(pane->view->document(0));
    const auto chatId = Images::diskKey(QUrl(chatUrl)).path().mid(1);
    QVERIFY(!app->images.diskLastShown.contains(chatId)); // Layout and preloading are not display.
    app->show(); QCoreApplication::processEvents(); pane->view->viewport()->repaint();
    QTRY_COMPARE(app->images.diskLastShown.value(chatId), now);

    pane->view->setMessages({});
    const QJsonArray spans{QJsonObject{{"start", 0}, {"end", 3}, {"code", "Pin"}, {"urls", QJsonArray{pinUrl}}}};
    twitch->pinned = {{"text", "Pin"}, {"native_emotes", spans}};
    const auto pinId = Images::diskKey(QUrl(pinUrl)).path().mid(1);
    pane->refreshDecorations();
    QVERIFY(!app->images.diskLastShown.contains(pinId));
    auto *label = pane->decorations->findChild<QLabel *>("chat-decoration"); QVERIFY(label);
    label->repaint(); QTRY_COMPARE(app->images.diskLastShown.value(pinId), now);
}

void NativeTests::diskCacheEvictsUnusedEmotes() {
    constexpr qint64 day = 24 * 60 * 60;
    qint64 now = 1800000000;
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QImage image(8, 8, QImage::Format_ARGB32); image.fill(Qt::cyan);
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); QVERIFY(image.save(&buffer, "PNG"));
    const QString used = "https://cdn.7tv.app/emote/seen/1x.webp";
    const QString unused = "https://cdn.7tv.app/emote/unseen/1x.webp";
    const auto usedKey = Images::diskKey(QUrl(used)), unusedKey = Images::diskKey(QUrl(unused));
    int downloads = 0;
    {
        Network net; Images images(&net); images.diskClock = [&] { return now; }; images.setDiskCacheDirectory(directory.path());
        net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) { ++downloads; done(png, {}); };
        images.preload({used}); images.preload({unused});
        QTRY_COMPARE(downloads, 2);
        QTRY_VERIFY(images.cache.contains(used) && images.cache.contains(unused));
        QVERIFY(images.diskLastShown.isEmpty());
        now += 5 * day;
        images.markChatVisible({used});
        QCOMPARE(images.diskLastShown.value(usedKey.path().mid(1)), now);
        now += 3 * day;
        images.startDiskCleanup();
        QTRY_VERIFY(!images.diskSweep);
        QVERIFY(!images.diskCache.metaData(unusedKey).isValid());
        QVERIFY(images.diskCache.metaData(usedKey).isValid());
        QVERIFY(images.diskRetired.contains(unusedKey.path().mid(1)));
        images.cache.clear(); images.total = 0;
        images.preload({unused});
        QTRY_VERIFY(images.preloadQueue.isEmpty() && images.preloadActive == 0);
        QCOMPARE(downloads, 2); // A retired emote is not downloaded again by preloading.
    }
    {
        Network net; Images images(&net); images.diskClock = [&] { return now; }; images.setDiskCacheDirectory(directory.path());
        net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) { ++downloads; done(png, {}); };
        QVERIFY(images.diskRetired.contains(unusedKey.path().mid(1)));
        images.preload({unused});
        QTRY_VERIFY(images.preloadQueue.isEmpty() && images.preloadActive == 0);
        QCOMPARE(downloads, 2); // The retirement survives a restart.
        images.get({used}); QTRY_VERIFY(images.cache.contains(used)); QCOMPARE(downloads, 2);
        images.get({unused}); QTRY_VERIFY(images.cache.contains(unused)); QCOMPARE(downloads, 3);
        images.markChatVisible({unused});
        QVERIFY(!images.diskRetired.contains(unusedKey.path().mid(1)));
        now += 6 * day;
        images.startDiskCleanup(); QTRY_VERIFY(!images.diskSweep);
        QVERIFY(!images.diskCache.metaData(usedKey).isValid());
        QVERIFY(images.diskCache.metaData(unusedKey).isValid());
        QVERIFY(images.cache.contains(used));
        images.markChatVisible({used}); // A still-resident image can return to disk without a download.
        QVERIFY(images.diskCache.metaData(usedKey).isValid());
        QVERIFY(!images.diskRetired.contains(usedKey.path().mid(1)));
        QCOMPARE(downloads, 3);
        images.markChatVisible({unused});
        now += 59; images.markChatVisible({unused});
        now += 7 * day - 59;
        QVERIFY(!images.diskExpired(images.diskCache.metaData(unusedKey)));
        now += 60;
        QVERIFY(images.diskExpired(images.diskCache.metaData(unusedKey)));
    }
}

#include "local_image_links.moc"
