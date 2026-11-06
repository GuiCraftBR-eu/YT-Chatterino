#include "native_tests.hpp"

void NativeTests::settingsLayoutNavigationAndApply() {
    app->addWorkspace({twitch}); app->show(); app->showSettings("Messages");
    auto *settings = app->settings.data(); QVERIFY(settings);
    settings->resize(850, 650); QTest::qWait(50);
    QVERIFY(settings->applyBar->isVisible());
    auto *deleted = settings->findChild<QCheckBox *>("show-deleted-messages"); QVERIFY(deleted);
    deleted->setChecked(true);
    settings->showPage("Accounts");
    QVERIFY(settings->applyBar->isHidden());
    settings->showPage("Messages");
    QVERIFY(deleted->isChecked()); // Switching pages must keep unsaved edits.
    settings->applyButton->click();
    QVERIFY(app->cfg.data["show_deleted_messages"].toBool());

    settings->search->setText("timestamp font size");
    QVERIFY(settings->results->count() > 0);
    QVERIFY(settings->applyBar->isHidden());
    auto *match = settings->results->item(0);
    QCOMPARE(match->data(Qt::UserRole).toString(), QString("Display"));
    emit settings->results->itemClicked(match);
    QCOMPARE(settings->currentPage, QString("Display"));
    QVERIFY(settings->search->text().isEmpty());
    QVERIFY(settings->applyBar->isVisible());
    auto *scroll = qobject_cast<QScrollArea *>(settings->built.value("Display")); QVERIFY(scroll);
    QTRY_VERIFY(scroll->verticalScrollBar()->value() > 0);
    auto *font = settings->findChild<QSpinBox *>("message_font_size"); QVERIFY(font);
    font->setValue(15); settings->applyButton->click();
    QCOMPARE(app->cfg.appearance()["message_font_size"].toInt(), 15);

    settings->showPage("Highlights");
    auto *mention = settings->findChild<QLineEdit *>("mention_color");
    auto *alert = settings->findChild<QLineEdit *>("alert_color"); QVERIFY(mention && alert);
    mention->setText("#112233"); alert->setText("#334455"); settings->applyButton->click();
    QCOMPARE(str(app->cfg.feature("highlight")["mention_color"]), QString("#112233"));
    QCOMPARE(str(app->cfg.feature("highlight")["alert_color"]), QString("#334455"));

    settings->showPage("Notifications");
    auto *notifyKick = settings->findChild<QCheckBox *>("notify-open-kick"); QVERIFY(notifyKick);
    notifyKick->setChecked(false); settings->applyButton->click();
    QVERIFY(!app->cfg.feature("alerts")["all_kick"].toBool());

    settings->showPage("YouTube quota");
    auto *quota = settings->findChild<QSpinBox *>("youtube-daily-limit"); QVERIFY(quota);
    quota->setValue(12000); settings->applyButton->click();
    QCOMPARE(app->cfg.data["youtube_daily_limit"].toInt(), 12000);

    settings->search->setText("phone mobile");
    QVERIFY(settings->results->count() > 0);
    auto *phone = settings->results->item(0);
    QCOMPARE(phone->data(Qt::UserRole).toString(), QString("Tutorial"));
    emit settings->results->itemClicked(phone);
    auto *topics = settings->findChild<QTabWidget *>("tutorial-topics"); QVERIFY(topics);
    QCOMPARE(topics->currentIndex(), 2);
    settings->search->setText("phone mobile");
    app->cfg.data["language"] = "pt-BR";
    emit app->cfg.languageChanged(); QTest::qWait(30);
    QCOMPARE(settings->search->text(), QString("phone mobile"));
    QCOMPARE(settings->pages->currentWidget(), static_cast<QWidget *>(settings->results));
    QCOMPARE(settings->applyButton->text(), QString("Aplicar"));
    settings->search->clear();
    settings->resize(620, 400); QTest::qWait(30);
    QCOMPARE(settings->sidebar->width(), 160);
    settings->resize(850, 650); QTest::qWait(30);
    QCOMPARE(settings->sidebar->width(), 195);
    settings->showPage("Saved channels");
    auto *empty = settings->findChild<QLabel *>("saved-channels-empty");
    QVERIFY(empty && empty->isVisible());
}
