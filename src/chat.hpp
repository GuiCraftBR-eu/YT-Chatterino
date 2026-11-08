#pragma once
#include "accounts.hpp"
#include <QtWebSockets/QWebSocket>

namespace yc {
class Hub;

class Source : public QObject {
    Q_OBJECT
  public:
    Source(Hub *hub, QString platform, QString id);
    Hub *hub;
    QString platform, id, key;
    QJsonObject header;
    QJsonObject pinned, activePoll, activePrediction;
    QJsonObject chatModes;
    bool chatModesKnown = false;
    QString youtubeModeNotice;
    QVector<Message> history;
    QHash<quintptr, int> viewCaps;
    QSet<QString> seen;
    QQueue<QString> seenOrder;
    QHash<QString, Span> emotes;
    QHash<QString, Span> channelEmotes;
    QHash<QString, Span> kickGlobalEmotes;
    QMap<int, QString> kickSubscriberBadges;
    QHash<QString, Badge> badges;
    QHash<QString, Badge> globalBadges, channelBadges;
    bool paused = false, closed = false, historyLoaded = false, historyLoading = false;
    QVector<Badge> ownTwitchBadges;
    QString ownTwitchUserId, ownTwitchDisplayName, ownTwitchColor, ownTwitchBadgeInfo;
    bool twitchModeratorKnown = false, twitchModerator = false, twitchJoined = false;
    QString youtubeRoleUserId;
    QDateTime youtubeRoleStamp;
    bool youtubeModeratorKnown = false, youtubeModerator = false;
    QHash<QString, qint64> connectionNotices;
    QString continuation, visitor;
    QJsonObject context;
    QTimer poll, metadataTimer, badgeRetry, decorationTimer, historyRetry, pollResultTimer;
    QTimer youtubeDisplayTimer;
    QQueue<Message> youtubePending;
    int youtubeDisplayChunk = 1;
    int historyRetryMs = 30000;
    QString kickChannelId, kickRoomId, kickLastPollId, kickPollHead, kickSocketToken;
    QWebSocket kickSocket;
    QTimer kickSocketRetry, kickSocketWatchdog, kickSocketRenew;
    QTimer kickModesTimer;
    bool kickModesPending = false;
    int kickModesRetryMs = 60000;
    bool kickSocketActive = false, kickSocketStarting = false;
    bool kickThumbnailPending = false;
    qint64 kickThumbnailRetryAt = 0;
    int kickSocketRetryMs = 3000;
    bool kickInitial = true, kickConnecting = false;
    int kickRetryMs = 5000;
    qint64 kickPollCooldownUntil = 0;
    bool decorationPending = false, pollDecorationPending = false, predictionDecorationPending = false;
    qint64 pinRetryAfter = 0;
    quint64 decorationGeneration = 0;
    QString decorationSignature;
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
    qint64 globalBadgesLoaded = 0, channelBadgesLoaded = 0;
    bool globalBadgesPending = false, channelBadgesPending = false;
    QString badgeChannelRoom;
    QString name() const;
    QString room() const;
    void start();
    void reconnect();
    void setPaused(bool paused);
    void stop();
    void publish(Message message);
    void status(const QString &text);
    void connectionStatus(const QString &text);
    void updateTwitchUserState(const QJsonObject &tags);
    void resetTwitchUserState();
    void redact(const QJsonObject &target);
    void trim();
    int retentionCap() const;
    void refreshHeader();
    void refreshDecorations();
    void setDecorations(QJsonObject pin, QJsonObject poll);
    void setPrediction(QJsonObject prediction);
    void youtubeBannerActions(const QJsonArray &actions);
    void youtubeWebPollState(const QJsonObject &response);
    void updateChatModes(const QJsonObject &patch, bool known = true);
    void youtubeModeNotices(const QJsonValue &data);
    QString chatModeSummary() const;
    void youtubeApiPollState(const QJsonObject &data);
    void loadEmotes(QString channelId = {});
    void loadBadges();
    void loadHistory();
    void kickConnect();
    void kickLoadThumbnail();
    void kickPoll();
    void kickFetchPage(QString cursor, int page, QSharedPointer<QVector<Message>> batch, bool historical);
    void kickError(QString error);
    void kickLoadEmotes();
    void kickStartSocket();
    void kickSocketMessage(const QString &text);
    void kickSocketFailed();
    void kickEvent(QString event, QJsonObject data);
    void kickUpdateChatModes(const QJsonObject &room);
    void kickRefreshChatModes();
    void kickCheckRole();
    void youtubeConnect();
    void youtubePopout(QJsonObject chat, QJsonObject config);
    void youtubePoll();
    void youtubeActions(const QJsonArray &actions, bool historical = false);
    void queueYoutubeMessages(const QVector<Message> &messages);
    void flushYoutubeMessages();
    int youtubeWebDelay() const;
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
    void headerChanged();
    void decorationsChanged();
    void chatModesChanged();
    void permissionsChanged();
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
    bool intentionalDisconnect = false;
    quint64 connectionAttempt = 0;
    QByteArray connectionIdentity;
    void join(Source *source);
    void part(Source *source);
    void connectServer();
    void reconnect();
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
    QStringList kickLiveQueue;
    QSet<QString> kickLivePending;
    int kickLiveActive = 0;
    qint64 kickLiveRetryAt = 0;
    QTemporaryDir soundFiles;
    bool testMode = false;
    QElapsedTimer youtubeClock;
    qint64 youtubeCooldownUntil = 0, youtubeConservativeUntil = 0;
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
    void moderatedPage(QPointer<Source> source, QString userId, QString cursor, int page,
                       std::function<void(bool, QString)> done);
    bool hasModerationRole(const Source *source) const;
    bool canEditChannel(const Source *source) const;
    void youtubeChatId(Source *source, std::function<void(QString, QString)> done);
    void youtubeMessageId(Source *source, Message message, std::function<void(QString, QString)> done);
    void editChannel(Source *source, QString title, QString categoryId, JsonCallback done);
    void profile(Source *source, Message message, JsonCallback done);
    void followSince(Source *source, QString userId, JsonCallback done);
    void savedStreams(QJsonObject saved, JsonCallback done);
    void youtubeChannelByName(QString name, JsonCallback done);
    void sound(bool live = false, bool force = false);
    void messageAlert(Source *source, const Message &message);
    void pollLive();
    void pollKickLive();
    void pumpKickLive();
    void liveChange(Source *source, bool live);
    void liveChange(QString login, QString name, bool live, bool opened);
    void liveChange(QString platform, QString login, QString name, bool live, bool opened);
  signals:
    void message(Source *source, yc::Message message);
    void sourceChanged(Source *source);
    void notice(QString text);
    void alertPlayed(bool live);
    void twitchAvatarsReady();
};
} // namespace yc
