#pragma once
#include <QtCore>
#include <QtGui>
#include <QtWidgets>
#include <functional>

namespace yc {
inline const QString TwitchClient = QStringLiteral("vbyiljlhljby8n1uwpjkpo4h5a6iiq");
inline const QString Repo = QStringLiteral("GuiCraftBR-eu/YT-Chatterino");
using JsonCallback = std::function<void(QJsonObject, QString)>;
using BytesCallback = std::function<void(QByteArray, QString)>;
using TokenCallback = std::function<void(QString, QString)>;
QJsonObject object(const QJsonValue &v);

inline QJsonValue firstItem(const QJsonArray &a) {
    return a.isEmpty() ? QJsonValue() : a[0];
}

QString str(const QJsonValue &v);
QJsonValue at(const QJsonObject &o, std::initializer_list<QString> path);
QJsonObject readObject(const QString &path);
bool writeObject(const QString &path, const QJsonObject &o);
QJsonObject embeddedJson(const QString &html, const QString &marker);
void walk(const QJsonValue &v, const std::function<void(const QJsonObject &)> &fn);
QString textOf(const QJsonValue &v);
QStringList imageUrls(const QJsonValue &v);
QString videoId(const QString &input);
QString twitchLogin(const QString &input);
QStringList words(const QString &input);
bool wordMatch(const QString &text, const QString &word);
int codepointOffset(const QString &text, int cp);
int characterCount(const QString &text);
QString stripAt(QString value);
QString emoteName(QString value);
QIcon platformIcon(const QString &platform);
void darkTitlebar(QWidget *window);

class Config : public QObject {
    Q_OBJECT
  public:
    explicit Config(QString dir = {}, QObject *parent = nullptr);
    QString home;
    QJsonObject data;
    QString T(const QString &key, const QMap<QString, QString> &args = {}) const;
    QString language() const;
    QJsonObject appearance() const;
    QJsonObject features() const;
    QJsonObject feature(const QString &name) const;
    void setAppearance(const QJsonObject &appearance);
    void setFeature(const QString &name, const QJsonObject &settings);
    void save();
    int cap(const QString &key = {}) const;
    QFont messageFont() const;
    QFont usernameFont() const;
    QString time(const QDateTime &stamp) const;
    QString date(const QDateTime &stamp) const;
    QString date(const QString &iso) const;
    QString number(qint64 value) const;
  signals:
    void appearanceChanged();
    void languageChanged();

  private:
    QJsonObject catalog;
};

struct Span {
    int start = 0, end = 0;
    QString code;
    QStringList urls;
    bool gif = false;
};

struct Badge {
    QString id, version, label, role;
    QStringList urls;
};

struct Message {
    QString id, source, name, login, uid, text, color, avatar, special, apiId, webId;
    QDateTime stamp;
    QVector<Span> emotes;
    QVector<Badge> badges;
    QJsonObject reply;
    QString badgeInfo;
    qint64 seq = 0;
    bool history = false, deleted = false, own = false, system = false;
    QJsonObject json() const;
    static Message fromJson(const QJsonObject &o);
};

struct ParsedIrc {
    QString channel, command, params;
    QJsonObject tags;
    Message message;
};

ParsedIrc parseIrc(QString line);
Message parseYouTube(const QJsonObject &o, const QString &kind, bool historical = false);
QSet<QString> roles(const Message &m);
QColor userColor(const Message &m);
QColor highlight(const Message &m, const Config &cfg, const QStringList &ownNames = {},
                 bool *mention = nullptr);
bool accepts(const Message &m, const QJsonObject &filter);
QString threadRoot(const Message &m, const QHash<QString, Message> &messages);
QVector<Message> collectThread(const QVector<Message> &history, const Message &selected, QString *root);
QString browserUrl(const QString &platform, const QString &id, const QString &destination);
bool validVersion(const QString &version);
bool versionNewer(const QString &candidate, const QString &current);

class Quota : public QObject {
    Q_OBJECT
  public:
    explicit Quota(Config *cfg);
    QJsonObject daily;
    Config *cfg;
    QString day() const;
    void add(int units);
    int used() const;
    int remaining() const;
    void roll();
  signals:
    void changed();
};
} // namespace yc
Q_DECLARE_METATYPE(yc::Message)
