#pragma once
#include "accounts.hpp"

namespace yc {
class Hub;

class Source : public QObject {
    Q_OBJECT
  public:
    Source(Hub *hub, QString platform, QString id);
    Hub *hub;
    QString platform, id, key;
    QJsonObject header;
    QVector<Message> history;
    QHash<quintptr, int> viewCaps;
    QSet<QString> seen;
    QQueue<QString> seenOrder;
    QHash<QString, Span> emotes;
    QHash<QString, Span> channelEmotes;
    QHash<QString, Badge> badges;
    bool paused = false, closed = false, historyLoaded = false, historyLoading = false;
    QVector<Message> liveBuffer;
    QString continuation, visitor;
    QJsonObject context;
    QTimer poll, metadataTimer;
    bool requesting = false, initial = true;
    bool apiFallbackEnabled = false, apiActive = false;
    QString apiPageToken;
    double apiInterval = 18.25;
    int recommendedMs = 5000, retryMs = 5000;
    int apiRetryMs = 120000;
    double interval = 4;
    qint64 polls = 0;
    quint64 generation = 0;
    QHash<QString, qint64> emoteLoaded;
    qint64 badgesLoaded = 0;
    QString name() const;
    QString room() const;
    void start();
    void reconnect();
    void setPaused(bool paused);
    void stop();
    void publish(Message message);
    void status(const QString &text);
    void redact(const QJsonObject &target);
    void trim();
    int retentionCap() const;
    void refreshHeader();
    void loadEmotes(QString channelId = {});
    void loadBadges();
    void loadHistory();
    void youtubeConnect();
    void youtubePopout(QJsonObject chat, QJsonObject config);
    void youtubePoll();
    void youtubeActions(const QJsonArray &actions, bool historical = false);
    void youtubeContinue(QJsonObject chat, QJsonObject config);
    void youtubeError(const QString &error, int delay = 0);
    bool deferYouTubeForCooldown();
    void youtubeApiConnect();
    void youtubeApiPoll();
    void youtubeApiError(const QString &error);
    void setApiFallback(bool enabled);
    QJsonObject spec() const;
  signals:
    void message(yc::Message message);
    void changed();
    void redacted(QJsonObject target);
};

class TwitchConnection : public QObject {
    Q_OBJECT
  public:
    explicit TwitchConnection(Hub *hub);
    Hub *hub;
    QSslSocket socket;
    QTimer retry, heartbeat;
    QByteArray buffer;
    QHash<QString, QPointer<Source>> channels;
    bool registered = false, connecting = false, closing = false, authRejected = false;
    void join(Source *source);
    void part(Source *source);
    void connectServer();
    void line(const QString &line);
    void close();
};

class Hub : public QObject {
    Q_OBJECT
  public:
    Hub(Config *cfg, Network *net, Accounts *accounts, Images *images, Quota *quota,
        QObject *parent = nullptr);
    Config *cfg;
    Network *net;
    Accounts *accounts;
    Images *images;
    Quota *quota;
    QHash<QString, Source *> sources;
    TwitchConnection twitch;
    qint64 sequence = 0;
    QHash<QString, Span> globals;
    QHash<QString, QString> twitchAvatarUrls;
    QHash<QString, qint64> twitchAvatarRetry;
    QSet<QString> twitchAvatarPending;
    bool globalsLoading = false;
    qint64 globalsLoaded = 0;
    QTimer liveTimer;
    QHash<QString, bool> liveState;
    bool liveRequest = false;
    QTemporaryDir soundFiles;
    bool testMode = false;
    QElapsedTimer youtubeClock;
    qint64 youtubeCooldownUntil = 0;
    int youtubeRateDelay = 120000;
    int youtubeCooldownRemaining() const;
    Source *source(QString platform, QString id);
    void remove(Source *source);
    void globalEmotes();
    void requestTwitchAvatars(const QStringList &logins);
    void provider(Source *source, const QJsonValue &data, QString provider, QString scope);
    void send(Source *source, QString text, Message reply, JsonCallback done);
    void moderate(Source *source, const Message &message, QString action, int duration, JsonCallback done);
    void canModerate(Source *source, std::function<void(bool, QString)> done);
    void youtubeChatId(Source *source, std::function<void(QString, QString)> done);
    void youtubeMessageId(Source *source, Message message, std::function<void(QString, QString)> done);
    void editChannel(Source *source, QString title, QString categoryId, JsonCallback done);
    void profile(Source *source, Message message, JsonCallback done);
    void savedStreams(QJsonObject saved, JsonCallback done);
    void sound(bool live = false, bool force = false);
    void messageAlert(Source *source, const Message &message);
    void pollLive();
    void liveChange(Source *source, bool live);
    void liveChange(QString login, QString name, bool live, bool opened);
  signals:
    void message(Source *source, yc::Message message);
    void sourceChanged(Source *source);
    void notice(QString text);
    void alertPlayed(bool live);
    void twitchAvatarsReady();
};
} // namespace yc
