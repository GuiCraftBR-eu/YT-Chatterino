#include "native_tests.hpp"

void NativeTests::emoteHoverOpensImmediately() {
    ChatView view(&app->hub);
    view.resize(480, 200);
    view.show();
    const QString url = "https://cdn.betterttv.net/emote/immediate/3x";
    auto entry = QSharedPointer<Images::Entry>::create();
    entry->image = QImage(24, 24, QImage::Format_ARGB32);
    entry->image.fill(Qt::green);
    app->images.cache[url] = entry;
    Message message;
    message.seq = 9414;
    message.name = "Viewer";
    message.source = twitch->key;
    message.text = "Instant";
    message.emotes = {{0, 7, "Instant", {url}, false}};
    view.setMessages({message});
    auto doc = view.document(0);
    (void)doc->size();
    const auto emotes = view.details.value(message.seq).emoteImages;
    QCOMPARE(emotes.size(), 1);
    int pos = emotes.constBegin().key();
    auto block = doc->findBlock(pos);
    auto line = block.layout()->lineForTextPosition(pos - block.position());
    auto bounds = doc->documentLayout()->blockBoundingRect(block);
    QPoint point(qRound(6 + bounds.left() + (line.cursorToX(pos - block.position()) +
                                             line.cursorToX(pos - block.position() + 1)) / 2),
                 qRound(3 + bounds.top() + line.y() + line.height() / 2));
    auto move = [&](QPoint at) {
        QMouseEvent event(QEvent::MouseMove, QPointF(at), QPointF(view.viewport()->mapToGlobal(at)),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &event);
    };
    move(point);
    QVERIFY(view.emoteTooltip && view.emoteTooltip->isVisible());
    QCOMPARE(view.emoteTooltipName->text(), "Instant");
    QCOMPARE(view.tooltipMessage, message.seq);
    auto *popup = view.emoteTooltip;
    move(point + QPoint(1, 0));
    QCOMPARE(view.emoteTooltip, popup);
    QVERIFY(popup->isVisible());
    move(QPoint(470, 180));
    QVERIFY(!popup->isVisible());
}

void NativeTests::tutorialTopicsAndGoogleRequirement() {
    app->showSettings("Tutorial");
    auto *settings = app->settings.data();
    auto *topics = settings->findChild<QTabWidget *>("tutorial-topics");
    QVERIFY(topics);
    QCOMPARE(topics->count(), 4);
    QCOMPARE(topics->tabText(0), "Getting started");
    QCOMPARE(topics->tabText(1), "Accounts");
    topics->setCurrentIndex(1);
    auto *requirement = topics->widget(1)->findChild<QLabel *>("google-testing-requirement");
    QVERIFY(requirement);
    QVERIFY(requirement->text().contains("Test users is required"));
    QVERIFY(requirement->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
    app->cfg.data["language"] = "pt-BR";
    emit app->cfg.languageChanged();
    settings->showPage("Tutorial");
    topics = settings->built.value("Tutorial")->findChild<QTabWidget *>("tutorial-topics");
    QVERIFY(topics);
    QCOMPARE(topics->tabText(0), "Primeiros passos");
    QCOMPARE(topics->tabText(3), "Personalização");
    requirement = topics->widget(1)->findChild<QLabel *>("google-testing-requirement");
    QVERIFY(requirement && requirement->text().contains("Usuários de teste"));
}

void NativeTests::compactTabBarKeepsControlsUsable() {
    auto *yt = app->hub.source("youtube", "0sfDth4lMLc");
    auto *youtubeTab = app->addWorkspace({yt}, "YouTube test");
    auto *twitchTab = app->addWorkspace({twitch}, "Twitch test");
    app->resize(400, 300);
    app->show();
    app->tabs->setCurrentWidget(youtubeTab);
    QCoreApplication::processEvents();
    QVERIFY(app->quotaLabel->isVisible());
    QVERIFY(app->quotaLabel->text().startsWith("YT ~"));
    auto *controls = app->tabs->cornerWidget(Qt::TopRightCorner);
    QVERIFY(controls && controls->isVisible());
    for (auto *button : {app->addButton, app->layoutButton, app->gear}) {
        QVERIFY(button->isVisible());
        QCOMPARE(button->size(), QSize(23, 23));
        QVERIFY(controls->rect().contains(QRect(button->mapTo(controls, QPoint()), button->size())));
    }
    app->tabs->setCurrentWidget(twitchTab);
    QCoreApplication::processEvents();
    QVERIFY(!app->quotaLabel->isVisible());
    QVERIFY(app->tabs->isMovable());
    QVERIFY(app->tabs->tabsClosable());
}
