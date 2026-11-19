#pragma once
#include "core.hpp"
#include <QtNetwork>
#include <memory>

namespace yc {
class Network : public QObject {
    Q_OBJECT
  public:
    explicit Network(QObject *parent = nullptr);
    QNetworkAccessManager manager;
    std::function<void(QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>, BytesCallback)> transport;
    void bytes(QUrl url, BytesCallback done, QByteArray method = "GET", QByteArray body = {},
               QMap<QByteArray, QByteArray> headers = {}, qint64 maxBytes = 8 * 1024 * 1024,
               int timeoutMs = 20000);
    void json(QUrl url, JsonCallback done, QByteArray method = "GET", QJsonObject body = {},
              QMap<QByteArray, QByteArray> headers = {}, qint64 maxBytes = 8 * 1024 * 1024,
              int timeoutMs = 20000);
    void sevenTv(const QString &path, bool global, JsonCallback done);
    void form(QUrl url, const QMap<QString, QString> &form, JsonCallback done);
    static QUrl query(QString base, const QMap<QString, QString> &params);
};

class Images : public QObject {
    Q_OBJECT
  public:
    explicit Images(Network *net, QObject *parent = nullptr);
    ~Images() override;

    struct Entry {
        QByteArray bytes;
        QSharedPointer<QBuffer> buffer;
        QSharedPointer<QMovie> movie;
        QImage image;
        qint64 use = 0;
        qint64 visibleAt = 0;
        qint64 cost = 0;
        bool preview = false;
        QString diskSource;
    };

    Network *net;
    QHash<QString, QSharedPointer<Entry>> cache;
    QSet<QString> pending;
    QHash<QString, qint64> failures;
    qint64 clock = 0, total = 0;
    QTimer visibilityTimer, preloadTimer;
    struct PreloadItem { QStringList urls; std::function<bool()> active; bool priority = false; int attempt = 0; };
    QQueue<PreloadItem> preloadQueue;
    QSet<QString> preloadKeys;
    int preloadActive = 0;
    struct Download { QUrl url; BytesCallback done; qint64 maxBytes; int timeoutMs; std::function<bool()> active; };
    QQueue<Download> foregroundDownloads, backgroundDownloads;
    int activeDownloads = 0, maxDownloads = 3;
    qint64 cacheLimit = 48 * 1024 * 1024;
    bool pumpingDownloads = false;
    std::unique_ptr<QLockFile> diskLock;
    QNetworkDiskCache diskCache;
    QTimer diskRetryTimer;
    QTimer diskIndexTimer, diskCleanupTimer, diskSweepTimer;
    QString diskDirectory;
    quint64 diskGeneration = 0;
    QHash<QString, qint64> diskLastShown;
    QHash<QString, qint64> diskVisibleThrottle;
    QSet<QString> diskRetired;
    std::unique_ptr<QDirIterator> diskSweep;
    bool diskIndexDirty = false;
    std::function<qint64()> diskClock;
    void configure(int downloads, int cacheMb, int diskMb = 512);
    void setDiskCacheDirectory(const QString &path);
    bool tryOpenDiskCache();
    bool clearDiskCache();
    static QUrl diskKey(const QUrl &url);
    qint64 diskNow() const;
    bool diskExpired(const QNetworkCacheMetaData &meta) const;
    void markChatVisible(const QStringList &urls);
    void loadDiskIndex();
    void saveDiskIndex();
    void scheduleDiskIndexSave();
    void retireDiskKey(const QUrl &key);
    void startDiskCleanup();
    void sweepDiskCache();
    QByteArray diskImage(const QUrl &url, qint64 maxBytes);
    void saveDiskImage(const QUrl &url, const QByteArray &bytes, quint64 generation);
    void imageBytes(QUrl url, BytesCallback done, bool foreground = true,
                    qint64 maxBytes = 8 * 1024 * 1024, int timeoutMs = 20000,
                    std::function<bool()> active = {});
    void pumpDownloads();
    void preload(const QStringList &urls, std::function<bool()> active = {}, bool priority = false, int attempt = 0);
    void pumpPreloads();
    QImage get(const QStringList &urls, bool animate = true);
    void request(const QStringList &urls, int alternative = 0);
    void requestStillPreview(const QString &key, const QUrl &url, int attempt = 0);
    void trim();
    void retryExpired();
    static bool allowed(const QUrl &url);
  signals:
    void ready(QString url);
    void frame(QString url);
    void diskCacheAvailable();
};
} // namespace yc
