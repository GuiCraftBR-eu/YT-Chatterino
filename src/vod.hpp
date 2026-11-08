#pragma once
#include "view.hpp"
#include <QtWebSockets/QWebSocketServer>

namespace yc {
// Replay traffic uses the providers' public web interfaces, never account credentials.
class VodReplay : public QObject {
    Q_OBJECT
  public:
    explicit VodReplay(Hub *hub, QObject *parent = nullptr);
    struct Item { qint64 offset = 0; Message message; };
    Hub *hub;
    QString platform, video, channel, cursor, youtubeToken;
    QJsonObject youtubeContext;
    QDateTime kickStart;
    qint64 kickChannel = 0, kickDuration = 0;
    qint64 position = 0, nextOffset = 0;
    quint64 epoch = 0;
    bool loading = false, ready = false, ended = false, paused = true;
    bool initialized = false;
    QElapsedTimer wall;
    QTimer retry;
    QVector<Item> pending;
    QSet<QString> seen;
    QString lastCursor;
    int failures = 0;
    static bool identify(const QUrl &url, QString *platform, QString *video, QString *channel);
    void playback(const QUrl &url, qint64 offsetMs, bool paused, bool seek);
    void loadMetadata();
    void fetch();
    void process(QVector<Item> items);
    void deliver();
    void fail(const QString &error);
  signals:
    void cleared();
    void message(yc::Message message);
    void status(QString message);
};

class VodWindow : public QDialog {
    Q_OBJECT
  public:
    explicit VodWindow(Hub *hub, QWidget *parent = nullptr);
    ~VodWindow() override;
    Hub *hub;
    VodReplay replay;
    QWebSocketServer server;
    QPointer<QWebSocket> peer;
    QString secret;
    QLabel *state;
    QLineEdit *pairing;
    ChatView *view;
    void acceptConnection();
};
} // namespace yc
