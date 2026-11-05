#include "native_tests.hpp"
void NativeTests::independentAlertAndMentionColors() {
 auto settings=app->cfg.feature("highlight"); settings["alerts"]=true; settings["mention_me"]=true;
 settings["alert_color"]="#aa1122"; settings["mention_color"]="#1177aa"; app->cfg.setFeature("highlight",settings);
 Message alert; alert.special="alert"; alert.text="Subscribed";
 QCOMPARE(highlight(alert,app->cfg,{"tester"}).name(),QString("#aa1122"));
 Message mention; mention.text="hello tester";
 QCOMPARE(highlight(mention,app->cfg,{"tester"}).name(),QString("#1177aa"));
 app->showSettings("Highlights");
 QVERIFY(app->settings->findChild<QLineEdit *>("alert_color"));
 QVERIFY(app->settings->findChild<QLineEdit *>("mention_color"));
}
