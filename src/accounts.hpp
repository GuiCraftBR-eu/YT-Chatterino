#pragma once
#include "network.hpp"

namespace yc {
class Vault : public QObject {
    Q_OBJECT
  public:
    explicit Vault(Config *cfg);
    Config *cfg;
    QString target(const QString &provider) const;
    void read(const QString &provider, BytesCallback done);
    void write(const QString &provider, const QByteArray &bytes, BytesCallback done = {});
};

class Accounts : public QObject {
    Q_OBJECT
  public:
    Accounts(Config *cfg, Network *net, Quota *quota, QObject *parent = nullptr);
    Config *cfg;
    Network *net;
    Quota *quota;
    Vault vault;
    QWidget *window = nullptr;
    QJsonObject twitch, google;
    bool loaded = false, twitchBusy = false, googleBusy = false;
    bool persistenceEnabled = true;
    QPointer<QDialog> twitchDialog;
    QTimer deviceTimer, googleTimeout;
    QTcpServer callbackServer;
    QString deviceCode, verifier, state, redirect;
    QJsonObject googleClient;
    QList<TokenCallback> twitchWaiters, googleWaiters;
    QList<TokenCallback> twitchRefreshWaiters, googleRefreshWaiters;
    bool twitchRefreshing = false, googleRefreshing = false;
    QString twitchIssue, googleIssue;
    void load();
    void validateTwitch();
    QStringList names() const;
    QSet<QString> twitchScopes() const;
    QSet<QString> googleScopes() const;
    bool connected(const QString &provider) const;
    bool twitchAllows(const QSet<QString> &scopes) const;
    bool googleAllowsWriting() const;
    bool googleClientConfigured() const;
    static QSet<QString> fullTwitchScopes();
    void twitchToken(TokenCallback done);
    void requireTwitch(const QSet<QString> &scopes, TokenCallback done);
    void signInTwitch(const QSet<QString> &scopes = {});
    void finishTwitch(QJsonObject auth, QString error = {});
    void saveTwitch();
    void signInGoogle(TokenCallback done = {});
    void googleToken(TokenCallback done);
    void finishGoogle(QJsonObject auth, QString error = {});
    void cancelGoogle();
    void helix(QString endpoint, JsonCallback done, QByteArray method = "GET",
               QMap<QString, QString> params = {}, QJsonObject body = {});
    void youtube(QString endpoint, JsonCallback done, QByteArray method = "GET",
                 QMap<QString, QString> params = {}, QJsonObject body = {}, int units = 1);
  signals:
    void changed();
    void ready();
    void status(QString text);

  private:
    QJsonObject clientFile(QString *error);
    void pollDevice();
    void googleCallback();
};
} // namespace yc
