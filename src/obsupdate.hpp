#pragma once
#include "network.hpp"

namespace yc {
// Stages a complete, verified OBS plugin beside the current one for the next OBS start.
class ObsPluginUpdater : public QObject {
  public:
    explicit ObsPluginUpdater(Network *network, QObject *parent = nullptr, QString rootOverride = {});
    Network *net;
    QString rootPath;
    QTimer timer;
    bool busy = false;
    std::function<void(const QString &)> notify;
    void start();
    void check();
    void stage(const QJsonObject &info, const QByteArray &bytes);
    void runArchive(const QStringList &arguments, std::function<void(bool, QByteArray)> done);
    static QJsonObject releaseInfo(const QJsonObject &release, const QString &current);
    static bool safeListing(const QByteArray &listing, QSet<QString> *files = nullptr);
};
} // namespace yc
