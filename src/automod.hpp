#pragma once
#include "chat.hpp"

namespace yc {
// One EventSub connection covers all open channels moderated by this account.
class TwitchAutoMod : public QObject {
  public:
    explicit TwitchAutoMod(Hub *hub);
    Hub *hub;
    QPointer<QWebSocket> active, migrating;
    QTimer refreshTimer, retryTimer, watchdog;
    QString session, accountKey;
    QSet<QString> subscribed, pending, eventIds;
    QHash<QString, QString> subscriptionIds;
    QHash<QString, qint64> retryAfter;
    QQueue<QString> eventOrder;
    int retryMs = 3000;
    bool stopping = false;
    void scheduleRefresh();
    void refresh();
    void connectSocket(const QUrl &url, bool migration = false);
    void frame(QWebSocket *socket, const QJsonObject &message);
    void subscribe();
    void event(const QJsonObject &payload);
    void review(Source *source, const QString &messageId, bool allow, JsonCallback done);
    void resolve(Source *source, const QString &messageId, const QString &status);
};
} // namespace yc
