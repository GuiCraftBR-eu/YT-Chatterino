#pragma once
#include "core.hpp"
#include <QtNetwork>

namespace yc {
class Network : public QObject {
    Q_OBJECT
  public:
    explicit Network(QObject *parent = nullptr);
    QNetworkAccessManager manager;
    std::function<void(QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback)> transport;
    void bytes(QUrl url, BytesCallback done, QByteArray method = "GET", QByteArray body = {},
               QMap<QByteArray, QByteArray> headers = {}, qint64 maxBytes = 8 * 1024 * 1024);
    void json(QUrl url, JsonCallback done, QByteArray method = "GET", QJsonObject body = {},
              QMap<QByteArray, QByteArray> headers = {});
    void form(QUrl url, const QMap<QString, QString> &form, JsonCallback done);
    static QUrl query(QString base, const QMap<QString, QString> &params);
};

class Images : public QObject {
    Q_OBJECT
  public:
    explicit Images(Network *net, QObject *parent = nullptr);

    struct Entry {
        QByteArray bytes;
        QSharedPointer<QBuffer> buffer;
        QSharedPointer<QMovie> movie;
        QImage image;
        qint64 use = 0;
        qint64 visibleAt = 0;
        qint64 cost = 0;
    };

    Network *net;
    QHash<QString, QSharedPointer<Entry>> cache;
    QSet<QString> pending;
    QHash<QString, qint64> failures;
    qint64 clock = 0, total = 0;
    QTimer visibilityTimer;
    QImage get(const QStringList &urls, bool animate = true);
    void request(const QStringList &urls, int alternative = 0);
    void trim();
    static bool allowed(const QUrl &url);
  signals:
    void ready(QString url);
    void frame(QString url);
};
} // namespace yc
