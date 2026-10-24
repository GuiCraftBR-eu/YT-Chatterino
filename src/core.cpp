#include "core.hpp"
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace yc {
QJsonObject object(const QJsonValue &v) {
    return v.toObject();
}

QString str(const QJsonValue &v) {
    return v.isString() ? v.toString() : v.isDouble() ? QString::number(v.toDouble(), 'f', 0) : QString();
}

QJsonValue at(const QJsonObject &o, std::initializer_list<QString> path) {
    QJsonValue v(o);
    for (const auto &p : path) {
        v = v.toObject().value(p);
    }
    return v;
}

QJsonObject readObject(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

bool writeObject(const QString &path, const QJsonObject &o) {
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(o).toJson()) >= 0 && f.commit();
}

QJsonObject embeddedJson(const QString &html, const QString &marker) {
    qsizetype pos = html.indexOf(marker);
    while (pos >= 0) {
        auto begin = html.indexOf('{', pos + marker.size());
        if (begin < 0 || begin - pos > 150) {
            pos = html.indexOf(marker, pos + marker.size());
            continue;
        }
        int depth = 0;
        bool quoted = false, escape = false;
        for (qsizetype i = begin; i < html.size(); ++i) {
            const auto c = html[i];
            if (quoted) {
                if (escape) {
                    escape = false;
                } else if (c == '\\') {
                    escape = true;
                } else if (c == '"') {
                    quoted = false;
                }
            } else if (c == '"') {
                quoted = true;
            } else if (c == '{') {
                ++depth;
            } else if (c == '}' && !--depth) {
                auto doc = QJsonDocument::fromJson(html.mid(begin, i - begin + 1).toUtf8());
                if (doc.isObject()) {
                    return doc.object();
                }
                break;
            }
        }
        pos = html.indexOf(marker, pos + marker.size());
    }
    return {};
}

void walk(const QJsonValue &v, const std::function<void(const QJsonObject &)> &fn) {
    if (v.isObject()) {
        const auto o = v.toObject();
        fn(o);
        for (auto i = o.begin(); i != o.end(); ++i) {
            walk(i.value(), fn);
        }
    } else if (v.isArray()) {
        for (const auto &i : v.toArray()) {
            walk(i, fn);
        }
    }
}

QString textOf(const QJsonValue &v) {
    if (v.isString()) {
        return v.toString();
    }
    const auto o = v.toObject();
    if (o.contains("simpleText")) {
        return o["simpleText"].toString();
    }
    if (o.contains("content")) {
        return o["content"].toString();
    }
    QString result;
    for (const auto &run : o["runs"].toArray()) {
        result += run.toObject()["text"].toString();
    }
    return result;
}

QStringList imageUrls(const QJsonValue &v) {
    QVector<QJsonObject> thumbs;
    const auto o = v.toObject();
    for (const auto &t : o["thumbnails"].toArray()) {
        thumbs.append(t.toObject());
    }
    std::sort(thumbs.begin(), thumbs.end(),
              [](const auto &a, const auto &b) { return a["width"].toInt() > b["width"].toInt(); });
    QStringList result;
    for (const auto &t : thumbs) {
        QString url = t["url"].toString();
        if (url.startsWith("//")) {
            url = "https:" + url;
        }
        if (url.startsWith("https://") && !result.contains(url)) {
            result << url;
        }
    }
    return result;
}

QString videoId(const QString &input) {
    QString s = input.trimmed();
    s.remove(QRegularExpression("^\"|\"$"));
    static QRegularExpression id("^[a-zA-Z0-9_-]{11}$");
    if (id.match(s).hasMatch()) {
        return s;
    }
    QUrl u(s.contains("://") ? s : "https://" + s);
    QString host = u.host().toLower();
    QString candidate;
    if (host == "youtu.be" || host == "www.youtu.be") {
        candidate = u.path().split('/', Qt::SkipEmptyParts).value(0);
    } else if (host == "youtube.com" || host.endsWith(".youtube.com")) {
        candidate = QUrlQuery(u).queryItemValue("v");
        if (candidate.isEmpty()) {
            auto m = QRegularExpression("^/(?:live|shorts|embed)/([a-zA-Z0-9_-]{11})").match(u.path());
            candidate = m.captured(1);
        }
    }
    return id.match(candidate).hasMatch() ? candidate : QString();
}

QString twitchLogin(const QString &input) {
    QString s = input.trimmed(), candidate;
    if (s.startsWith('@')) {
        candidate = s.mid(1);
    } else if (s.startsWith("twitch:", Qt::CaseInsensitive)) {
        candidate = s.mid(7).trimmed();
    } else {
        QUrl u(s.contains("://") ? s : "https://" + s);
        if (u.host() != "twitch.tv" && u.host() != "www.twitch.tv" && u.host() != "m.twitch.tv") {
            return {};
        }
        auto p = u.path().split('/', Qt::SkipEmptyParts);
        candidate = p.value(p.value(0) == "popout" ? 1 : 0);
        if (QStringList{"directory", "downloads", "jobs", "login", "p", "settings", "signup", "subscriptions",
                        "turbo", "videos", "wallet"}
                .contains(candidate)) {
            return {};
        }
    }
    return QRegularExpression("^[A-Za-z0-9_]{1,25}$").match(candidate).hasMatch() ? candidate.toLower()
                                                                                  : QString();
}

QStringList words(const QString &input) {
    auto list = input.split(QRegularExpression("[,\\n]"), Qt::SkipEmptyParts);
    for (auto &s : list) {
        s = s.trimmed();
    }
    list.removeAll({});
    return list;
}

QString stripAt(QString value) {
    value = value.trimmed();
    while (value.startsWith('@')) {
        value.remove(0, 1);
    }
    return value.toCaseFolded();
}

bool wordMatch(const QString &text, const QString &word) {
    const auto w = stripAt(word);
    return !w.isEmpty() &&
           QRegularExpression(
               "(?<![\\p{L}\\p{N}_])@?" + QRegularExpression::escape(w) + "(?![\\p{L}\\p{N}_])",
               QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption)
               .match(text)
               .hasMatch();
}

int codepointOffset(const QString &text, int cp) {
    int i = 0, n = 0;
    while (i < text.size() && n++ < cp) {
        if (text[i].isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate()) {
            ++i;
        }
        ++i;
    }
    return i;
}

int characterCount(const QString &text) {
    return text.toUcs4().size();
}

QIcon platformIcon(const QString &platform) {
    QPixmap p(24, 24);
    p.fill(Qt::transparent);
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(Qt::NoPen);
    if (platform == "youtube") {
        g.setBrush(QColor("#ff0033"));
        g.drawRoundedRect(QRectF(1, 4, 22, 16), 4, 4);
        g.setBrush(Qt::white);
        g.drawPolygon(QPolygonF{{10, 8}, {10, 16}, {17, 12}});
    } else if (platform == "twitch") {
        g.setBrush(QColor("#9146ff"));
        g.drawPolygon(
            QPolygonF{{4, 1}, {23, 1}, {23, 16}, {17, 22}, {12, 22}, {8, 25}, {8, 22}, {1, 22}, {1, 5}});
        g.setBrush(Qt::black);
        g.drawPolygon(QPolygonF{{6, 4}, {20, 4}, {20, 14}, {15, 19}, {11, 19}, {8, 22}, {8, 19}, {6, 19}});
        g.setBrush(QColor("#9146ff"));
        g.drawRect(10, 7, 3, 7);
        g.drawRect(15, 7, 3, 7);
    } else {
        g.setPen(QColor("#4a9cdb"));
        g.setFont(QFont("Segoe UI", 18, QFont::Bold));
        g.drawText(p.rect(), Qt::AlignCenter, "≋");
    }
    return QIcon(p);
}

void darkTitlebar(QWidget *window) {
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    BOOL yes = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &yes, sizeof(yes));
    COLORREF black = RGB(0, 0, 0);
    DwmSetWindowAttribute(hwnd, 35, &black, sizeof(black));
#else
    Q_UNUSED(window)
#endif
}

Config::Config(QString dir, QObject *parent) : QObject(parent) {
    if (dir.isEmpty()) {
        dir = qEnvironmentVariable("YTCHATTERINO_DATA_DIR");
    }
    if (dir.isEmpty()) {
        dir = QDir::homePath();
    }
    home = QDir::toNativeSeparators(QDir(dir).absolutePath());
    QDir().mkpath(home);
    data = readObject(home + "/.ytchatterino.json");
    QFile f(":/translations-ptbr.json");
    if (f.open(QIODevice::ReadOnly)) {
        catalog = QJsonDocument::fromJson(f.readAll()).object();
    }
    QFile native(":/native-ptbr.json");
    if (native.open(QIODevice::ReadOnly)) {
        auto extra = QJsonDocument::fromJson(native.readAll()).object();
        for (auto i = extra.begin(); i != extra.end(); ++i) {
            catalog[i.key()] = i.value();
        }
    }
    auto a = data["appearance"].toObject();
    const int legacy = qBound(7, a["font_size"].toInt(12), 48);
    if (!a.contains("message_font_size")) {
        a["message_font_size"] = legacy;
    }
    if (!a.contains("username_font_size")) {
        a["username_font_size"] = legacy;
    }
    if (!a.contains("emote_size")) {
        a["emote_size"] = 36;
    }
    if (!a.contains("badge_size")) {
        a["badge_size"] = 18;
    }
    if (!a.contains("show_timestamps")) {
        a["show_timestamps"] = true;
    }
    if (!a.contains("alternate_rows")) {
        a["alternate_rows"] = true;
    }
    if (!a.contains("date_format")) {
        a["date_format"] = "DD/MM/YYYY";
    }
    QString tf = a["time_format"].toString("HH:mm");
    tf.replace("AM/PM", "am/pm");
    a["time_format"] = tf;
    data["appearance"] = a;
}

QString Config::language() const {
    return data["language"].toString() == "pt-BR" ? "pt-BR" : "en";
}

QString Config::T(const QString &key, const QMap<QString, QString> &args) const {
    QString s = language() == "pt-BR" ? catalog[key].toString(key) : key;
    for (auto i = args.begin(); i != args.end(); ++i) {
        s.replace('{' + i.key() + '}', i.value());
    }
    return s;
}

QJsonObject Config::appearance() const {
    return data["appearance"].toObject();
}

QJsonObject Config::features() const {
    return data["features"].toObject();
}

QJsonObject Config::feature(const QString &name) const {
    return features()[name].toObject();
}

void Config::setAppearance(const QJsonObject &a) {
    data["appearance"] = a;
    save();
    emit appearanceChanged();
}

void Config::setFeature(const QString &name, const QJsonObject &settings) {
    auto f = features();
    f[name] = settings;
    data["features"] = f;
    save();
    emit appearanceChanged();
}

void Config::save() {
    writeObject(home + "/.ytchatterino.json", data);
}

int Config::cap(const QString &key) const {
    const auto v = data["chat_message_limits"].toObject().value(key);
    int n = v.isUndefined() ? data["message_limit"].toInt(2000) : v.toInt(2000);
    return qBound(100, n, 20000);
}

QFont Config::messageFont() const {
    return QFont("Segoe UI", qBound(7, appearance()["message_font_size"].toInt(12), 48));
}

QFont Config::usernameFont() const {
    QFont f("Segoe UI", qBound(7, appearance()["username_font_size"].toInt(12), 48));
    f.setBold(true);
    return f;
}

QString Config::time(const QDateTime &stamp) const {
    if (!stamp.isValid()) {
        return {};
    }
    QString fmt = appearance()["time_format"].toString("HH:mm");
    bool twelve = fmt.contains("am/pm", Qt::CaseInsensitive), seconds = fmt.contains(":ss");
    const auto t = stamp.toLocalTime().time();
    if (twelve) {
        return QString::number(t.hour() % 12 ? t.hour() % 12 : 12) + t.toString(seconds ? ":mm:ss" : ":mm") +
               (t.hour() < 12 ? " am" : " pm");
    }
    return t.toString(seconds ? "HH:mm:ss" : "HH:mm");
}

QString Config::date(const QDateTime &stamp) const {
    QString f = appearance()["date_format"].toString("DD/MM/YYYY");
    f.replace("DD", "dd");
    f.replace("YYYY", "yyyy");
    return stamp.isValid() ? stamp.toLocalTime().date().toString(f) : QString();
}

QString Config::date(const QString &iso) const {
    auto dt = QDateTime::fromString(iso, Qt::ISODate);
    if (!dt.isValid()) {
        dt = QDateTime(QDate::fromString(iso.left(10), Qt::ISODate), QTime(12, 0));
    }
    return date(dt);
}

QString Config::number(qint64 value) const {
    return QLocale(language() == "pt-BR" ? QLocale::Portuguese : QLocale::English).toString(value);
}

QJsonObject Message::json() const {
    QJsonArray es, bs;
    for (const auto &e : emotes) {
        es.append(QJsonObject{{"start", e.start},
                              {"end", e.end},
                              {"code", e.code},
                              {"urls", QJsonArray::fromStringList(e.urls)},
                              {"gif", e.gif}});
    }
    for (const auto &b : badges) {
        bs.append(QJsonObject{{"set_id", b.id},
                              {"version", b.version},
                              {"label", b.label},
                              {"role", b.role},
                              {"urls", QJsonArray::fromStringList(b.urls)}});
    }
    return {{"id", id},
            {"source", source},
            {"name", name},
            {"login", login},
            {"user_id", uid},
            {"text", text},
            {"color", color},
            {"avatar", avatar},
            {"special", special},
            {"reply", reply},
            {"badge_info", badgeInfo},
            {"native_emotes", es},
            {"badge_ids", bs},
            {"sent_at", stamp.isValid() ? stamp.toMSecsSinceEpoch() / 1000.0 : 0},
            {"_seq", seq},
            {"deleted", deleted},
            {"_history", history},
            {"_own", own},
            {"system", system},
            {"api_id", apiId},
            {"web_id", webId}};
}

Message Message::fromJson(const QJsonObject &o) {
    Message m;
    m.id = str(o["id"]);
    m.source = str(o["source"]);
    m.name = str(o["name"]);
    m.login = str(o["login"]);
    m.uid = str(o["user_id"]);
    m.text = str(o["text"]);
    m.color = str(o["color"]);
    m.avatar = str(o["avatar"]);
    m.special = str(o["special"]);
    m.reply = o["reply"].toObject();
    m.badgeInfo = str(o["badge_info"]);
    m.seq = o["_seq"].toInteger();
    m.deleted = o["deleted"].toBool();
    m.history = o["_history"].toBool();
    m.own = o["_own"].toBool();
    m.system = o["system"].toBool();
    m.apiId = str(o["api_id"]);
    m.webId = str(o["web_id"]);
    double ts = o["sent_at"].toDouble();
    if (ts > 0) {
        m.stamp = QDateTime::fromMSecsSinceEpoch(qint64(ts * 1000));
    }
    for (const auto &v : o["native_emotes"].toArray()) {
        auto e = v.toObject();
        Span s;
        s.start = e["start"].toInt();
        s.end = e["end"].toInt();
        s.code = str(e["code"]);
        s.gif = e["gif"].toBool();
        for (const auto &u : e["urls"].toArray()) {
            s.urls << u.toString();
        }
        m.emotes << s;
    }
    for (const auto &v : o["badge_ids"].toArray()) {
        auto b = v.toObject();
        Badge badge;
        badge.id = str(b["set_id"]);
        badge.version = str(b["version"]);
        badge.label = str(b["label"]);
        badge.role = str(b["role"]);
        for (const auto &u : b["urls"].toArray()) {
            badge.urls << u.toString();
        }
        m.badges << badge;
    }
    return m;
}

static QString decodeTag(QString s) {
    QString out;
    for (int i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const auto c = s[++i];
            out += c == 's' ? ' ' : c == ':' ? ';' : c == 'r' ? '\r' : c == 'n' ? '\n' : c;
        } else {
            out += s[i];
        }
    }
    return out;
}

ParsedIrc parseIrc(QString line) {
    ParsedIrc p;
    QString prefix;
    if (line.startsWith('@')) {
        int space = line.indexOf(' ');
        for (const auto &tag : line.mid(1, space - 1).split(';')) {
            int eq = tag.indexOf('=');
            p.tags[eq < 0 ? tag : tag.left(eq)] = eq < 0 ? QString() : decodeTag(tag.mid(eq + 1));
        }
        line = line.mid(space + 1);
    }
    if (line.startsWith(':')) {
        int space = line.indexOf(' ');
        prefix = line.mid(1, space - 1);
        line = line.mid(space + 1);
    }
    int space = line.indexOf(' ');
    p.command = space < 0 ? line : line.left(space);
    p.params = space < 0 ? QString() : line.mid(space + 1);
    int bodyPos = p.params.indexOf(" :");
    p.channel = p.params.left(bodyPos < 0 ? p.params.size() : bodyPos).section(' ', 0, 0);
    p.channel.remove('#');
    p.channel = p.channel.toLower();
    if (p.command != "PRIVMSG") {
        return p;
    }
    const int singleWord = p.params.indexOf(' ');
    if (bodyPos < 0 && singleWord < 0) {
        return p;
    }
    auto &m = p.message;
    m.id = str(p.tags["id"]);
    m.login = prefix.section('!', 0, 0);
    m.name = str(p.tags["display-name"]);
    if (m.name.isEmpty()) {
        m.name = m.login;
    }
    m.uid = str(p.tags["user-id"]);
    m.color = str(p.tags["color"]);
    m.badgeInfo = str(p.tags["badge-info"]);
    m.text = p.params.mid(bodyPos >= 0 ? bodyPos + 2 : singleWord + 1);
    if (m.text.startsWith("\x01"
                          "ACTION ")) {
        m.text = m.text.mid(8);
        if (m.text.endsWith(QChar(1))) {
            m.text.chop(1);
        }
    }
    auto ts = str(p.tags["tmi-sent-ts"]).toLongLong();
    if (!ts) {
        ts = str(p.tags["rm-received-ts"]).toLongLong();
    }
    m.stamp = ts ? QDateTime::fromMSecsSinceEpoch(ts) : QDateTime::currentDateTime();
    for (const auto &s : str(p.tags["badges"]).split(',', Qt::SkipEmptyParts)) {
        Badge b;
        b.id = s.section('/', 0, 0);
        b.version = s.section('/', 1);
        b.role = b.id == "broadcaster" ? "owner" : b.id;
        b.label = '[' + b.id.toUpper() + ']';
        m.badges << b;
    }
    for (const auto &entry : str(p.tags["emotes"]).split('/', Qt::SkipEmptyParts)) {
        QString eid = entry.section(':', 0, 0);
        for (const auto &range : entry.section(':', 1).split(',')) {
            bool ok1 = false, ok2 = false;
            int begin = range.section('-', 0, 0).toInt(&ok1), end = range.section('-', 1).toInt(&ok2);
            if (!ok1 || !ok2 || begin < 0 || end < begin) {
                continue;
            }
            Span s;
            s.start = codepointOffset(m.text, begin);
            s.end = codepointOffset(m.text, end + 1);
            s.code = m.text.mid(s.start, s.end - s.start);
            s.urls = {"https://static-cdn.jtvnw.net/emoticons/v2/" + eid + "/default/dark/3.0"};
            if (s.end <= m.text.size()) {
                m.emotes << s;
            }
        }
    }
    for (const auto &entry : str(p.tags["gifs"]).split(',').mid(0, 8)) {
        auto parts = entry.split('|');
        if (parts.size() != 3) {
            continue;
        }
        QUrl u(parts[2]);
        QString h = u.host();
        bool ok1 = false, ok2 = false;
        int a = parts[0].section('-', 0, 0).toInt(&ok1), b = parts[0].section('-', 1).toInt(&ok2);
        if (u.scheme() != "https" || !u.userInfo().isEmpty() || (u.port(-1) != -1 && u.port() != 443) ||
            !(h == "giphy.com" || h.endsWith(".giphy.com")) || !ok1 || !ok2 || a < 0 || b < a ||
            b >= characterCount(m.text)) {
            continue;
        }
        Span s;
        s.start = codepointOffset(m.text, a);
        s.end = codepointOffset(m.text, b + 1);
        s.code = m.text.mid(s.start, s.end - s.start);
        s.urls = {parts[2]};
        s.gif = true;
        m.emotes << s;
    }
    if (p.tags.contains("reply-parent-msg-id")) {
        m.reply = {{"id", p.tags["reply-parent-msg-id"]},
                   {"user_id", p.tags["reply-parent-user-id"]},
                   {"name", p.tags["reply-parent-display-name"]},
                   {"login", p.tags["reply-parent-user-login"]},
                   {"text", p.tags["reply-parent-msg-body"]},
                   {"thread_id", p.tags["reply-thread-parent-msg-id"]},
                   {"thread_login", p.tags["reply-thread-parent-user-login"]}};
    }
    return p;
}

static void appendRuns(Message &m, const QJsonValue &v) {
    auto o = v.toObject();
    if (!o.contains("runs")) {
        m.text += textOf(v);
        return;
    }
    for (const auto &run : o["runs"].toArray()) {
        auto r = run.toObject();
        auto emoji = r["emoji"].toObject();
        if (emoji.isEmpty()) {
            m.text += str(r["text"]);
            continue;
        }
        QString code = firstItem(emoji["shortcuts"].toArray()).toString();
        if (code.isEmpty()) {
            code = str(emoji["emojiId"]);
        }
        Span s;
        s.start = m.text.size();
        s.end = s.start + code.size();
        s.code = code;
        s.urls = imageUrls(emoji["image"]);
        m.text += code;
        if (!s.urls.isEmpty()) {
            m.emotes << s;
        }
    }
}

Message parseYouTube(const QJsonObject &o, const QString &kind, bool historical) {
    Message m;
    m.id = str(o["id"]);
    m.name = textOf(o["authorName"]);
    m.uid = str(o["authorExternalChannelId"]);
    m.login = m.name;
    m.avatar = imageUrls(o["authorPhoto"]).value(0);
    m.history = historical;
    qint64 micro = str(o["timestampUsec"]).toLongLong();
    m.stamp = micro ? QDateTime::fromMSecsSinceEpoch(micro / 1000) : QDateTime::currentDateTime();
    appendRuns(m, o.contains("message") ? o["message"] : o["headerSubtext"]);
    if (kind.contains("Paid")) {
        m.special = "super";
        if (m.text.isEmpty() && kind.contains("Sticker")) {
            m.text = "[sticker]";
            Span s;
            s.start = 0;
            s.end = m.text.size();
            s.code = m.text;
            s.urls = imageUrls(o["sticker"]);
            m.emotes << s;
        }
        QString amount = textOf(o["purchaseAmountText"]);
        if (!amount.isEmpty()) {
            QString prefix = amount + ' ';
            m.text.prepend(prefix);
            for (auto &e : m.emotes) {
                e.start += prefix.size();
                e.end += prefix.size();
            }
        }
    } else if (kind.contains("Membership") || kind.contains("Sponsorship")) {
        m.special = "member";
        if (m.text.isEmpty()) {
            m.text = textOf(o["headerPrimaryText"]);
        }
    }
    for (const auto &v : o["authorBadges"].toArray()) {
        auto b = v.toObject()["liveChatAuthorBadgeRenderer"].toObject();
        QString type = str(at(b, {"icon", "iconType"}));
        Badge badge;
        badge.id = type;
        badge.urls = imageUrls(b["customThumbnail"]);
        badge.label = str(b["tooltip"]);
        if (type == "OWNER") {
            badge.role = "owner";
        } else if (type == "MODERATOR") {
            badge.role = "moderator";
        } else if (type == "VERIFIED") {
            badge.role = "verified";
        } else if (!badge.urls.isEmpty() || badge.label.contains("member", Qt::CaseInsensitive)) {
            badge.role = "subscriber";
        }
        if (!badge.role.isEmpty()) {
            if (badge.label.isEmpty()) {
                badge.label = badge.role;
            }
            m.badges << badge;
        }
    }
    return m;
}

QSet<QString> roles(const Message &m) {
    QSet<QString> r;
    for (const auto &b : m.badges) {
        r.insert(b.role);
        r.insert(b.id);
    }
    if (r.contains("broadcaster")) {
        r.insert("owner");
    }
    if (r.contains("member")) {
        r.insert("subscriber");
    }
    return r;
}

QColor userColor(const Message &m) {
    QColor c(m.color);
    if (c.isValid()) {
        return c;
    }
    static QStringList palette = {"#ff7f50", "#1e90ff", "#00ff7f", "#daa520", "#ff69b4", "#a070ff",
                                  "#00ced1", "#9acd32", "#ff5a36", "#5f9ea0", "#e08a3c", "#e05d5d"};
    const auto h = QCryptographicHash::hash(m.name.toUtf8(), QCryptographicHash::Md5);
    return QColor(palette[quint8(h[0]) % palette.size()]);
}

QColor highlight(const Message &m, const Config &cfg, const QStringList &ownNames, bool *mentioned) {
    const auto settings = cfg.feature("highlight"), users = settings["users"].toObject(),
               rs = settings["roles"].toObject();
    QColor c;
    for (auto i = users.begin(); i != users.end(); ++i) {
        if (stripAt(i.key()) == stripAt(m.login) || stripAt(i.key()) == stripAt(m.name)) {
            c = QColor(i.value().toString());
            break;
        }
    }
    if (!c.isValid()) {
        auto mr = roles(m);
        for (auto i = rs.begin(); i != rs.end(); ++i) {
            if (mr.contains(i.key())) {
                c = QColor(i.value().toString());
                break;
            }
        }
    }
    QStringList terms;
    for (const auto &v : settings["words"].toArray()) {
        terms << v.toString();
    }
    if (settings["mention_me"].toBool(true)) {
        terms << ownNames;
    }
    bool match = false;
    for (const auto &word : terms) {
        if (wordMatch(m.text, word)) {
            match = true;
            break;
        }
    }
    if (mentioned) {
        *mentioned = match;
    }
    if (!c.isValid() && match) {
        c = QColor(settings["mention_color"].toString("#227400"));
    }
    return c;
}

bool accepts(const Message &m, const QJsonObject &f) {
    if (m.system) {
        return !f["hide_system"].toBool();
    }
    for (const auto &v : f["hidden_users"].toArray()) {
        if (stripAt(v.toString()) == stripAt(m.login) || stripAt(v.toString()) == stripAt(m.name)) {
            return false;
        }
    }
    auto include = f["include_terms"].toArray();
    bool found = include.isEmpty();
    for (const auto &v : include) {
        if (m.text.contains(v.toString(), Qt::CaseInsensitive)) {
            found = true;
        }
    }
    if (!found) {
        return false;
    }
    for (const auto &v : f["exclude_terms"].toArray()) {
        if (m.text.contains(v.toString(), Qt::CaseInsensitive)) {
            return false;
        }
    }
    if (f["hide_deleted"].toBool() && m.deleted) {
        return false;
    }
    if (f["hide_system"].toBool() && m.system) {
        return false;
    }
    return true;
}

QString threadRoot(const Message &message, const QHash<QString, Message> &messages) {
    Message m = message;
    QStringList path;
    QHash<QString, int> positions;
    while (true) {
        if (positions.contains(m.id)) {
            auto cycle = path.mid(positions[m.id]);
            std::sort(cycle.begin(), cycle.end());
            return cycle.value(0);
        }
        positions[m.id] = path.size();
        path << m.id;
        QString root = str(m.reply["thread_id"]);
        if (!root.isEmpty()) {
            return root;
        }
        QString parent = str(m.reply["id"]);
        if (parent.isEmpty()) {
            return m.id;
        }
        if (!messages.contains(parent)) {
            return parent;
        }
        m = messages[parent];
    }
}

QVector<Message> collectThread(const QVector<Message> &history, const Message &selected, QString *root) {
    QHash<QString, Message> index;
    for (const auto &m : history) {
        if (!m.id.isEmpty()) {
            index[m.id] = m;
        }
    }
    index[selected.id] = selected;
    *root = threadRoot(selected, index);
    QVector<Message> result;
    for (const auto &m : history) {
        if (!m.system && (m.id == *root || threadRoot(m, index) == *root)) {
            result << m;
        }
    }
    return result;
}

QString browserUrl(const QString &platform, const QString &id, const QString &destination) {
    if (platform == "twitch") {
        if (!QRegularExpression("^[a-z0-9_]{1,25}$").match(id).hasMatch()) {
            return {};
        }
        if (destination == "player") {
            return "https://player.twitch.tv/?channel=" + id + "&parent=twitch.tv";
        }
        if (destination == "chat") {
            return "https://www.twitch.tv/popout/" + id + "/chat?popout=&darkpopout";
        }
        return "https://www.twitch.tv/" + id;
    }
    if (!QRegularExpression("^[a-zA-Z0-9_-]{11}$").match(id).hasMatch()) {
        return {};
    }
    if (destination == "chat") {
        return "https://www.youtube.com/live_chat?v=" + id + "&is_popout=1";
    }
    if (destination == "player") {
        return "https://www.youtube.com/embed/" + id;
    }
    return "https://www.youtube.com/watch?v=" + id;
}

bool validVersion(const QString &v) {
    return QRegularExpression("^v?[0-9]+\\.[0-9]+\\.[0-9]+$").match(v).hasMatch();
}

bool versionNewer(const QString &candidate, const QString &current) {
    if (!validVersion(candidate) || !validVersion(current)) {
        return false;
    }
    QString a = candidate, b = current;
    a.remove(QRegularExpression("^v"));
    b.remove(QRegularExpression("^v"));
    auto aa = a.split('.'), bb = b.split('.');
    for (int i = 0; i < 3; ++i) {
        if (aa[i].toLongLong() != bb[i].toLongLong()) {
            return aa[i].toLongLong() > bb[i].toLongLong();
        }
    }
    return false;
}

Quota::Quota(Config *c) : QObject(c), cfg(c) {
    daily = readObject(cfg->home + "/.ytchatterino_quota.json");
    roll();
}

QString Quota::day() const {
    return QDateTime::currentDateTimeUtc()
        .toTimeZone(QTimeZone("America/Los_Angeles"))
        .date()
        .toString(Qt::ISODate);
}

void Quota::roll() {
    if (daily["date"].toString() != day()) {
        daily = {{"date", day()}, {"used", 0}};
        writeObject(cfg->home + "/.ytchatterino_quota.json", daily);
    }
}

void Quota::add(int units) {
    roll();
    daily["used"] = daily["used"].toInt() + qMax(0, units);
    writeObject(cfg->home + "/.ytchatterino_quota.json", daily);
    emit changed();
}

int Quota::used() const {
    return daily["date"].toString() == day() ? daily["used"].toInt() : 0;
}

int Quota::remaining() const {
    int limit = qBound(1, cfg->data["youtube_daily_limit"].toInt(10000), 10000000);
    auto adjustment = cfg->data["youtube_external_usage"].toObject();
    int external = adjustment["date"].toString() == day() ? qMax(0, adjustment["used"].toInt()) : 0;
    return qMax(0, limit - external - used());
}
} // namespace yc
