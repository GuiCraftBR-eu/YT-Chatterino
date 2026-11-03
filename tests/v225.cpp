#include "native_tests.hpp"

void NativeTests::activeFiltersAreVisibleAndSynchronized() {
    QJsonObject ownFilter{{"hidden_users", QJsonArray{"@GuiCraft_"}}};
    auto filters = app->cfg.data["chat_filters"].toObject();
    filters[twitch->key] = ownFilter;
    app->cfg.data["chat_filters"] = filters;
    auto *first = app->addWorkspace({twitch}, "First");
    auto *pane = first->focusedPane.data();
    auto *second = app->addWorkspace({twitch}, "Second");
    auto *otherPane = second->focusedPane.data();
    Message own; own.id = "own"; own.login = "guicraft_"; own.name = "GuiCraft_"; own.text = "hello";
    Message viewer; viewer.id = "viewer"; viewer.login = "viewer"; viewer.name = "Viewer"; viewer.text = "hi";
    twitch->publish(own);
    twitch->publish(viewer);
    app->show();
    app->tabs->setCurrentWidget(first);
    QCoreApplication::processEvents();
    QVERIFY(pane->filterButton->isVisible());
    QVERIFY(!otherPane->filterButton->isHidden());
    QCOMPARE(pane->view->messages.size(), 1);
    QCOMPARE(pane->view->messages.first().id, QString("viewer"));
    QCOMPARE(otherPane->view->messages.size(), 1);
    QSignalSpy requested(pane, &Pane::filtersRequested);
    QTest::mouseClick(pane->filterButton, Qt::LeftButton);
    QCOMPARE(requested.size(), 1);
    QDialog *dialog = nullptr;
    for (auto *candidate : app->findChildren<QDialog *>()) {
        if (candidate->windowTitle() == "Chat filters") dialog = candidate;
    }
    QVERIFY(dialog);
    const auto fields = dialog->findChildren<QPlainTextEdit *>();
    QCOMPARE(fields.size(), 3);
    QCOMPARE(fields.first()->toPlainText(), QString("@GuiCraft_"));
    fields.first()->clear();
    auto *buttons = dialog->findChild<QDialogButtonBox *>();
    QVERIFY(buttons);
    QTest::mouseClick(buttons->button(QDialogButtonBox::Apply), Qt::LeftButton);
    QVERIFY(pane->filterButton->isHidden());
    QVERIFY(otherPane->filterButton->isHidden());
    QCOMPARE(pane->view->messages.size(), 2);
    QCOMPARE(otherPane->view->messages.size(), 2);
    QCOMPARE(pane->view->messages.first().id, QString("own"));
    filters = app->cfg.data["chat_filters"].toObject();
    QCOMPARE(filters[twitch->key].toObject()["hidden_users"].toArray().size(), 0);
    filters[twitch->key] = QJsonObject{{"hide_system", true}};
    app->cfg.data["chat_filters"] = filters;
    emit app->cfg.filtersChanged(twitch->key);
    QVERIFY(!pane->filterButton->isHidden());
    dialog->close();
}

void NativeTests::versionAndToolbarControls() {
    QCOMPARE(app->windowTitle(), QString("YT-Chatterino %1v").arg(APP_VERSION));
    auto *strip = app->tabs->cornerWidget(Qt::TopRightCorner);
    QVERIFY(strip);
    const auto controls = strip->findChildren<QToolButton *>("chat-control", Qt::FindDirectChildrenOnly);
    QCOMPARE(controls.size(), 3);
    QCOMPARE(app->addButton->text(), QString("+"));
    app->show();
    QTest::mouseClick(app->addButton, Qt::LeftButton);
    QDialog *dialog = nullptr;
    for (auto *candidate : app->findChildren<QDialog *>()) {
        if (candidate->windowTitle() == "Add chats") dialog = candidate;
    }
    QVERIFY(dialog);
    auto *combo = dialog->findChild<QComboBox *>();
    QVERIFY(combo);
    QCOMPARE(combo->count(), 4);
    QCOMPARE(combo->itemText(0), QString("Separate tabs"));
    QCOMPARE(combo->itemText(1), QString("Side by side"));
    QCOMPARE(combo->itemText(2), QString("Stacked"));
    QCOMPARE(combo->itemText(3), QString("Mixed feed"));
    dialog->close();
}
