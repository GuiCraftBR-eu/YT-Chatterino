#include "accounts.hpp"

namespace yc {
static const QString KickRedirect = QStringLiteral("http://localhost:37365/callback");

void Accounts::signInKick(QString clientId, QString clientSecret) {
    if (kickBusy) return;
    clientId = clientId.trimmed();
    if (clientSecret.isEmpty() && clientId == str(kick["client_id"])) clientSecret = str(kick["client_secret"]);
    if (clientId.isEmpty() || clientSecret.isEmpty()) {
        kickIssue = "Enter your Kick client ID and client secret.";
        emit changed(); emit status(kickIssue); return;
    }
    if (!kickCallbackServer.listen(QHostAddress::LocalHost, 37365)) {
        kickIssue = "Could not open the local Kick sign-in callback on port 37365.";
        emit changed(); emit status(kickIssue); return;
    }
    kickBusy = true;
    kickIssue.clear();
    kickClientId = clientId;
    kickClientSecret = clientSecret;
    QByteArray bytes(64, 0);
    for (auto &byte : bytes) byte = char(QRandomGenerator::system()->generate() & 255);
    kickVerifier = QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    kickState = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto challenge = QString::fromLatin1(QCryptographicHash::hash(kickVerifier.toUtf8(), QCryptographicHash::Sha256)
                                                   .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    const auto url = Network::query("https://id.kick.com/oauth/authorize",
        {{"client_id", clientId}, {"redirect_uri", KickRedirect}, {"response_type", "code"},
         {"scope", "user:read chat:write"}, {"state", kickState}, {"code_challenge", challenge},
         {"code_challenge_method", "S256"}});
    kickTimeout.start(180000);
    emit changed();
    if (!QDesktopServices::openUrl(url)) {
        finishKick("Could not open the browser for Kick sign-in.");
        return;
    }
    emit status("Complete Kick sign-in in your browser.");
}

void Accounts::kickCallback() {
    while (kickCallbackServer.hasPendingConnections()) {
        auto *socket = kickCallbackServer.nextPendingConnection();
        auto buffer = QSharedPointer<QByteArray>::create();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            buffer->append(socket->readAll());
            if (buffer->size() > 16384) { socket->disconnectFromHost(); return; }
            if (!buffer->contains("\r\n\r\n")) return;
            const auto first = buffer->split('\n').value(0).trimmed().split(' ');
            const QUrl url("http://localhost" + QString::fromUtf8(first.value(1)));
            const QUrlQuery query(url);
            const auto error = query.queryItemValue("error");
            const auto code = query.queryItemValue("code");
            const bool valid = first.value(0) == "GET" && socket->peerAddress().isLoopback() && kickBusy &&
                url.path() == "/callback" && !kickState.isEmpty() && query.queryItemValue("state") == kickState &&
                (!code.isEmpty() || !error.isEmpty());
            const QByteArray body = valid ? "Return to YT-Chatterino to continue." : "Invalid sign-in callback.";
            socket->write(QByteArray(valid ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 400 Bad Request\r\n") +
                          "Content-Type: text/plain; charset=utf-8\r\nConnection: close\r\nContent-Length: " +
                          QByteArray::number(body.size()) + "\r\n\r\n" + body);
            socket->disconnectFromHost();
            if (!valid) return;
            kickCallbackServer.close();
            if (!error.isEmpty()) { finishKick("Kick sign-in was cancelled or denied."); return; }
            net->form(QUrl("https://id.kick.com/oauth/token"),
                      {{"grant_type", "authorization_code"}, {"client_id", kickClientId},
                       {"client_secret", kickClientSecret}, {"redirect_uri", KickRedirect},
                       {"code_verifier", kickVerifier}, {"code", code}},
                      [this](QJsonObject token, QString issue) {
                if (!kickBusy) return;
                if (!issue.isEmpty() || str(token["access_token"]).isEmpty()) {
                    finishKick("Kick could not complete sign-in. Check the app credentials and redirect URL.");
                    return;
                }
                net->json(QUrl("https://api.kick.com/public/v1/users"),
                          [this, token](QJsonObject response, QString error) mutable {
                    if (!kickBusy) return;
                    const auto user = firstItem(response["data"].toArray()).toObject();
                    if (!error.isEmpty() || str(user["user_id"]).isEmpty()) {
                        finishKick("Kick could not identify the signed-in account.");
                        return;
                    }
                    kick = {{"access_token", token["access_token"]}, {"refresh_token", token["refresh_token"]},
                            {"expires_at", double(QDateTime::currentSecsSinceEpoch() + token["expires_in"].toInt())},
                            {"scope", token["scope"]}, {"user_id", user["user_id"]}, {"name", user["name"]},
                            {"client_id", kickClientId}, {"client_secret", kickClientSecret}};
                    saveKick();
                    finishKick();
                }, "GET", {}, {{"Authorization", "Bearer " + str(token["access_token"]).toUtf8()}});
            });
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void Accounts::finishKick(QString error) {
    kickCallbackServer.close();
    kickTimeout.stop();
    kickBusy = false;
    kickVerifier.clear(); kickState.clear(); kickClientId.clear(); kickClientSecret.clear();
    kickIssue = error;
    if (!error.isEmpty()) emit status(error);
    emit changed();
}

void Accounts::saveKick() {
    if (!persistenceEnabled) return;
    vault.write("kick", QJsonDocument(kick).toJson(QJsonDocument::Compact), [this](QByteArray, QString error) {
        if (!error.isEmpty()) emit status(error);
    });
}

void Accounts::kickToken(TokenCallback done) {
    const auto token = str(kick["access_token"]);
    if (!token.isEmpty() && kick["expires_at"].toDouble() > QDateTime::currentSecsSinceEpoch() + 60) {
        done(token, {}); return;
    }
    if (str(kick["refresh_token"]).isEmpty() || str(kick["client_id"]).isEmpty() ||
        str(kick["client_secret"]).isEmpty()) {
        done({}, "Sign in to Kick in Settings → Accounts."); return;
    }
    kickRefreshWaiters << done;
    if (kickRefreshing) return;
    kickRefreshing = true;
    emit changed();
    net->form(QUrl("https://id.kick.com/oauth/token"),
              {{"grant_type", "refresh_token"}, {"client_id", str(kick["client_id"])},
               {"client_secret", str(kick["client_secret"])}, {"refresh_token", str(kick["refresh_token"])}},
              [this](QJsonObject data, QString error) {
        kickRefreshing = false;
        if (error.isEmpty() && !str(data["access_token"]).isEmpty()) {
            kick["access_token"] = data["access_token"];
            if (!str(data["refresh_token"]).isEmpty()) kick["refresh_token"] = data["refresh_token"];
            kick["expires_at"] = double(QDateTime::currentSecsSinceEpoch() + data["expires_in"].toInt());
            kickIssue.clear();
            saveKick();
        } else {
            error = "Kick sign-in expired. Reconnect it in Settings → Accounts.";
            kickIssue = error;
        }
        const auto waiters = std::exchange(kickRefreshWaiters, {});
        for (const auto &waiter : waiters) waiter(error.isEmpty() ? str(kick["access_token"]) : QString(), error);
        emit changed();
    });
}

void Accounts::kickApi(QString endpoint, JsonCallback done, QByteArray method, QJsonObject body) {
    kickToken([this, endpoint, done, method, body](QString token, QString error) {
        if (!error.isEmpty()) { done({}, error); return; }
        net->json(QUrl("https://api.kick.com/public/v1/" + endpoint), done, method, body,
                  {{"Authorization", "Bearer " + token.toUtf8()}});
    });
}
} // namespace yc
