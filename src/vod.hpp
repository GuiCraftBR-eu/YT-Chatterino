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
    bool initialized = false, announced = false;
    QElapsedTimer wall;
    QTimer retry;
    QVector<Item> pending;
    QSet<QString> seen;
    QString lastCursor;
    int failures = 0;
    static bool identify(const QUrl &url, QString *platform, QString *video, QString *channel);
    static QString sourceKey(const QUrl &url);
    void playback(const QUrl &url, qint64 offsetMs, bool paused, bool seek);
    void disconnectPlayback();
    void loadMetadata();
    void fetch();
    void process(QVector<Item> items);
    void deliver();
    void fail(const QString &error);
  signals:
    void cleared();
    void message(yc::Message message);
    void status(QString message);
    void available();
    void unavailable();
    void metadata(QJsonObject data);
};

class VodBridge : public QObject {
    Q_OBJECT
  public:
    explicit VodBridge(Hub *hub, QObject *parent = nullptr, quint16 port = 37367, bool listen = true);
    ~VodBridge() override;
    Hub *hub;
    QWebSocketServer server;
    QHash<QWebSocket *, QString> clients;
    QHash<QString, QString> players, owners;
    QHash<QString, QUrl> livePlayers;
    QHash<QString, QJsonObject> liveItems, liveCache;
    QHash<QString, QJsonObject> browserPanels;
    quint64 browserPanelSequence = 0;
    QHash<QString, qint64> liveCacheUntil;
    QSet<QString> livePending;
    bool syncScheduled = false;
    QSet<QString> suppressed, hooked;
    static bool allowedOrigin(const QString &origin);
    void acceptConnection();
    void processState(const QString &client, const QJsonObject &data);
    void processLiveState(const QString &client, const QJsonObject &data);
    void requestSync();
    void updateBrowserPanels(const QString &player, const QString &login, const QJsonValue &chat);
    void refreshBrowserPanels(const QString &login);
    bool openTwitchPanel(Source *source, const QString &kind);
    void removeLivePlayer(const QString &player);
    static QJsonObject identifyLive(const QUrl &url);
    void removePlayer(const QString &player, bool navigation = false);
    void disconnectClient(const QString &client);
    void suppress(const QString &key);
    void allow(const QString &key);
    void hook(Source *source);
  signals:
    void discovered(yc::Source *source);
    void liveStreamsChanged();
};
} // namespace yc
