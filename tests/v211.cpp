#include "native_tests.hpp"

void NativeTests::globalSearchKeepsTargetsAfterTabChanges() {
    auto *first = app->addWorkspace({twitch}, "First");
    auto *other = app->hub.source("twitch", "another");
    auto *second = app->addWorkspace({other}, "Second");
    Message message;
    message.seq = 7101;
    message.source = twitch->key;
    message.id = "search-fixture";
    message.name = "Viewer";
    message.text = "unique search text";
    first->focusedPane->view->setMessages({message});
    app->show();
    app->globalSearch();
    auto *dialog = app->findChild<QDialog *>("global-search");
    QVERIFY(dialog);
    auto *input = dialog->findChild<QLineEdit *>("global-search-input");
    auto *results = dialog->findChild<QListWidget *>("global-search-results");
    auto *status = dialog->findChild<QLabel *>("global-search-status");
    QVERIFY(input && results && status && input->isClearButtonEnabled());
    input->setText("unique search");
    QCOMPARE(results->count(), 0);
    QTRY_COMPARE(results->count(), 1);
    input->setText("no matching text");
    QCOMPARE(results->count(), 0);
    QTRY_COMPARE(status->text(), "No matching messages.");
    input->setText("unique search");
    QTRY_COMPARE(results->count(), 1);
    app->tabs->tabBar()->moveTab(app->tabs->indexOf(first), app->tabs->indexOf(second));
    app->tabs->setCurrentWidget(second);
    QVERIFY(QMetaObject::invokeMethod(results, "itemActivated", Qt::DirectConnection,
                                      Q_ARG(QListWidgetItem *, results->item(0))));
    QCOMPARE(app->current(), first);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    app->globalSearch();
    dialog = app->findChild<QDialog *>("global-search");
    input = dialog->findChild<QLineEdit *>("global-search-input");
    results = dialog->findChild<QListWidget *>("global-search-results");
    status = dialog->findChild<QLabel *>("global-search-status");
    input->setText("unique search");
    QTRY_COMPARE(results->count(), 1);
    app->closeTab(app->tabs->indexOf(first));
    QVERIFY(QMetaObject::invokeMethod(results, "itemActivated", Qt::DirectConnection,
                                      Q_ARG(QListWidgetItem *, results->item(0))));
    QCOMPARE(app->current(), second);
    QCOMPARE(status->text(), "This message is no longer available.");
    QVERIFY(dialog->isVisible());
    input->clear();
    QCOMPARE(results->count(), 0);
    QVERIFY(status->text().isEmpty());
}

void NativeTests::tutorialClearlyExplainsTestingUsers() {
    app->showSettings("Tutorial");
    QVERIFY(app->settings->search->isClearButtonEnabled());
    auto tutorialText = [this] {
        QString all;
        auto *page = app->settings->built.value("Tutorial");
        for (auto *label : page->findChildren<QLabel *>()) {
            all += label->text() + '\n';
        }
        return all;
    };
    QVERIFY(tutorialText().contains("If the OAuth app is in Testing"));
    QVERIFY(tutorialText().contains("Test users is required"));
    QVERIFY(app->settings->findChild<QLabel *>("google-testing-requirement"));
    app->cfg.data["language"] = "pt-BR";
    emit app->cfg.languageChanged();
    app->settings->showPage("Tutorial");
    QVERIFY(tutorialText().contains("é obrigatório adicionar a conta Google exata"));
    QVERIFY(tutorialText().contains("Usuários de teste"));
    QVERIFY(app->settings->findChild<QLabel *>("google-testing-requirement"));
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    QVERIFY(pane->searchInput->isClearButtonEnabled());
}

void NativeTests::youtubeRateLimitWaitsAndHidesRedirect() {
    auto *youtube = app->hub.source("youtube", "0sfDth4lMLc");
    app->cfg.data["language"] = "pt-BR";
    youtube->youtubeError("HTTP 429: Error transferring https://www.google.com/sorry/index?q=fixture");
    QVERIFY(youtube->poll.isActive());
    QCOMPARE(youtube->poll.interval(), 120000);
    QVERIFY(youtube->history.last().text.contains("YouTube limitou temporariamente"));
    QVERIFY(youtube->history.last().text.contains("120s"));
    QVERIFY(!youtube->history.last().text.contains("fixture"));
    QVERIFY(!youtube->history.last().text.contains("google.com"));
    youtube->setPaused(true);
    youtube->youtubeError("HTTP 429: another limit");
    QVERIFY(!youtube->poll.isActive());
    QVERIFY(unexpectedRequests.isEmpty());
}
