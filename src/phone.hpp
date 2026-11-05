#pragma once
#include "view.hpp"
#include <QtNetwork>

namespace yc {
class PhoneServer : public QObject {
    Q_OBJECT
  public:
    explicit PhoneServer(Hub *hub, QObject *parent = nullptr);
    bool start(Pane *pane, QHostAddress address, quint16 port = 0);
    void stop();
    bool isActive() const;
    QString address() const;
    QString pairingCode() const;
    Hub *hub;
    QPointer<Pane> pane;
    QTcpServer listener;
    QHostAddress bindAddress;
    QString code;
    QElapsedTimer codeAge;
    struct Session { qint64 expires = 0; bool sending = false; quint64 request = 0; };
    struct Attempts { int count = 0; qint64 until = 0; };
    QHash<QByteArray, Session> sessions;
    QHash<QString, Attempts> attempts;
    QHash<QTcpSocket *, QByteArray> buffers;
    QMetaObject::Connection paneDestroyed;
  signals:
    void stopped();
  private:
    void acceptConnections();
    void receive(QTcpSocket *socket);
    void handle(QTcpSocket *socket, QByteArray method, QUrl target,
                QMap<QByteArray, QByteArray> headers, QByteArray body);
    void respond(QTcpSocket *socket, int status, QByteArray contentType, QByteArray body,
                 QByteArray extraHeaders = {});
    QJsonObject state(qint64 after, quint32 clientRevision) const;
    QJsonObject renderMessage(const Message &message) const;
    Source *allowedSource(const QString &key) const;
    Message findMessage(qint64 seq, Source *source) const;
};
}
