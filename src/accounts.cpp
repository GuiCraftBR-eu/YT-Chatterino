#include "accounts.hpp"
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#include <wincrypt.h>
#endif
namespace yc {
Vault::Vault(Config *c) : QObject(c), cfg(c) {}

QString Vault::target(const QString &provider) const {
    QString t = "YT-Chatterino/" + (provider == "twitch" ? QString("Twitch OAuth") : QString("Google OAuth"));
    if (qEnvironmentVariableIsSet("YTCHATTERINO_DATA_DIR") ||
        cfg->home != QDir::toNativeSeparators(QDir::homePath())) {
        t += '/' +
             QString(
                 QCryptographicHash::hash(cfg->home.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
    }
    return t;
}

void Vault::read(const QString &provider, BytesCallback done) {
#ifdef Q_OS_WIN
    QByteArray result;
    QFile f(cfg->home + "/.ytchatterino_" + provider + ".dpapi");
    if (f.open(QIODevice::ReadOnly)) {
        auto encrypted = f.readAll();
        DATA_BLOB in{DWORD(encrypted.size()), reinterpret_cast<BYTE *>(encrypted.data())}, out{};
        if (CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
            result = QByteArray(reinterpret_cast<char *>(out.pbData), out.cbData);
            LocalFree(out.pbData);
        }
    }
    if (result.isEmpty()) {
        PCREDENTIALW credential = nullptr;
        const auto name = target(provider).toStdWString();
        if (CredReadW(name.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
            result = QString::fromUtf16(reinterpret_cast<const char16_t *>(credential->CredentialBlob),
                                        credential->CredentialBlobSize / 2)
                         .toUtf8();
            CredFree(credential);
        }
    }
    QTimer::singleShot(0, this, [done, result] { done(result, {}); });
#else
    auto *p = new QProcess(this);
    auto timer = new QTimer(p);
    timer->setSingleShot(true);
    timer->start(3000);
    connect(timer, &QTimer::timeout, p, &QProcess::kill);
    connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [p, done](int, QProcess::ExitStatus) {
                auto b = p->readAllStandardOutput().trimmed();
                p->deleteLater();
                done(b, {});
            });
    connect(p, &QProcess::errorOccurred, this, [p, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            done({}, {});
        }
    });
    p->start("secret-tool", {"lookup", "service", target(provider), "username", "YT-Chatterino"});
#endif
}

void Vault::write(const QString &provider, const QByteArray &bytes, BytesCallback done) {
#ifdef Q_OS_WIN
    const auto name = target(provider).toStdWString();
    const auto value = QString::fromUtf8(bytes);
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<wchar_t *>(name.c_str());
    cred.CredentialBlobSize = value.size() * 2;
    cred.CredentialBlob = reinterpret_cast<BYTE *>(const_cast<ushort *>(value.utf16()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = const_cast<wchar_t *>(L"YT-Chatterino");
    bool ok = CredWriteW(&cred, 0);
    if (!ok) {
        DATA_BLOB in{DWORD(bytes.size()), reinterpret_cast<BYTE *>(const_cast<char *>(bytes.data()))}, out{};
        if (CryptProtectData(&in, L"YT-Chatterino sign-in", nullptr, nullptr, nullptr,
                             CRYPTPROTECT_UI_FORBIDDEN, &out)) {
            QSaveFile f(cfg->home + "/.ytchatterino_" + provider + ".dpapi");
            ok = f.open(QIODevice::WriteOnly) &&
                 f.write(reinterpret_cast<char *>(out.pbData), out.cbData) == out.cbData && f.commit();
            LocalFree(out.pbData);
        }
    } else {
        QFile::remove(cfg->home + "/.ytchatterino_" + provider + ".dpapi");
    }
    if (done) {
        done({}, ok ? QString()
                    : QString("Encrypted sign-in storage failed. This sign-in lasts for this session."));
    }
#else
    auto *p = new QProcess(this);
    connect(p, &QProcess::started, this, [p, bytes] {
        p->write(bytes);
        p->closeWriteChannel();
    });
    connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [p, done](int code, QProcess::ExitStatus) {
                p->deleteLater();
                if (done) {
                    done({},
                         code ? QString("Unlock a Secret Service / KWallet keyring to save sign-in securely.")
                              : QString());
                }
            });
    connect(p, &QProcess::errorOccurred, this, [p, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            if (done) {
                done({}, "Install libsecret and unlock your desktop keyring to save sign-in.");
            }
        }
    });
    p->start("secret-tool",
             {"store", "--label=YT-Chatterino", "service", target(provider), "username", "YT-Chatterino"});
#endif
}

Accounts::Accounts(Config *c, Network *n, Quota *q, QObject *parent)
    : QObject(parent), cfg(c), net(n), quota(q), vault(c) {
    deviceTimer.setSingleShot(true);
    connect(&deviceTimer, &QTimer::timeout, this, &Accounts::pollDevice);
    googleTimeout.setSingleShot(true);
    connect(&googleTimeout, &QTimer::timeout, this, &Accounts::cancelGoogle);
    connect(&callbackServer, &QTcpServer::newConnection, this, &Accounts::googleCallback);
}

void Accounts::load() {
    vault.read("twitch", [this](QByteArray b, QString) {
        twitch = QJsonDocument::fromJson(b).object();
        auto legacy = cfg->data["twitch_auth"].toObject();
        if (twitch.isEmpty() && !legacy.isEmpty()) {
            twitch = legacy;
            saveTwitch();
        }
        if (cfg->data.contains("twitch_auth")) {
            cfg->data.remove("twitch_auth");
            cfg->save();
        }
        vault.read("google", [this](QByteArray g, QString) {
            google = QJsonDocument::fromJson(g).object();
            if (str(google["channel_title"]).isEmpty()) {
                google["channel_title"] = google["name"];
            }
            loaded = true;
            emit changed();
            validateTwitch();
            if (!str(google.value("access_token")).isEmpty()) {
                googleToken([this](QString, QString error) {
                    googleIssue = error;
                    emit changed();
                });
            }
        });
    });
}

void Accounts::validateTwitch() {
    if (str(twitch["access_token"]).isEmpty()) {
        emit ready();
        return;
    }
    net->json(QUrl("https://id.twitch.tv/oauth2/validate"),
              [this](QJsonObject d, QString error) {
                  if (error.isEmpty() && str(d["client_id"]) == TwitchClient) {
                      twitchIssue.clear();
                      twitch["login"] = d["login"];
                      twitch["user_id"] = d["user_id"];
                      twitch["scope"] = d["scopes"];
                      twitch["expires_at"] =
                          double(QDateTime::currentSecsSinceEpoch() + d["expires_in"].toInt());
                      saveTwitch();
                      emit ready();
                      return;
                  }
                  if (!error.contains("401") && !error.isEmpty()) {
                      emit status(error);
                      emit ready();
                      return;
                  }
                  twitch["expires_at"] = 0;
                  twitchToken([this](QString, QString e) {
                      if (!e.isEmpty()) {
                          emit status(e);
                      }
                      emit ready();
                  });
              },
              "GET", {}, {{"Authorization", "OAuth " + str(twitch["access_token"]).toUtf8()}});
}

QStringList Accounts::names() const {
    QStringList list;
    for (const auto &s : {str(twitch["login"]), str(google["channel_title"]), str(google["title"])}) {
        if (!s.isEmpty()) {
            list << s;
        }
    }
    for (const auto &v : cfg->features()["nicknames"].toArray()) {
        list << v.toString();
    }
    return list;
}

static QSet<QString> grantedScopes(const QJsonObject &auth) {
    QSet<QString> scopes;
    const auto value = auth.value("scope");
    if (value.isArray()) {
        for (const auto &scope : value.toArray()) {
            scopes.insert(scope.toString());
        }
    } else {
        const auto list = value.toString().split(' ', Qt::SkipEmptyParts);
        scopes = QSet<QString>(list.begin(), list.end());
    }
    return scopes;
}

QSet<QString> Accounts::googleScopes() const {
    return grantedScopes(google);
}

bool Accounts::connected(const QString &provider) const {
    const auto &auth = provider == "twitch" ? twitch : google;
    const auto &issue = provider == "twitch" ? twitchIssue : googleIssue;
    return issue.isEmpty() && !str(auth.value("access_token")).isEmpty() &&
           auth.value("expires_at").toDouble() > QDateTime::currentSecsSinceEpoch();
}

bool Accounts::twitchAllows(const QSet<QString> &scopes) const {
    return connected("twitch") && (scopes - twitchScopes()).isEmpty();
}

bool Accounts::googleAllowsWriting() const {
    const auto scopes = googleScopes();
    return connected("google") && (scopes.contains("https://www.googleapis.com/auth/youtube.force-ssl") ||
                                   scopes.contains("https://www.googleapis.com/auth/youtube"));
}

bool Accounts::googleClientConfigured() const {
    auto path = cfg->features().value("google_client_file").toString();
    if (path.isEmpty()) {
        path = QCoreApplication::applicationDirPath() + "/google-oauth-client.json";
    }
    return !str(readObject(path).value("installed").toObject().value("client_id")).isEmpty();
}

QSet<QString> Accounts::fullTwitchScopes() {
    return {"chat:read",
            "user:write:chat",
            "user:read:emotes",
            "user:read:moderated_channels",
            "moderator:manage:chat_messages",
            "moderator:manage:banned_users",
            "channel:manage:broadcast"};
}

QSet<QString> Accounts::twitchScopes() const {
    QSet<QString> s;
    auto v = twitch.value("scope");
    if (v.isArray()) {
        for (const auto &e : v.toArray()) {
            s.insert(e.toString());
        }
    } else {
        for (const auto &e : v.toString().split(' ', Qt::SkipEmptyParts)) {
            s.insert(e);
        }
    }
    return s;
}

void Accounts::saveTwitch() {
    if (!persistenceEnabled) {
        emit changed();
        return;
    }
    vault.write("twitch", QJsonDocument(twitch).toJson(QJsonDocument::Compact),
                [this](QByteArray, QString e) {
                    if (!e.isEmpty()) {
                        emit status(e);
                    }
                });
    emit changed();
}

void Accounts::twitchToken(TokenCallback done) {
    if (twitch["access_token"].toString().isEmpty()) {
        done({}, "Sign in to Twitch from settings to connect this chat.");
        return;
    }
    if (twitch["expires_at"].toDouble() > QDateTime::currentSecsSinceEpoch() + 120) {
        done(str(twitch["access_token"]), {});
        return;
    }
    twitchRefreshWaiters << done;
    if (twitchRefreshing) {
        return;
    }
    twitchRefreshing = true;
    emit changed();
    done = [this](QString token, QString error) {
        twitchRefreshing = false;
        twitchIssue = error;
        emit changed();
        auto waiting = std::exchange(twitchRefreshWaiters, {});
        for (const auto &cb : waiting) {
            cb(token, error);
        }
    };
    auto auth = twitch;
    QString refresh = str(auth["refresh_token"]);
    if (refresh.isEmpty()) {
        net->bytes(QUrl("https://id.twitch.tv/oauth2/validate"),
                   [this, done](QByteArray b, QString e) {
                       auto d = QJsonDocument::fromJson(b).object();
                       if (e.isEmpty() && str(d["client_id"]) == TwitchClient) {
                           twitch["login"] = d["login"];
                           twitch["user_id"] = d["user_id"];
                           twitch["scope"] = d["scopes"];
                           twitch["expires_at"] =
                               double(QDateTime::currentSecsSinceEpoch() + d["expires_in"].toInt());
                           saveTwitch();
                           done(str(twitch["access_token"]), {});
                       } else {
                           done({}, "Twitch sign-in expired. Use Twitch sign-in / permissions in Settings.");
                       }
                   },
                   "GET", {}, {{"Authorization", "OAuth " + str(auth["access_token"]).toUtf8()}});
        return;
    }
    net->form(QUrl("https://id.twitch.tv/oauth2/token"),
              {{"client_id", TwitchClient}, {"grant_type", "refresh_token"}, {"refresh_token", refresh}},
              [this, done](QJsonObject d, QString e) {
                  if (!e.isEmpty() || str(d["access_token"]).isEmpty()) {
                      done({}, "Twitch sign-in expired. Use Twitch sign-in / permissions in Settings.");
                      return;
                  }
                  for (auto i = d.begin(); i != d.end(); ++i) {
                      twitch[i.key()] = i.value();
                  }
                  twitch["expires_at"] = double(QDateTime::currentSecsSinceEpoch() + d["expires_in"].toInt());
                  saveTwitch();
                  done(str(twitch["access_token"]), {});
              });
}

void Accounts::requireTwitch(const QSet<QString> &scopes, TokenCallback done) {
    auto existing = twitchScopes();
    bool enough = !twitch["access_token"].toString().isEmpty();
    for (const auto &s : scopes) {
        enough &= existing.contains(s);
    }
    if (enough) {
        twitchToken([this, scopes, done](QString token, QString error) {
            if (error.isEmpty()) {
                done(token, {});
            } else {
                twitchWaiters << done;
                signInTwitch(scopes);
            }
        });
        return;
    }
    twitchWaiters << done;
    signInTwitch(scopes);
}

void Accounts::signInTwitch(const QSet<QString> &requested) {
    if (twitchBusy) {
        if (twitchDialog) {
            twitchDialog->raise();
        }
        return;
    }
    twitchBusy = true;
    emit changed();
    QSet<QString> scopes = twitchScopes();
    scopes.unite(requested);
    scopes.insert("chat:read");
    QStringList scopeList(scopes.begin(), scopes.end());
    scopeList.sort();
    net->form(
        QUrl("https://id.twitch.tv/oauth2/device"),
        {{"client_id", TwitchClient}, {"scopes", scopeList.join(' ')}}, [this](QJsonObject d, QString e) {
            if (!e.isEmpty() || str(d["device_code"]).isEmpty()) {
                finishTwitch({}, e.isEmpty() ? "Twitch did not return a sign-in code." : e);
                return;
            }
            deviceCode = str(d["device_code"]);
            deviceTimer.setInterval(qMax(3, d["interval"].toInt(5)) * 1000);
            auto *dialog = new QDialog(window);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setWindowTitle(cfg->T("Twitch sign-in"));
            auto *layout = new QVBoxLayout(dialog);
            auto *help = new QLabel(
                cfg->T("Open Twitch, enter this code, then approve the requested permissions."), dialog);
            help->setWordWrap(true);
            layout->addWidget(help);
            auto *code = new QLineEdit(str(d["user_code"]), dialog);
            code->setReadOnly(true);
            code->setAlignment(Qt::AlignCenter);
            code->setFont(QFont("Segoe UI", 20, QFont::Bold));
            layout->addWidget(code);
            auto *buttons = new QDialogButtonBox(dialog);
            auto *open = buttons->addButton(cfg->T("Open Twitch"), QDialogButtonBox::ActionRole);
            auto *copy = buttons->addButton(cfg->T("Copy code"), QDialogButtonBox::ActionRole);
            buttons->addButton(cfg->T("Cancel"), QDialogButtonBox::RejectRole);
            layout->addWidget(buttons);
            QString url = str(d["verification_uri"]);
            if (url.isEmpty()) {
                url = "https://www.twitch.tv/activate";
            }
            connect(open, &QPushButton::clicked, dialog, [url] { QDesktopServices::openUrl(QUrl(url)); });
            connect(copy, &QPushButton::clicked, dialog,
                    [code] { QApplication::clipboard()->setText(code->text()); });
            connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
            connect(dialog, &QDialog::rejected, this, [this] {
                if (twitchBusy) {
                    finishTwitch({}, "Twitch sign-in cancelled.");
                }
            });
            twitchDialog = dialog;
            dialog->resize(440, 200);
            dialog->show();
            darkTitlebar(dialog);
            QDesktopServices::openUrl(QUrl(url));
            deviceTimer.start();
        });
}

void Accounts::pollDevice() {
    if (!twitchBusy || deviceCode.isEmpty()) {
        return;
    }
    net->form(QUrl("https://id.twitch.tv/oauth2/token"),
              {{"client_id", TwitchClient},
               {"device_code", deviceCode},
               {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}},
              [this](QJsonObject d, QString e) {
                  if (!twitchBusy) {
                      return;
                  }
                  QString kind = str(d["error"]), message = str(d["message"]);
                  if (kind == "authorization_pending" || message.contains("pending", Qt::CaseInsensitive)) {
                      deviceTimer.start();
                      return;
                  }
                  if (kind == "slow_down") {
                      deviceTimer.start(deviceTimer.interval() + 5000);
                      return;
                  }
                  if (!e.isEmpty() || str(d["access_token"]).isEmpty()) {
                      finishTwitch({}, e.isEmpty() ? "Twitch sign-in failed." : e);
                      return;
                  }
                  net->bytes(
                      QUrl("https://id.twitch.tv/oauth2/validate"),
                      [this, d](QByteArray raw, QString error) {
                          auto details = QJsonDocument::fromJson(raw).object();
                          if (!error.isEmpty() || str(details["client_id"]) != TwitchClient) {
                              finishTwitch({}, error.isEmpty() ? "Invalid Twitch account response." : error);
                              return;
                          }
                          auto a = d;
                          a["login"] = details["login"];
                          a["user_id"] = details["user_id"];
                          a["client_id"] = TwitchClient;
                          a["scope"] = details["scopes"];
                          a["expires_at"] =
                              double(QDateTime::currentSecsSinceEpoch() + details["expires_in"].toInt());
                          finishTwitch(a);
                      },
                      "GET", {}, {{"Authorization", "OAuth " + str(d["access_token"]).toUtf8()}});
              });
}

void Accounts::finishTwitch(QJsonObject auth, QString error) {
    deviceTimer.stop();
    deviceCode.clear();
    twitchBusy = false;
    if (twitchDialog) {
        twitchDialog->close();
        twitchDialog.clear();
    }
    if (error.isEmpty()) {
        twitchIssue.clear();
        twitch = auth;
        saveTwitch();
        emit ready();
    } else {
        emit status(error);
    }
    auto waiting = std::exchange(twitchWaiters, {});
    for (const auto &cb : waiting) {
        cb(str(auth["access_token"]), error);
    }
    emit changed();
}

QJsonObject Accounts::clientFile(QString *error) {
    QString path = cfg->features()["google_client_file"].toString();
    if (path.isEmpty()) {
        path = QCoreApplication::applicationDirPath() + "/google-oauth-client.json";
    }
    auto d = readObject(path)["installed"].toObject();
    if (str(d["client_id"]).isEmpty()) {
        *error = "Choose a Google Desktop OAuth JSON file in Settings → Accounts.";
    }
    return d;
}

void Accounts::signInGoogle(TokenCallback done) {
    if (done) {
        googleWaiters << done;
    }
    if (googleBusy) {
        return;
    }
    QString error;
    googleClient = clientFile(&error);
    if (!error.isEmpty()) {
        finishGoogle({}, error);
        return;
    }
    if (!callbackServer.listen(QHostAddress::LocalHost, 0)) {
        finishGoogle({}, "Could not open the local Google callback.");
        return;
    }
    googleBusy = true;
    emit changed();
    QByteArray random(64, 0);
    for (auto &b : random) {
        b = char(QRandomGenerator::system()->generate() & 255);
    }
    verifier =
        QString::fromLatin1(random.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    state = QUuid::createUuid().toString(QUuid::WithoutBraces);
    redirect = "http://127.0.0.1:" + QString::number(callbackServer.serverPort()) + "/";
    QString challenge =
        QString::fromLatin1(QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256)
                                .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    auto url = Network::query("https://accounts.google.com/o/oauth2/v2/auth",
                              {{"client_id", str(googleClient["client_id"])},
                               {"redirect_uri", redirect},
                               {"response_type", "code"},
                               {"scope", "https://www.googleapis.com/auth/youtube.force-ssl"},
                               {"state", state},
                               {"code_challenge", challenge},
                               {"code_challenge_method", "S256"},
                               {"access_type", "offline"},
                               {"prompt", "consent"}});
    googleTimeout.start(180000);
    QDesktopServices::openUrl(url);
    emit status("Complete Google sign-in in your browser.");
}

void Accounts::googleCallback() {
    while (callbackServer.hasPendingConnections()) {
        auto *socket = callbackServer.nextPendingConnection();
        auto buffer = QSharedPointer<QByteArray>::create();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            buffer->append(socket->readAll());
            if (buffer->size() > 16384) {
                socket->disconnectFromHost();
                return;
            }
            if (!buffer->contains("\r\n\r\n")) {
                return;
            }
            auto first = buffer->split('\n').value(0).split(' ');
            QUrl u("http://127.0.0.1" + QString::fromUtf8(first.value(1)));
            QUrlQuery query(u);
            bool good = socket->peerAddress().isLoopback() && googleBusy &&
                        query.queryItemValue("state") == state && !query.queryItemValue("code").isEmpty();
            QByteArray body = good ? "Sign-in received. You may close this page and return to YT-Chatterino."
                                   : "Invalid sign-in callback.";
            socket->write(QByteArray(good ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 400 Bad Request\r\n") +
                          "Content-Type: text/plain; charset=utf-8\r\nConnection: close\r\nContent-Length: " +
                          QByteArray::number(body.size()) + "\r\n\r\n" + body);
            socket->disconnectFromHost();
            if (!good) {
                return;
            }
            callbackServer.close();
            googleTimeout.stop();
            QMap<QString, QString> params{{"client_id", str(googleClient["client_id"])},
                                          {"code", query.queryItemValue("code")},
                                          {"redirect_uri", redirect},
                                          {"grant_type", "authorization_code"},
                                          {"code_verifier", verifier}};
            if (googleClient.contains("client_secret")) {
                params["client_secret"] = str(googleClient["client_secret"]);
            }
            net->form(QUrl("https://oauth2.googleapis.com/token"), params, [this](QJsonObject d, QString e) {
                if (!e.isEmpty()) {
                    finishGoogle({}, e);
                    return;
                }
                d["expires_at"] = double(QDateTime::currentSecsSinceEpoch() + d["expires_in"].toInt());
                d["client_id"] = googleClient["client_id"];
                google = d;
                net->json(Network::query("https://www.googleapis.com/youtube/v3/channels",
                                         {{"part", "snippet"}, {"mine", "true"}}),
                          [this, d](QJsonObject info, QString error) {
                              auto a = d;
                              if (error.isEmpty()) {
                                  auto item = firstItem(info["items"].toArray()).toObject();
                                  a["channel_id"] = item["id"];
                                  a["channel_title"] = at(item, {"snippet", "title"});
                              }
                              quota->add(1);
                              finishGoogle(a);
                          },
                          "GET", {}, {{"Authorization", "Bearer " + str(d["access_token"]).toUtf8()}});
            });
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void Accounts::finishGoogle(QJsonObject auth, QString error) {
    callbackServer.close();
    googleTimeout.stop();
    googleBusy = false;
    verifier.clear();
    state.clear();
    if (error.isEmpty()) {
        googleIssue.clear();
        google = auth;
        if (persistenceEnabled) {
            vault.write("google", QJsonDocument(auth).toJson(QJsonDocument::Compact),
                        [this](QByteArray, QString e) {
                            if (!e.isEmpty()) {
                                emit status(e);
                            }
                        });
        }
    } else {
        emit status(error);
    }
    auto waiting = std::exchange(googleWaiters, {});
    for (const auto &cb : waiting) {
        cb(str(auth["access_token"]), error);
    }
    emit changed();
}

void Accounts::cancelGoogle() {
    if (googleBusy) {
        finishGoogle({}, "Google sign-in cancelled or timed out.");
    }
}

void Accounts::googleToken(TokenCallback done) {
    if (str(google["access_token"]).isEmpty()) {
        done({}, "Sign in to YouTube in Settings → Accounts.");
        return;
    }
    if (google["expires_at"].toDouble() > QDateTime::currentSecsSinceEpoch() + 120) {
        done(str(google["access_token"]), {});
        return;
    }
    googleRefreshWaiters << done;
    if (googleRefreshing) {
        return;
    }
    googleRefreshing = true;
    emit changed();
    done = [this](QString token, QString error) {
        googleRefreshing = false;
        googleIssue = error;
        emit changed();
        auto waiting = std::exchange(googleRefreshWaiters, {});
        for (const auto &cb : waiting) {
            cb(token, error);
        }
    };
    QString error;
    auto client = clientFile(&error);
    if (!error.isEmpty()) {
        done({}, error);
        return;
    }
    if (str(client["client_id"]) != str(google["client_id"]) && !str(google["client_id"]).isEmpty()) {
        done({}, "The saved Google sign-in uses another OAuth client. Sign in again.");
        return;
    }
    QMap<QString, QString> form{{"client_id", str(client["client_id"])},
                                {"grant_type", "refresh_token"},
                                {"refresh_token", str(google["refresh_token"])}};
    if (client.contains("client_secret")) {
        form["client_secret"] = str(client["client_secret"]);
    }
    net->form(QUrl("https://oauth2.googleapis.com/token"), form, [this, done](QJsonObject d, QString e) {
        if (!e.isEmpty() || str(d.value("access_token")).isEmpty()) {
            done({}, "YouTube sign-in expired. Sign in again in Settings → Accounts.");
            return;
        }
        for (auto i = d.begin(); i != d.end(); ++i) {
            google[i.key()] = i.value();
        }
        google["expires_at"] = double(QDateTime::currentSecsSinceEpoch() + d["expires_in"].toInt());
        if (persistenceEnabled) {
            vault.write("google", QJsonDocument(google).toJson(QJsonDocument::Compact));
        }
        done(str(google["access_token"]), {});
    });
}

void Accounts::helix(QString endpoint, JsonCallback done, QByteArray method, QMap<QString, QString> params,
                     QJsonObject body) {
    twitchToken([=, this](QString token, QString error) {
        if (!error.isEmpty()) {
            done({}, error);
            return;
        }
        net->json(Network::query("https://api.twitch.tv/helix/" + endpoint, params), done, method, body,
                  {{"Client-Id", TwitchClient.toUtf8()}, {"Authorization", "Bearer " + token.toUtf8()}});
    });
}

void Accounts::youtube(QString endpoint, JsonCallback done, QByteArray method, QMap<QString, QString> params,
                       QJsonObject body, int units) {
    googleToken([=, this](QString token, QString error) {
        if (!error.isEmpty()) {
            done({}, error);
            return;
        }
        net->json(Network::query("https://www.googleapis.com/youtube/v3/" + endpoint, params),
                  [=, this](QJsonObject d, QString e) {
                      quota->add(units);
                      done(d, e);
                  },
                  method, body, {{"Authorization", "Bearer " + token.toUtf8()}});
    });
}
} // namespace yc
