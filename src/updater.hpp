#pragma once
#include "network.hpp"

namespace yc {
class Updater : public QObject {
    Q_OBJECT
  public:
    Updater(Config *cfg, Network *net, QWidget *window, QObject *parent = nullptr);
    Config *cfg;
    Network *net;
    QWidget *window;
    QJsonObject available;
    bool busy = false;
    QTimer timer;
    QString state, error;
    QPointer<QDialog> dialog;
    QLabel *status = nullptr;
    QPushButton *installButton = nullptr;
    void start();
    void check();
    void show();
    void render();
    void install();
    QString kind() const;
    static QJsonObject releaseInfo(const QJsonObject &data, const QString &current, const QString &kind,
                                   QString *error);
    static bool safePath(QString path);
    static bool copyTree(const QString &from, const QString &to, QString *error);
    static bool validTransaction(const QJsonObject &manifest, const QString &path, bool staged = true);
    static int applyUpdate(const QString &manifest, bool interactive = true);
    static void acknowledge(const QString &manifest);
  signals:
    void changed();
    void restartRequested();

  private:
    void prepare(QByteArray bytes);
};
} // namespace yc
