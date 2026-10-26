#include "application.hpp"

namespace yc {
static QPushButton *action(Config &cfg, QString text, QObject *receiver, std::function<void()> fn) {
    auto *b = new QPushButton(cfg.T(text));
    QObject::connect(b, &QPushButton::clicked, receiver, std::move(fn));
    return b;
}

static QLabel *text(Config &cfg, QString value, bool dim = false) {
    auto *l = new QLabel(cfg.T(value));
    l->setWordWrap(true);
    l->setTextFormat(Qt::PlainText);
    if (dim) {
        l->setStyleSheet("color:#8b8b94;");
    }
    return l;
}

static QJsonArray strings(const QStringList &list) {
    return QJsonArray::fromStringList(list);
}

static QStringList stringList(const QJsonValue &v) {
    QStringList out;
    for (const auto &s : v.toArray()) {
        out << s.toString();
    }
    return out;
}

static QString normalize(QString s) {
    s = s.normalized(QString::NormalizationForm_D).toCaseFolded();
    s.remove(QRegularExpression("\\p{M}"));
    return s;
}

static QWidget *pageWidget(Config &cfg, const QString &title, QVBoxLayout **layout) {
    auto *w = new QWidget;
    *layout = new QVBoxLayout(w);
    (*layout)->setContentsMargins(14, 8, 14, 10);
    auto *heading = text(cfg, title);
    heading->setFont(QFont("Segoe UI", 14, QFont::Bold));
    (*layout)->addWidget(heading);
    return w;
}

SettingsWindow::SettingsWindow(MainWindow *m) : QDialog(m), main(m) {
    setWindowTitle(main->cfg.T("Settings · YT-Chatterino"));
    resize(850, 700);
    setMinimumSize(620, 400);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);
    search = new QLineEdit(this);
    search->setPlaceholderText(main->cfg.T("Search settings"));
    search->setClearButtonEnabled(true);
    outer->addWidget(search);
    auto *body = new QHBoxLayout;
    navigation = new QTreeWidget(this);
    navigation->setHeaderHidden(true);
    navigation->setMaximumWidth(195);
    navigation->setMinimumWidth(145);
    navigation->setRootIsDecorated(true);
    navigation->setIndentation(12);
    body->addWidget(navigation);
    pages = new QStackedWidget(this);
    body->addWidget(pages, 1);
    outer->addLayout(body, 1);
    results = new QListWidget(this);
    pages->addWidget(results);
    auto *shortcut = new QShortcut(QKeySequence::Find, this);
    connect(shortcut, &QShortcut::activated, this, [this] {
        search->setFocus();
        search->selectAll();
    });
    connect(search, &QLineEdit::textChanged, this, &SettingsWindow::searchSettings);
    connect(navigation, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        search->clear();
        showPage(item->data(0, Qt::UserRole).toString());
    });
    connect(navigation, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
                if (item) {
                    search->clear();
                    showPage(item->data(0, Qt::UserRole).toString());
                }
            });
    connect(results, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        QString page = item->data(Qt::UserRole).toString();
        search->clear();
        showPage(page);
    });
    QFile file(":/settings-search.json");
    if (file.open(QIODevice::ReadOnly)) {
        auto list = QJsonDocument::fromJson(file.readAll()).array();
        for (const auto &v : list) {
            auto row = v.toArray();
            if (row.size() >= 3) {
                searchItems[row[0].toString()] << row[1].toString() + '\t' + row[2].toString();
            }
        }
    }
    searchItems["Display"] << "Message font size (pt)\tmessage font size username fonte nome mensagem"
                           << "Date format\tdates time seconds formato data am pm";
    searchItems["Updates"]
        << "Check for updates automatically\tupdates native automatic download atualizar atualizacoes";
    rebuildLanguage();
}

void SettingsWindow::rebuildLanguage() {
    QString keep = currentPage.isEmpty() ? "Accounts" : currentPage;
    navigation->clear();
    for (const auto &name : QStringList{"Accounts", "Appearance", "Notifications", "Saved channels",
                                        "YouTube quota", "Updates", "Keyboard shortcuts", "Tutorial"}) {
        auto *item = new QTreeWidgetItem(navigation, {main->cfg.T(name)});
        item->setData(0, Qt::UserRole, name == "Appearance" ? "Display" : name);
        if (name == "Appearance") {
            for (const auto &sub : QStringList{"Display", "Messages", "Highlights"}) {
                auto *child = new QTreeWidgetItem(item, {main->cfg.T(sub)});
                child->setData(0, Qt::UserRole, sub);
            }
            item->setExpanded(true);
        }
    }
    const auto old = built.values();
    built.clear();
    for (auto *w : old) {
        pages->removeWidget(w);
        w->deleteLater();
    }
    setWindowTitle(main->cfg.T("Settings · YT-Chatterino"));
    search->setPlaceholderText(main->cfg.T("Search settings"));
    showPage(keep);
}

void SettingsWindow::showPage(QString name) {
    if (name == "Appearance") {
        name = "Display";
    }
    if (name.isEmpty()) {
        name = "Accounts";
    }
    currentPage = name;
    QTreeWidgetItem *selected = nullptr;
    for (QTreeWidgetItemIterator it(navigation); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole).toString() == name && (!selected || (*it)->parent())) {
            selected = *it;
        }
    }
    {
        QSignalBlocker block(navigation);
        navigation->setCurrentItem(selected);
        if (selected && selected->parent()) {
            selected->parent()->setExpanded(true);
        }
    }
    if (!built.contains(name)) {
        auto *inner = buildPage(name);
        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(inner);
        built[name] = scroll;
        pages->addWidget(scroll);
    }
    pages->setCurrentWidget(built[name]);
}

QWidget *SettingsWindow::buildPage(QString name) {
    if (name == "Display") {
        return appearancePage();
    }
    if (name == "Messages") {
        return messagesPage();
    }
    if (name == "Highlights") {
        return highlightsPage();
    }
    if (name == "Notifications") {
        return notificationsPage();
    }
    if (name == "Saved channels") {
        return savedPage();
    }
    if (name == "YouTube quota") {
        return quotaPage();
    }
    if (name == "Updates") {
        return updatesPage();
    }
    if (name == "Keyboard shortcuts") {
        return shortcutsPage();
    }
    if (name == "Tutorial") {
        return tutorialPage();
    }
    return accountsPage();
}

void SettingsWindow::searchSettings() {
    QString q = normalize(search->text().trimmed());
    if (q.isEmpty()) {
        if (built.contains(currentPage)) {
            pages->setCurrentWidget(built[currentPage]);
        }
        return;
    }
    results->clear();
    auto tokens = q.split(' ', Qt::SkipEmptyParts);
    QSet<QString> displayed;
    for (auto i = searchItems.begin(); i != searchItems.end(); ++i) {
        for (const auto &entry : i.value()) {
            QString label = entry.section('\t', 0, 0);
            QString hay =
                normalize(i.key() + ' ' + main->cfg.T(i.key()) + ' ' + entry + ' ' + main->cfg.T(label));
            bool match = true;
            for (const auto &token : tokens) {
                match &= hay.contains(token);
            }
            if (match) {
                const auto identity = i.key() + '\t' + label;
                if (displayed.contains(identity)) {
                    continue;
                }
                displayed.insert(identity);
                auto *item = new QListWidgetItem(main->cfg.T(i.key()) + " — " + main->cfg.T(label), results);
                item->setData(Qt::UserRole, i.key());
            }
        }
    }
    if (!results->count()) {
        auto *none = new QListWidgetItem(main->cfg.T("No settings found. Try another word."), results);
        none->setFlags(Qt::NoItemFlags);
    }
    pages->setCurrentWidget(results);
}

QWidget *SettingsWindow::accountsPage() {
    QVBoxLayout *l;
    auto *w = pageWidget(main->cfg, "Accounts", &l);
    auto *twBox = new QGroupBox("Twitch", w);
    auto *twLayout = new QVBoxLayout(twBox);
    auto *twStatus = text(main->cfg, "");
    twStatus->setObjectName("twitch-account-status");
    auto *twPermissions = text(main->cfg, "", true);
    twPermissions->setObjectName("twitch-permission-status");
    auto *twButton = action(main->cfg, "Sign in to Twitch", w,
                            [this] { main->accounts.signInTwitch(Accounts::fullTwitchScopes()); });
    twButton->setObjectName("twitch-permissions");
    auto *twReconnect = action(main->cfg, "Reconnect / change Twitch account…", w,
                               [this] { main->accounts.signInTwitch(Accounts::fullTwitchScopes()); });
    twLayout->addWidget(twStatus);
    twLayout->addWidget(twPermissions);
    twLayout->addWidget(twButton);
    twLayout->addWidget(twReconnect);
    l->addWidget(twBox);

    auto *ytBox = new QGroupBox("YouTube", w);
    auto *ytLayout = new QVBoxLayout(ytBox);
    auto *ytStatus = text(main->cfg, "");
    ytStatus->setObjectName("youtube-account-status");
    auto *ytPermissions = text(main->cfg, "", true);
    auto *chooseJson = action(main->cfg, "Choose Google OAuth JSON…", w, [this] {
        auto path = QFileDialog::getOpenFileName(this, main->cfg.T("Google Desktop OAuth client"), {},
                                                 "JSON (*.json)");
        if (path.isEmpty()) {
            return;
        }
        auto d = readObject(path)["installed"].toObject();
        if (str(d["client_id"]).isEmpty()) {
            QMessageBox::warning(this, main->cfg.T("Google OAuth"),
                                 main->cfg.T("Choose a Google Desktop OAuth client JSON file."));
            return;
        }
        auto f = main->cfg.features();
        f["google_client_file"] = path;
        main->cfg.data["features"] = f;
        main->cfg.save();
        emit main->accounts.changed();
    });
    chooseJson->setObjectName("google-client-file");
    auto *ytButton = action(main->cfg, "Sign in to YouTube", w, [this] { main->accounts.signInGoogle(); });
    ytButton->setObjectName("youtube-permissions");
    auto *ytReconnect = action(main->cfg, "Reconnect / change YouTube account…", w,
                               [this] { main->accounts.signInGoogle(); });
    auto *cancel =
        action(main->cfg, "Cancel pending Google sign-in", w, [this] { main->accounts.cancelGoogle(); });
    ytLayout->addWidget(ytStatus);
    ytLayout->addWidget(ytPermissions);
    auto *googleHelp = text(main->cfg, Accounts::googleSignInHelp(), true);
    googleHelp->setObjectName("google-signin-help");
    ytLayout->addWidget(googleHelp);
    ytLayout->addWidget(chooseJson);
    ytLayout->addWidget(ytButton);
    ytLayout->addWidget(ytReconnect);
    ytLayout->addWidget(cancel);
    l->addWidget(ytBox);

    auto refresh = [=, this] {
        auto &cfg = main->cfg;
        auto &accounts = main->accounts;
        const bool twConnected = accounts.connected("twitch");
        const bool twReady = accounts.twitchAllows(Accounts::fullTwitchScopes());
        const bool twPending = accounts.twitchBusy || accounts.twitchRefreshing;
        const bool twSaved = !str(accounts.twitch.value("access_token")).isEmpty();
        twStatus->setText(accounts.twitchRefreshing ? cfg.T("Refreshing saved sign-in…")
                          : twConnected
                              ? cfg.T("Signed in as @{name}", {{"name", str(accounts.twitch.value("login"))}})
                          : !accounts.twitchIssue.isEmpty() ? cfg.T(accounts.twitchIssue)
                                                            : cfg.T("Not connected"));
        QStringList permissions;
        for (const auto &item : QList<QPair<QString, QSet<QString>>>{
                 {"Reading chat", {"chat:read"}},
                 {"Sending messages", {"user:write:chat"}},
                 {"Subscriber emotes", {"user:read:emotes"}},
                 {"Moderation",
                  {"user:read:moderated_channels", "moderator:manage:chat_messages",
                   "moderator:manage:banned_users"}},
                 {"Title / category editing", {"channel:manage:broadcast"}}}) {
            permissions << (accounts.twitchAllows(item.second) ? "✓ " : "○ ") + cfg.T(item.first);
        }
        twPermissions->setText(permissions.join("\n"));
        twButton->setText(cfg.T(twPending     ? "Waiting for Twitch…"
                                : twReady     ? "All Twitch permissions enabled"
                                : twConnected ? "Enable missing Twitch permissions…"
                                : twSaved     ? "Reconnect Twitch…"
                                              : "Sign in to Twitch"));
        twButton->setEnabled(!twPending && !twReady);
        twReconnect->setVisible(twReady);
        twReconnect->setEnabled(!twPending);

        const bool ytReady = accounts.googleAllowsWriting();
        const bool ytPending = accounts.googleBusy || accounts.googleRefreshing;
        const bool ytSaved = !str(accounts.google.value("access_token")).isEmpty();
        const auto ytName = str(accounts.google.value("channel_title"));
        ytStatus->setText(accounts.googleRefreshing         ? cfg.T("Refreshing saved sign-in…")
                          : accounts.connected("google")    ? cfg.T("Signed in as {name}", {{"name", ytName}})
                          : !accounts.googleIssue.isEmpty() ? cfg.T(accounts.googleIssue)
                                                            : cfg.T("Not connected"));
        ytPermissions->setText(
            cfg.T(ytReady ? "✓ Sending, moderation and title editing enabled. Channel roles still apply."
                          : "Sign in to send messages, moderate or edit a stream title. Reading chat needs "
                            "no sign-in."));
        chooseJson->setText(cfg.T(accounts.googleClientConfigured() ? "Change Google OAuth JSON…"
                                                                    : "Choose Google OAuth JSON…"));
        chooseJson->setEnabled(!ytPending);
        ytButton->setText(cfg.T(ytPending                      ? "Waiting for YouTube…"
                                : ytReady                      ? "YouTube permissions enabled"
                                : accounts.connected("google") ? "Enable YouTube sending / moderation…"
                                : ytSaved                      ? "Reconnect YouTube…"
                                                               : "Sign in to YouTube"));
        ytButton->setEnabled(!ytPending && !ytReady);
        ytReconnect->setVisible(ytReady);
        ytReconnect->setEnabled(!ytPending);
        cancel->setVisible(accounts.googleBusy);
    };
    refresh();
    connect(&main->accounts, &Accounts::changed, w, refresh);
    l->addWidget(
        text(main->cfg, "Moderation and stream editing also require the appropriate channel role.", true));
    l->addSpacing(10);
    l->addWidget(
        text(main->cfg, "YouTube reading: no key, no sign-in, 0 units. Paste a live link with +.", true));
    l->addWidget(action(main->cfg, "Connection tutorial…", w, [this] { showPage("Tutorial"); }));
    l->addStretch();
    return w;
}

QWidget *SettingsWindow::appearancePage() {
    auto &cfg = main->cfg;
    QVBoxLayout *l;
    auto *w = pageWidget(cfg, "Appearance", &l);
    auto a = cfg.appearance();
    auto *form = new QFormLayout;
    auto *lang = new QComboBox(w);
    lang->addItems({"English", "Português (Brasil)"});
    lang->setCurrentIndex(cfg.language() == "pt-BR" ? 1 : 0);
    form->addRow(cfg.T("Language / Idioma"), lang);
    auto *top = new QCheckBox(cfg.T("Always on top"), w);
    top->setChecked(cfg.data["topmost"].toBool());
    form->addRow(top);
    QMap<QString, QCheckBox *> checks;
    for (const auto &pair :
         QList<QPair<QString, QString>>{{"show_timestamps", "Show timestamps"},
                                        {"compact_spacing", "Compact message spacing"},
                                        {"alternate_rows", "Alternate message backgrounds"}}) {
        auto *check = new QCheckBox(cfg.T(pair.second), w);
        check->setChecked(a[pair.first].toBool());
        checks[pair.first] = check;
        form->addRow(check);
    }
    QMap<QString, QSpinBox *> sizes;
    for (const auto &item :
         QList<QStringList>{{"message_font_size", "Message font size (pt)", "7", "48", "12"},
                            {"username_font_size", "Username font size (pt)", "7", "48", "12"},
                            {"badge_size", "Badge height (px)", "10", "64", "18"},
                            {"emote_size", "Emote height (px)", "16", "128", "36"}}) {
        auto *spin = new QSpinBox(w);
        spin->setRange(item[2].toInt(), item[3].toInt());
        spin->setValue(a[item[0]].toInt(item[4].toInt()));
        spin->setObjectName(item[0]);
        sizes[item[0]] = spin;
        form->addRow(cfg.T(item[1]), spin);
    }
    auto *date = new QComboBox(w);
    date->addItems({"DD/MM/YYYY", "MM/DD/YYYY", "YYYY-MM-DD", "DD.MM.YYYY"});
    date->setCurrentText(a["date_format"].toString("DD/MM/YYYY"));
    form->addRow(cfg.T("Date format"), date);
    auto *time = new QComboBox(w);
    time->addItems({"HH:mm", "HH:mm:ss", "hh:mm am/pm", "hh:mm:ss am/pm"});
    time->setCurrentText(a["time_format"].toString("HH:mm"));
    form->addRow(cfg.T("Timestamp format"), time);
    auto *scrollSpeed = new QDoubleSpinBox(w);
    scrollSpeed->setObjectName("scroll-multiplier");
    scrollSpeed->setRange(0.5, 5.0);
    scrollSpeed->setSingleStep(0.25);
    scrollSpeed->setSuffix("×");
    scrollSpeed->setValue(a.value("scroll_multiplier").toDouble(2.0));
    form->addRow(cfg.T("Mouse wheel speed"), scrollSpeed);
    l->addLayout(form);
    l->addWidget(
        text(cfg, "Text size: Ctrl++ / Ctrl+- · Ctrl+0 resets. Images keep their aspect ratio.", true));
    l->addWidget(text(cfg, "HH = 24-hour; hh with am/pm = 12-hour. Add :ss to show seconds.", true));
    auto baseline = QSharedPointer<QMap<QString, int>>::create();
    for (auto i = sizes.begin(); i != sizes.end(); ++i) {
        (*baseline)[i.key()] = i.value()->value();
    }
    connect(&cfg, &Config::appearanceChanged, w, [this, sizes, baseline] {
        auto a = main->cfg.appearance();
        for (auto i = sizes.begin(); i != sizes.end(); ++i) {
            if (i.value()->value() == baseline->value(i.key())) {
                i.value()->setValue(a[i.key()].toInt());
            }
            (*baseline)[i.key()] = a[i.key()].toInt();
        }
    });
    l->addWidget(action(cfg, "Apply", w, [this, checks, sizes, top, date, time, scrollSpeed] {
        auto a = main->cfg.appearance();
        for (auto i = checks.begin(); i != checks.end(); ++i) {
            a[i.key()] = i.value()->isChecked();
        }
        for (auto i = sizes.begin(); i != sizes.end(); ++i) {
            a[i.key()] = i.value()->value();
        }
        a["font_size"] = sizes["message_font_size"]->value();
        a["date_format"] = date->currentText();
        a["time_format"] = time->currentText();
        a["scroll_multiplier"] = scrollSpeed->value();
        main->cfg.data["topmost"] = top->isChecked();
        main->cfg.setAppearance(a);
        main->applyTopmost();
    }));
    connect(lang, qOverload<int>(&QComboBox::activated), w, [this](int i) {
        main->cfg.data["language"] = i ? "pt-BR" : "en";
        main->cfg.save();
        QTimer::singleShot(0, main, [this] { emit main->cfg.languageChanged(); });
    });
    l->addStretch();
    return w;
}

QWidget *SettingsWindow::messagesPage() {
    QVBoxLayout *l;
    auto *w = pageWidget(main->cfg, "Messages", &l);
    auto *form = new QFormLayout;
    auto *cap = new QSpinBox(w);
    cap->setRange(100, 20000);
    cap->setSingleStep(100);
    cap->setValue(main->cfg.cap());
    form->addRow(main->cfg.T("Default message limit per pane"), cap);
    auto *history = new QCheckBox(main->cfg.T("Load recent Twitch messages when opening a channel"), w);
    history->setChecked(main->cfg.data["load_twitch_history"].toBool(true));
    form->addRow(history);
    l->addLayout(form);
    l->addWidget(text(main->cfg,
                      "Twitch history uses recent-messages.robotty.de without account tokens. Availability "
                      "varies. YouTube loads the recent messages its page provides.",
                      true));
    l->addWidget(text(main->cfg,
                      "YouTube and Twitch stay connected in background tabs. Pause YouTube manually from "
                      "Channel actions when needed.",
                      true));
    l->addWidget(action(main->cfg, "Apply message limits / history", w, [this, cap, history] {
        main->cfg.data["message_limit"] = cap->value();
        main->cfg.data["load_twitch_history"] = history->isChecked();
        main->cfg.save();
        for (auto s : main->hub.sources) {
            s->trim();
        }
        emit main->cfg.appearanceChanged();
    }));
    l->addStretch();
    return w;
}

QWidget *SettingsWindow::highlightsPage() {
    auto &cfg = main->cfg;
    auto settings = cfg.feature("highlight");
    QVBoxLayout *l;
    auto *w = pageWidget(cfg, "Highlights", &l);
    l->addWidget(text(cfg, "Users · one username per line, optionally followed by #RRGGBB"));
    auto *users = new QPlainTextEdit(w);
    users->setMaximumHeight(130);
    QStringList lines;
    auto map = settings["users"].toObject();
    for (auto i = map.begin(); i != map.end(); ++i) {
        lines << i.key() + ' ' + i.value().toString();
    }
    users->setPlainText(lines.join('\n'));
    l->addWidget(users);
    auto *form = new QFormLayout;
    QMap<QString, QCheckBox *> roleChecks;
    QMap<QString, QLineEdit *> colors;
    for (const auto &pair : QList<QPair<QString, QString>>{{"owner", "Owners"},
                                                           {"moderator", "Moderators"},
                                                           {"vip", "VIPs"},
                                                           {"subscriber", "Subscribers / members"},
                                                           {"founder", "Founders"}}) {
        auto *row = new QWidget(w);
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        auto *check = new QCheckBox(cfg.T(pair.second), row);
        check->setChecked(settings["roles"].toObject().contains(pair.first));
        auto *color = new QLineEdit(settings["roles"].toObject()[pair.first].toString("#227400"), row);
        color->setMaximumWidth(100);
        auto *pick = action(cfg, "Color", row, [this, color] {
            auto c = QColorDialog::getColor(QColor(color->text()), this);
            if (c.isValid()) {
                color->setText(c.name());
            }
        });
        rl->addWidget(check, 1);
        rl->addWidget(color);
        rl->addWidget(pick);
        form->addRow(row);
        roleChecks[pair.first] = check;
        colors[pair.first] = color;
    }
    auto *terms = new QLineEdit(stringList(settings["words"]).join(", "), w);
    form->addRow(cfg.T("Words / phrases · separate with commas"), terms);
    auto *mention = new QCheckBox(cfg.T("Highlight mentions of my signed-in names"), w);
    mention->setChecked(settings["mention_me"].toBool(true));
    form->addRow(mention);
    l->addLayout(form);
    l->addWidget(action(cfg, "Apply highlights", w, [this, users, terms, mention, roleChecks, colors] {
        QJsonObject people, roles;
        for (const auto &line : users->toPlainText().split('\n')) {
            auto parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.isEmpty()) {
                continue;
            }
            QString color = "#227400";
            if (QColor(parts.last()).isValid() && parts.last().startsWith('#')) {
                color = parts.takeLast();
            }
            QString name = stripAt(parts.join(' '));
            if (!name.isEmpty()) {
                people[name] = color;
            }
        }
        for (auto i = roleChecks.begin(); i != roleChecks.end(); ++i) {
            if (i.value()->isChecked()) {
                QColor c(colors[i.key()]->text());
                if (!c.isValid()) {
                    QMessageBox::warning(this, main->cfg.T("Highlights"),
                                         main->cfg.T("Enter colors as #RRGGBB."));
                    return;
                }
                roles[i.key()] = c.name();
            }
        }
        auto h = main->cfg.feature("highlight");
        h["users"] = people;
        h["roles"] = roles;
        h["words"] = strings(words(terms->text()));
        h["mention_me"] = mention->isChecked();
        main->cfg.setFeature("highlight", h);
    }));
    l->addStretch();
    return w;
}

QWidget *SettingsWindow::notificationsPage() {
    auto &cfg = main->cfg;
    auto settings = cfg.feature("alerts");
    QVBoxLayout *l;
    auto *w = pageWidget(cfg, "Notifications", &l);
    auto *enabled = new QCheckBox(cfg.T("Enable notification sounds"), w);
    enabled->setChecked(settings["enabled"].toBool(true));
    auto *mentions = new QCheckBox(cfg.T("Sound for matching mentions / highlight words"), w);
    mentions->setChecked(settings["mention_sound"].toBool(true));
    auto *automatic = new QCheckBox(cfg.T("Notify all open Twitch channels by default"), w);
    automatic->setChecked(settings["all_twitch"].toBool(true));
    l->addWidget(enabled);
    l->addWidget(mentions);
    l->addWidget(automatic);
    l->addWidget(text(cfg, "Additional nicknames · separate with commas"));
    auto *names = new QLineEdit(stringList(cfg.features()["nicknames"]).join(", "), w);
    l->addWidget(names);
    l->addWidget(text(cfg, "Default sound: ChatternoPing.wav · Live alerts: Twitch only", true));
    auto *soundRow = new QHBoxLayout;
    for (const auto &pair : QList<QPair<QString, QString>>{{"sound_file", "Mention sound…"},
                                                           {"live_sound_file", "Live sound…"}}) {
        soundRow->addWidget(action(cfg, pair.second, w, [this, pair] {
            auto path =
                QFileDialog::getOpenFileName(this, main->cfg.T("Notification sound"), {}, "WAV (*.wav)");
            if (!path.isEmpty()) {
                auto a = main->cfg.feature("alerts");
                a[pair.first] = path;
                main->cfg.setFeature("alerts", a);
            }
        }));
    }
    soundRow->addWidget(action(cfg, "Test", w, [this] { main->hub.sound(false, true); }));
    soundRow->addWidget(action(cfg, "Default", w, [this] {
        auto a = main->cfg.feature("alerts");
        a.remove("sound_file");
        a.remove("live_sound_file");
        main->cfg.setFeature("alerts", a);
    }));
    l->addLayout(soundRow);
    auto *watch = new QListWidget(w);
    watch->setMaximumHeight(150);
    for (const auto &v : settings["watch"].toArray()) {
        auto item = v.toObject();
        if (str(item["platform"]) == "youtube") {
            continue;
        }
        auto *row = new QListWidgetItem(str(item["name"]) + " (@" + str(item["id"]) + ')', watch);
        row->setData(Qt::UserRole, item);
    }
    l->addWidget(watch);
    auto *watchInput = new QLineEdit(w);
    watchInput->setPlaceholderText(cfg.T("Additional Twitch @channel"));
    auto *watchRow = new QHBoxLayout;
    watchRow->addWidget(watchInput, 1);
    watchRow->addWidget(action(cfg, "Add", w, [this, watch, watchInput] {
        QString login =
            twitchLogin(watchInput->text().startsWith('@') ? watchInput->text() : '@' + watchInput->text());
        if (login.isEmpty()) {
            return;
        }
        auto *row = new QListWidgetItem('@' + login, watch);
        row->setData(Qt::UserRole, QJsonObject{{"platform", "twitch"}, {"id", login}, {"name", login}});
        watchInput->clear();
    }));
    watchRow->addWidget(action(cfg, "Remove", w, [watch] { delete watch->takeItem(watch->currentRow()); }));
    l->addLayout(watchRow);
    l->addWidget(text(cfg,
                      "Twitch checks run once a minute while the app is open. Alerts trigger on offline → "
                      "live, with no sound on initial detection.",
                      true));
    l->addWidget(action(cfg, "Apply notifications", w, [this, enabled, mentions, automatic, names, watch] {
        auto a = main->cfg.feature("alerts");
        a["enabled"] = enabled->isChecked();
        a["mention_sound"] = mentions->isChecked();
        a["all_twitch"] = automatic->isChecked();
        QJsonArray list;
        for (int i = 0; i < watch->count(); ++i) {
            list.append(watch->item(i)->data(Qt::UserRole).toJsonObject());
        }
        a["watch"] = list;
        auto f = main->cfg.features();
        f["alerts"] = a;
        f["nicknames"] = strings(words(names->text()));
        main->cfg.data["features"] = f;
        main->cfg.save();
        emit main->cfg.appearanceChanged();
    }));
    l->addStretch();
    return w;
}

QWidget *SettingsWindow::savedPage() {
    QVBoxLayout *l;
    auto *w = pageWidget(main->cfg, "Saved channels", &l);
    l->addWidget(text(main->cfg,
                      "Save channels from an open chat's Channel actions. YouTube finds current streams only "
                      "when you click Open.",
                      true));
    auto *list = new QListWidget(w);
    auto refresh = [this, list] {
        list->clear();
        for (const auto &v : main->cfg.data["saved_channels"].toArray()) {
            auto d = v.toObject();
            auto *item = new QListWidgetItem(
                (str(d["platform"]) == "twitch" ? "TW · " : "YT · ") + str(d["title"]), list);
            item->setData(Qt::UserRole, d);
        }
    };
    refresh();
    l->addWidget(list, 1);
    auto *addRow = new QHBoxLayout;
    auto *platform = new QComboBox(w);
    platform->addItems({"Twitch", "YouTube"});
    auto *address = new QLineEdit(w);
    address->setPlaceholderText(main->cfg.T("@channel or channel URL"));
    addRow->addWidget(platform);
    addRow->addWidget(address, 1);
    addRow->addWidget(action(main->cfg, "Add channel", w, [this, w, platform, address, refresh] {
        QString value = address->text().trimmed();
        if (value.isEmpty()) {
            return;
        }
        auto store = [this, refresh](QString platform, QString id, QString title) {
            auto saved = main->cfg.data["saved_channels"].toArray();
            for (auto row : saved) {
                auto item = row.toObject();
                if (str(item["platform"]) == platform && str(item["channel_id"]) == id) {
                    return;
                }
            }
            saved.append(QJsonObject{{"platform", platform}, {"channel_id", id}, {"title", title}});
            main->cfg.data["saved_channels"] = saved;
            main->cfg.save();
            refresh();
        };
        if (platform->currentIndex() == 0) {
            QString login = twitchLogin(value.startsWith('@') || value.contains('/') ? value : '@' + value);
            if (!login.isEmpty()) {
                store("twitch", login, login);
                address->clear();
            }
            return;
        }
        QUrl url(value.startsWith('@')    ? "https://www.youtube.com/" + value
                 : value.startsWith("UC") ? "https://www.youtube.com/channel/" + value
                                          : value);
        if (url.scheme().isEmpty()) {
            url = QUrl("https://" + value);
        }
        if (url.scheme() != "https" || !(url.host() == "youtube.com" || url.host() == "www.youtube.com") ||
            !url.userInfo().isEmpty()) {
            return;
        }
        QPointer<QWidget> safe(w);
        main->net.bytes(
            url,
            [this, safe, store, address](QByteArray bytes, QString error) {
                if (!safe) {
                    return;
                }
                auto d = embeddedJson(QString::fromUtf8(bytes), "ytInitialData");
                auto meta = at(d, {"metadata", "channelMetadataRenderer"}).toObject();
                QString id = str(meta["externalId"]);
                if (!error.isEmpty() || !QRegularExpression("^UC[A-Za-z0-9_-]{22}$").match(id).hasMatch()) {
                    QMessageBox::warning(
                        this, main->cfg.T("Saved channels"),
                        main->cfg.T(
                            "Could not find that YouTube channel. Save it from an open chat instead."));
                    return;
                }
                store("youtube", id, str(meta["title"]));
                address->clear();
            },
            "GET", {}, {}, 16 * 1024 * 1024);
    }));
    l->addLayout(addRow);
    auto *row = new QHBoxLayout;
    row->addWidget(action(main->cfg, "Open", w, [this, list] {
        if (auto *i = list->currentItem()) {
            main->openSaved(i->data(Qt::UserRole).toJsonObject());
        }
    }));
    row->addWidget(action(main->cfg, "Remove", w, [this, list, refresh] {
        int index = list->currentRow();
        if (index < 0) {
            return;
        }
        auto saved = main->cfg.data["saved_channels"].toArray();
        saved.removeAt(index);
        main->cfg.data["saved_channels"] = saved;
        main->cfg.save();
        refresh();
    }));
    row->addWidget(action(main->cfg, "Save current channel", w, [this, refresh] {
        if (auto *p = main->currentPane()) {
            if (auto *s = p->targetSource()) {
                main->saveChannel(s);
            }
        }
        refresh();
    }));
    l->addLayout(row);
    connect(list, &QListWidget::itemActivated, w,
            [this](QListWidgetItem *i) { main->openSaved(i->data(Qt::UserRole).toJsonObject()); });
    return w;
}

QWidget *SettingsWindow::quotaPage() {
    QVBoxLayout *l;
    auto *w = pageWidget(main->cfg, "YouTube quota", &l);
    l->addWidget(text(
        main->cfg,
        "YouTube reading is free of API quota. This estimate is for sending, moderation and editing.", true));
    auto *stats = text(main->cfg, "");
    auto refresh = [this, stats] {
        int left = main->quota.remaining();
        stats->setText(
            main->cfg.T("Approximate units left: {count}", {{"count", main->cfg.number(left)}}) + "\n" +
            main->cfg.T("Units used today: {count}", {{"count", main->cfg.number(main->quota.used())}}) +
            "\n" + main->cfg.T("Messages: ~{count}", {{"count", main->cfg.number(qMax(0, left - 1) / 20)}}) +
            "\n" + main->cfg.T("Deletions: ~{count}", {{"count", main->cfg.number(left / 26)}}) + "\n" +
            main->cfg.T("Bans / timeouts: ~{count}", {{"count", main->cfg.number(left / 200)}}));
    };
    refresh();
    l->addWidget(stats);
    connect(&main->quota, &Quota::changed, w, refresh);
    auto *form = new QFormLayout;
    auto *limit = new QSpinBox(w);
    limit->setRange(1, 10000000);
    limit->setValue(main->cfg.data["youtube_daily_limit"].toInt(10000));
    auto *external = new QSpinBox(w);
    external->setRange(0, 10000000);
    auto adjustment = main->cfg.data["youtube_external_usage"].toObject();
    external->setValue(adjustment["date"].toString() == main->quota.day() ? adjustment["used"].toInt() : 0);
    form->addRow(main->cfg.T("Daily project limit"), limit);
    form->addRow(main->cfg.T("Units used outside this app today"), external);
    l->addLayout(form);
    l->addWidget(text(main->cfg,
                      "Each count is an alternative use of the same balance. Deletions allow for an ID "
                      "lookup; first actions may need 1 extra unit.",
                      true));
    l->addWidget(action(main->cfg, "Apply", w, [this, limit, external, refresh] {
        main->cfg.data["youtube_daily_limit"] = limit->value();
        main->cfg.data["youtube_external_usage"] =
            QJsonObject{{"date", main->quota.day()}, {"used", external->value()}};
        main->cfg.save();
        main->refreshQuota();
        refresh();
    }));
    l->addStretch();
    return w;
}

QWidget *SettingsWindow::updatesPage() {
    QVBoxLayout *l;
    auto *w = pageWidget(main->cfg, "Updates", &l);
    l->addWidget(text(main->cfg, main->cfg.T("Installed version: {version}", {{"version", APP_VERSION}})));
    auto *automatic = new QCheckBox(main->cfg.T("Check for updates automatically"), w);
    automatic->setChecked(main->cfg.data["automatic_updates"].toBool(true));
    l->addWidget(automatic);
    connect(automatic, &QCheckBox::toggled, w, [this](bool v) {
        main->cfg.data["automatic_updates"] = v;
        main->cfg.save();
        if (v) {
            main->updater.check();
        } else {
            main->updater.timer.stop();
        }
    });
    l->addWidget(text(main->cfg,
                      "Checks run in the background at startup and every six hours. The download arrow "
                      "appears beside the YouTube balance when a new version is ready.",
                      true));
    l->addWidget(action(main->cfg, "Check now / install update…", w, [this] { main->updater.show(); }));
    l->addWidget(text(main->cfg,
                      "Updates require confirmation before downloading and restarting. Your previous app "
                      "files are kept in a backup folder.",
                      true));
    l->addStretch();
    return w;
}

QWidget *SettingsWindow::shortcutsPage() {
    QVBoxLayout *l;
    auto *w = pageWidget(main->cfg, "Keyboard shortcuts", &l);
    auto *table = new QTableWidget(w);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({main->cfg.T("Shortcut"), main->cfg.T("Action")});
    QList<QPair<QString, QString>> rows{{"Ctrl+T", "Add chats"},
                                        {"Ctrl+W", "Close current pane"},
                                        {"Ctrl+Shift+W", "Close current tab"},
                                        {"Ctrl+Shift+T", "Add a split"},
                                        {"Ctrl+1–8 / Ctrl+9", "Select tab / last tab"},
                                        {"Ctrl+Alt+Shift+T", "Reopen closed tab"},
                                        {"Ctrl+Tab / Ctrl+Shift+Tab", "Switch tabs"},
                                        {"Ctrl+F", "Search current pane"},
                                        {"Ctrl+Shift+F", "Search all chats"},
                                        {"Ctrl+P", "Appearance settings"},
                                        {"Ctrl+K", "Switch chat"},
                                        {"Ctrl+F5", "Reconnect current pane"},
                                        {"Alt+Arrows", "Move pane focus"},
                                        {"Ctrl+Alt+M", "Mix channels"},
                                        {"Ctrl++ / Ctrl+-", "Bigger / smaller text"},
                                        {"Ctrl+0", "Reset text size"},
                                        {"Tab / Shift+Tab", "Complete emotes or @names"},
                                        {"Mouse wheel over tabs", "Switch tabs"},
                                        {"Middle-click empty tab bar", "Add chats"},
                                        {"Left-click username", "User information"},
                                        {"Left-click reply body", "Open reply thread"},
                                        {"Right-click message", "Reply to message"},
                                        {"Ctrl+right-click message", "Message actions / moderation"},
                                        {"Middle-click chat", "Toggle autoscroll"},
                                        {"Esc / click", "Stop autoscroll"}};
    table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(main->cfg.T(rows[i].first)));
        table->setItem(i, 1, new QTableWidgetItem(main->cfg.T(rows[i].second)));
    }
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    table->resizeColumnToContents(0);
    l->addWidget(table, 1);
    return w;
}

QWidget *SettingsWindow::tutorialPage() {
    QVBoxLayout *l;
    auto *w = pageWidget(main->cfg, "Tutorial", &l);
    for (const auto &section : QList<QPair<QString, QString>>{
             {"Read chats", "Click +. Paste a YouTube live link or Twitch @channel. Reading YouTube needs no "
                            "API key or account. YouTube stays connected in background tabs."},
             {"Twitch: connect, send and moderate",
              "Settings → Accounts shows your sign-in and enabled permissions. Connect Twitch there if "
              "needed and approve the code in your browser. Right-click a message to prepare a reply; "
              "Ctrl+right-click opens moderation actions. Moderation requires the channel's owner/moderator "
              "role; title/category edits require the broadcaster account."},
             {"YouTube: connect, send and moderate",
              "Create a Google Cloud project, enable YouTube Data API v3, configure the consent screen and "
              "create a Desktop OAuth client. Download its JSON. "
              "In Accounts choose the JSON, then Sign in to YouTube and approve in your browser. Select your "
              "channel or Brand Account. Sending uses about 20 units; deletion about 20–26; bans/timeouts "
              "200. You must be the owner/moderator to moderate. Public reading stays free."},
             {"Google sign-in blocked (403 / access_denied)", Accounts::googleSignInHelp()},
             {"YouTube chat limited (HTTP 429)",
              "Web reading may be temporarily limited by Google. Sign in to YouTube in Accounts, then "
              "right-click the chat tab → channel → Official API fallback and confirm its quota cost. "
              "When web reading is limited, the fallback uses 5 units per request, about 986 units per "
              "hour per chat at the default 18.25 seconds. Multiple chats use more. Change the interval "
              "in the same channel menu. API reading stops when the estimated quota is exhausted."},
             {"Tabs, splits and saved channels",
              "Ctrl+T adds chats. Choose separate tabs, side by side, stacked or mixed. Drag tabs and "
              "dividers; scroll over tabs to switch. Middle-click empty tab space for a new chat. "
              "Right-click headers for layout and browser actions. Save channels there; open them later in "
              "Saved channels. YouTube checks broadcasts only when you open a save."},
             {"Messages, emotes and reply threads",
              "Ctrl+F searches a pane; Ctrl+Shift+F searches all. Tab completes emotes and @names; ☺ opens "
              "the picker. Left-click names for user cards; right-click to reply and Ctrl+right-click for "
              "actions. "
              "Left-click a Twitch reply to open its retained thread. Ctrl+F5 reconnects. Pause YouTube "
              "manually from the header menu or pause all from the gear menu. Middle-click the chat for "
              "autoscroll; move the mouse to choose speed, then click or press Esc to stop."},
             {"Appearance, highlights and sounds",
              "Appearance has separate text/name fonts, badge/emote sizes, timestamps and date formats. "
              "Messages sets the retention cap (default 2000). Highlights matches users, badges, names and "
              "phrases. Notifications plays configurable WAV sounds for mentions and Twitch live "
              "transitions. YouTube has no live alerts. Updates are checked in the background and installed "
              "only after confirmation."}}) {
        auto *title = text(main->cfg, section.first);
        title->setFont(QFont("Segoe UI", 11, QFont::Bold));
        l->addWidget(title);
        l->addWidget(text(main->cfg, section.second, true));
        if (section.first == "YouTube: connect, send and moderate") {
            auto *requirement = text(main->cfg,
                                     "If the OAuth app is in Testing, adding the exact Google account used "
                                     "to sign in under Google Auth Platform → Audience → Test users is required.");
            requirement->setObjectName("google-testing-requirement");
            requirement->setStyleSheet("background:#102016;color:#c9ebce;border:1px solid #285b34;padding:8px;");
            l->addSpacing(5);
            l->addWidget(requirement);
        }
        l->addSpacing(10);
    }
    l->addStretch();
    return w;
}
} // namespace yc
