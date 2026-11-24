#include "native_tests.hpp"
#include <QTcpServer>
#include <QTcpSocket>

void NativeTests::chatSendIsolatedFromStalledRequests() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    Network net;
    int stalled = 0, sent = 0, backgroundFinished = 0;
    connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto *socket = server.nextPendingConnection()) {
            auto request = QSharedPointer<QByteArray>::create();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket, request] {
                request->append(socket->readAll());
                if (!request->contains("\r\n\r\n") || socket->property("handled").toBool()) return;
                socket->setProperty("handled", true);
                if (request->startsWith("GET /slow")) {
                    ++stalled; // Keep these six requests open without sending a response.
                } else if (request->startsWith("POST /send")) {
                    ++sent;
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                  "Content-Length: 2\r\nConnection: close\r\n\r\n{}");
                    socket->disconnectFromHost();
                }
            });
        }
    });
    const QString base = QString("http://127.0.0.1:%1").arg(server.serverPort());
    for (int i = 0; i < 6; ++i)
        net.bytes(QUrl(base + "/slow"), [&](QByteArray, QString) { ++backgroundFinished; },
                  "GET", {}, {}, 1024, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(stalled, 6, 5000);
    bool finished = false; QString failure;
    net.json(QUrl(base + "/send"), [&](QJsonObject, QString error) { failure = error; finished = true; },
             "POST", {{"text", "fixture"}}, {}, 1024, 2000, Network::Lane::ChatSend);
    QTRY_VERIFY_WITH_TIMEOUT(finished, 4000);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QCOMPARE(sent, 1);
    QCOMPARE(backgroundFinished, 0);
    for (auto *reply : net.manager.findChildren<QNetworkReply *>()) reply->abort();
}

void NativeTests::twitchSendTimeoutHasUncertainOutcome() {
    auto *pane = app->addWorkspace({twitch})->focusedPane.data();
    int calls = 0;
    app->net.transport = [&](QUrl url, QByteArray method, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        QCOMPARE(url.path(), QString("/helix/chat/messages")); QCOMPARE(method, QByteArray("POST"));
        ++calls; done({}, "HTTP 0: Operation canceled");
    };
    const QString draft = "An unsent test message";
    pane->input->setText(draft);
    QString shownWarning;
    QTimer::singleShot(0, app.get(), [&] {
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *box = qobject_cast<QMessageBox *>(widget)) {
            shownWarning = box->text(); box->accept(); return;
        }
    });
    pane->send();
    QCOMPARE(calls, 1);
    QCOMPARE(shownWarning, QString("Twitch did not confirm whether your message was sent. Check the chat before trying again."));
    QCOMPARE(pane->input->text(), draft);
    QVERIFY(!pane->sending); QVERIFY(!pane->input->isReadOnly());

    QString returnedError;
    app->net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback done) {
        ++calls; done({}, "HTTP 429: Too Many Requests");
    };
    app->hub.send(twitch, draft, {}, [&](QJsonObject, QString error) { returnedError = error; });
    QCOMPARE(returnedError, QString("HTTP 429: Too Many Requests"));
    QCOMPARE(calls, 2);

    // Keep the fake-token request on localhost while checking which manager receives it.
    QTcpServer proxy;
    QVERIFY(proxy.listen(QHostAddress::LocalHost, 0));
    app->net.chatManager.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", proxy.serverPort()));
    app->net.transport = {};
    bool completed = false;
    app->hub.send(twitch, draft, {}, [&](QJsonObject, QString) { completed = true; });
    const auto replies = app->net.chatManager.findChildren<QNetworkReply *>();
    QCOMPARE(replies.size(), 1);
    QCOMPARE(replies.first()->url().host(), QString("api.twitch.tv"));
    replies.first()->abort();
    QTRY_VERIFY(completed);
}
