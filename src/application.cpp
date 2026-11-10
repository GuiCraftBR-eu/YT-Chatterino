#include "application.hpp"
#include "phone.hpp"
#include "vod.hpp"
#include "vendor/qrcodegen.hpp"
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace yc {
class StandardTranslator final : public QTranslator {
    Config *cfg;

  public:
    explicit StandardTranslator(Config *c, QObject *parent) : QTranslator(parent), cfg(c) {}

    QString translate(const char *, const char *source, const char *, int) const override {
        if (cfg->language() != "pt-BR") {
            return {};
        }
        QString key = QString::fromUtf8(source);
        key.remove('&');
        auto translated = cfg->T(key);
        return translated == key ? QString() : translated;
    }

    bool isEmpty() const override {
        return false;
    }
};

static QPushButton *button(Config &cfg, QString text, QObject *receiver, std::function<void()> action) {
    auto *b = new QPushButton(cfg.T(text));
    QObject::connect(b, &QPushButton::clicked, receiver, std::move(action));
    return b;
}

static QLabel *label(Config &cfg, QString text, bool dim = false) {
    auto *l = new QLabel(cfg.T(text));
    l->setWordWrap(true);
    l->setTextFormat(Qt::PlainText);
    if (dim) {
        l->setStyleSheet("color:#8b8b94;");
    }
    return l;
}

static QJsonArray arrayOf(const QStringList &values) {
    return QJsonArray::fromStringList(values);
}

MainWindow::MainWindow(QString profile, bool testing)
    : cfg(profile, this), net(this), quota(&cfg), accounts(&cfg, &net, &quota, this), images(&net, this),
      hub(&cfg, &net, &accounts, &images, &quota, this), updater(&cfg, &net, this, this), testMode(testing) {
    hub.testMode = testing;
    accounts.persistenceEnabled = !testing;
    accounts.window = this;
    standardTranslator = new StandardTranslator(&cfg, this);
    qApp->installTranslator(standardTranslator);
    setWindowTitle(QStringLiteral("YT-Chatterino " APP_VERSION "v"));
    setWindowIcon(QIcon(":/ytchatterino.ico"));
    resize(520, 760);
    setMinimumSize(340, 300);
    tabs = new QTabWidget(this);
    tabs->setObjectName("chat-tabs");
    tabs->setTabsClosable(true);
    tabs->setMovable(true);
    tabs->setDocumentMode(true);
    tabs->tabBar()->setDrawBase(false);
    tabs->tabBar()->setObjectName("chat-tabbar");
    tabs->tabBar()->setElideMode(Qt::ElideRight);
    tabs->tabBar()->setUsesScrollButtons(true);
    tabs->setIconSize(QSize(10, 10));
    tabs->setStyleSheet("QTabBar#chat-tabbar::tab{background:#101012;color:#bdbdc3;"
                        "padding:4px 7px;border-top:2px solid transparent;}"
                        "QTabBar#chat-tabbar::tab:hover:!selected{background:#1b1b1e;color:#fff;}"
                        "QTabBar#chat-tabbar::tab:selected{background:#202024;color:#fff;"
                        "border-top:2px solid #4a9cdb;}"
                        "QTabBar#chat-tabbar::close-button{image:url(:/close.svg);width:11px;height:11px;}"
                        "QTabBar#chat-tabbar::close-button:hover{background:#444;border-radius:2px;}"
                        "QWidget#chat-controls{background:#101012;border-left:1px solid #28282d;}"
                        "QToolButton#chat-control{background:transparent;color:#d2d2d8;"
                        "border:0;border-radius:3px;padding:0;}"
                        "QToolButton#chat-control:hover{background:#303037;color:#fff;}"
                        "QToolButton#chat-control:pressed{background:#3b3b44;}");
    setCentralWidget(tabs);
    tabs->tabBar()->installEventFilter(this);
    tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *strip = new QWidget(tabs);
    strip->setObjectName("chat-controls");
    auto *sl = new QHBoxLayout(strip);
    sl->setContentsMargins(3, 2, 3, 2);
    sl->setSpacing(2);
    addButton = new QToolButton(strip);
    addButton->setText("+");
    quotaLabel = new QLabel(strip);
    quotaLabel->setStyleSheet("color:#a4a4ad;font-size:10px;padding:0 3px;");
    updateArrow = new QToolButton(strip);
    updateArrow->setText("⬇");
    updateArrow->setStyleSheet("color:#78c34b;");
    updateArrow->hide();
    gear = new QToolButton(strip);
    gear->setText("⚙");
    for (auto *control : {addButton, updateArrow, gear}) {
        control->setObjectName("chat-control");
        control->setFixedSize(23, 23);
    }
    sl->addWidget(addButton);
    sl->addWidget(quotaLabel);
    sl->addWidget(updateArrow);
    sl->addWidget(gear);
    tabs->setCornerWidget(strip, Qt::TopRightCorner);
    connect(addButton, &QToolButton::clicked, this, [this] { addChats(); });
    connect(gear, &QToolButton::clicked, this, [this] { showSettings(); });
    gear->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(gear, &QWidget::customContextMenuRequested, this, [this] { gearMenu(); });
    connect(updateArrow, &QToolButton::clicked, &updater, &Updater::show);
    connect(&updater, &Updater::changed, this, [this] {
        updateArrow->setVisible(updater.hasUpdate());
        updateArrow->setToolTip(
            cfg.T("Update to {version}", {{"version", str(updater.available.value("version"))}}));
    });
    connect(&updater, &Updater::restartRequested, this, [this] {
        saveSession();
        qApp->quit();
    });
    connect(tabs, &QTabWidget::currentChanged, this, [this] {
        refreshQuota();
        sessionTimer.start();
    });
    connect(tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(tabs->tabBar(), &QTabBar::tabMoved, this, [this] { sessionTimer.start(); });
    connect(tabs->tabBar(), &QWidget::customContextMenuRequested, this,
            [this](QPoint p) { tabMenu(tabs->tabBar()->tabAt(p), tabs->tabBar()->mapToGlobal(p)); });
    sessionTimer.setInterval(650);
    sessionTimer.setSingleShot(true);
    connect(&sessionTimer, &QTimer::timeout, this, &MainWindow::saveSession);
    connect(&hub, &Hub::sourceChanged, this, [this](Source *) {
        refreshTabs();
        refreshQuota();
    });
    connect(&hub, &Hub::notice, this, [this](const QString &text) { statusBar()->showMessage(text, 10000); });
    connect(&accounts, &Accounts::status, this,
            [this](const QString &text) { statusBar()->showMessage(cfg.T(text), 12000); });
    connect(&accounts, &Accounts::ready, this, [this] {
        for (auto s : hub.sources) {
            if (s->platform == "twitch" && !s->isVod) {
                s->emoteLoaded.clear();
                s->globalBadgesLoaded = s->channelBadgesLoaded = 0;
                s->refreshHeader();
                s->loadEmotes(s->room());
                s->loadBadges();
            }
        }
    });
    connect(&quota, &Quota::changed, this, &MainWindow::refreshQuota);
    connect(&cfg, &Config::appearanceChanged, this, [this] {
        for (auto s : hub.sources) {
            s->trim();
        }
        refreshTabs();
        refreshQuota();
    });
    connect(&cfg, &Config::languageChanged, this, &MainWindow::refreshLanguage);
    auto shortcut = [this](QString key, std::function<void()> fn) {
        auto *s = new QShortcut(QKeySequence(key), this);
        connect(s, &QShortcut::activated, this, std::move(fn));
    };
    shortcut("Ctrl+T", [this] { addChats(); });
    shortcut("Ctrl+W", [this] {
        auto *w = current();
        auto *p = currentPane();
        if (!w || !p) {
            return;
        }
        if (w->panes().size() == 1) {
            closeTab(tabs->currentIndex());
        } else {
            w->closePane(p);
        }
        QTimer::singleShot(0, this, &MainWindow::pruneSources);
        sessionTimer.start();
    });
    shortcut("Ctrl+Shift+W", [this] { closeTab(tabs->currentIndex()); });
    shortcut("Ctrl+Shift+T", [this] { addChats("horizontal", currentPane()); });
    for (int n = 1; n <= 9; ++n) {
        shortcut("Ctrl+" + QString::number(n), [this, n] {
            if (tabs->count()) {
                tabs->setCurrentIndex(n == 9 ? tabs->count() - 1 : qMin(n - 1, tabs->count() - 1));
            }
        });
    }
    shortcut("Ctrl+Alt+Shift+T", [this] {
        if (closedTabs.isEmpty()) {
            return;
        }
        auto saved = closedTabs.takeAt(closedTabs.size() - 1).toObject();
        auto *w = new Workspace(&hub, tabs);
        connect(w, &Workspace::paneAdded, this, &MainWindow::attachPane);
        w->restore(saved);
        tabs->setCurrentIndex(tabs->addTab(w, w->title));
        refreshTabs();
    });
    shortcut("Ctrl+Tab", [this] {
        if (tabs->count()) {
            tabs->setCurrentIndex((tabs->currentIndex() + 1) % tabs->count());
        }
    });
    shortcut("Ctrl+Shift+Tab", [this] {
        if (tabs->count()) {
            tabs->setCurrentIndex((tabs->currentIndex() + tabs->count() - 1) % tabs->count());
        }
    });
    shortcut("Ctrl+F", [this] {
        if (currentPane()) {
            currentPane()->showSearch();
        }
    });
    shortcut("Ctrl+Shift+F", [this] { globalSearch(); });
    shortcut("Ctrl+P", [this] { showSettings("Display"); });
    shortcut("Ctrl+K", [this] { quickSwitch(); });
    shortcut("F5", [this] {
        if (auto *p = currentPane()) {
            QSet<Source *> refreshed;
            for (auto s : p->sources) if (s && !refreshed.contains(s)) {
                refreshed.insert(s); s->reloadChannelEmotes();
            }
        }
    });
    shortcut("Ctrl+F5", [this] {
        if (auto *p = currentPane()) {
            for (auto s : p->sources) {
                if (s) {
                    s->reconnect();
                }
            }
        }
    });
    shortcut("Ctrl+Alt+M", [this] { addChats("mix", currentPane()); });
    shortcut("Ctrl++", [this] { changeFont(1); });
    shortcut("Ctrl+=", [this] { changeFont(1); });
    shortcut("Ctrl+-", [this] { changeFont(-1); });
    shortcut("Ctrl+0", [this] { changeFont(0, true); });
    for (const auto &dir : QStringList{"Left", "Right", "Up", "Down"}) {
        shortcut("Alt+" + dir, [this, dir] {
            auto *w = current();
            auto *p = currentPane();
            if (!w || !p) {
                return;
            }
            QPoint origin = p->mapToGlobal(p->rect().center());
            Pane *best = nullptr;
            int distance = INT_MAX;
            for (auto candidate : w->panes()) {
                if (candidate == p) {
                    continue;
                }
                QPoint d = candidate->mapToGlobal(candidate->rect().center()) - origin;
                bool good = dir == "Left"    ? d.x() < 0
                            : dir == "Right" ? d.x() > 0
                            : dir == "Up"    ? d.y() < 0
                                             : d.y() > 0;
                int score = d.manhattanLength();
                if (good && score < distance) {
                    best = candidate;
                    distance = score;
                }
            }
            if (best) {
                w->focusedPane = best;
                best->input->setFocus();
                refreshQuota();
            }
        });
    }
    restoreSession();
    if (!testMode) startVodBridge();
    applyTopmost();
    refreshLanguage();
    if (!testMode) {
        accounts.load();
        updater.start();
    }
}

Workspace *MainWindow::current() const {
    return qobject_cast<Workspace *>(tabs->currentWidget());
}

MainWindow::~MainWindow() {
    qApp->removeTranslator(standardTranslator);
    sessionTimer.stop();
    hub.liveTimer.stop();
    hub.twitch.close();
    for (auto *reply : net.manager.findChildren<QNetworkReply *>()) {
        reply->disconnect();
        reply->abort();
    }
    for (auto *dialog : findChildren<QDialog *>(QString(), Qt::FindDirectChildrenOnly)) {
        dialog->disconnect(this);
        delete dialog;
    }
    delete takeCentralWidget();
    tabs = nullptr;
}

Pane *MainWindow::currentPane() const {
    auto *w = current();
    return w ? w->focusedPane.data() : nullptr;
}

Workspace *MainWindow::addWorkspace(QList<Source *> sources, QString title) {
    if (tabs->count() == 1 && tabs->widget(0)->objectName() == "empty-placeholder") {
        auto *empty = tabs->widget(0);
        tabs->removeTab(0);
        empty->deleteLater();
    }
    auto *w = new Workspace(&hub, tabs);
    w->title = title;
    connect(w, &Workspace::paneAdded, this, &MainWindow::attachPane);
    connect(w, &Workspace::changed, this, [this] {
        sessionTimer.start();
        refreshTabs();
        refreshQuota();
    });
    tabs->setCurrentIndex(tabs->addTab(w, title));
    w->add(sources);
    refreshTabs();
    return w;
}

void MainWindow::attachPane(Pane *p) {
    p->openBrowserPanel = [this](Source *source, const QString &kind) {
        return vodBridge && vodBridge->openTwitchPanel(source, kind);
    };
    auto markVodSources = [this, p] {
        for (auto source : p->sources) if (source && source->isVod) {
            source->header["vod_shown"] = true;
            if (vodBridge) vodBridge->allow(source->key);
        }
    };
    markVodSources();
    connect(p, &Pane::sourcesChanged, this, [this, markVodSources] {
        markVodSources();
        if (!restoring) { sessionTimer.start(); refreshTabs(); QTimer::singleShot(0, this, &MainWindow::pruneSources); }
    });
    connect(p, &Pane::headerLinesChanged, this, [this] { sessionTimer.start(); });
    connect(p, &Pane::focused, this, [this](Pane *pane) {
        if (current()) {
            current()->focusedPane = pane;
        }
        refreshQuota();
    });
    connect(p, &Pane::menuRequested, this, &MainWindow::paneMenu);
    connect(p, &Pane::filtersRequested, this, &MainWindow::filters);
    connect(p, &Pane::phoneRequested, this, &MainWindow::showPhone);
    connect(p, &Pane::messageMenu, this, &MainWindow::messageMenu);
    connect(p, &Pane::userRequested, this, &MainWindow::showUser);
    connect(p, &Pane::threadRequested, this, &MainWindow::showThread);
}

void MainWindow::refreshTabs() {
    if (!tabs->count()) {
        auto *empty = new QWidget(tabs);
        empty->setObjectName("empty-placeholder");
        auto *layout = new QVBoxLayout(empty);
        auto *hint = new QLabel(cfg.T("Click + to add chats.\nChoose a split or mixed layout in the dialog."), empty);
        hint->setAlignment(Qt::AlignCenter);
        hint->setStyleSheet("color:#8b8b94;");
        layout->addWidget(hint);
        tabs->addTab(empty, QString());
        tabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
        tabs->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
    }
    for (int i = 0; i < tabs->count(); ++i) {
        auto *w = qobject_cast<Workspace *>(tabs->widget(i));
        if (!w) {
            continue;
        }
        bool live = false;
        for (auto *pane : w->panes()) for (auto source : pane->sources)
            live |= source && !source->isVod && source->header.value("live").toBool();
        static const QIcon liveIcon = [] {
            QPixmap pixmap(10, 10);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor("#eb3349"));
            painter.drawEllipse(QRectF(1, 1, 8, 8));
            return QIcon(pixmap);
        }();
        if (tabs->tabIcon(i).isNull() == live) tabs->setTabIcon(i, live ? liveIcon : QIcon());
        QString title = w->title;
        if (title.isEmpty()) {
            QStringList names;
            for (auto *p : w->panes()) {
                for (auto s : p->sources) {
                    if (s && !names.contains(s->name())) {
                        names << s->name();
                    }
                }
            }
            title = names.join(" + ");
        }
        const auto label = title.isEmpty() ? cfg.T("Chat") : title;
        if (tabs->tabText(i) != label) tabs->setTabText(i, label);
        if (tabs->tabToolTip(i) != title) tabs->setTabToolTip(i, title);
    }
}

void MainWindow::refreshQuota() {
    bool yt = false;
    if (auto *p = currentPane()) {
        for (auto s : p->sources) {
            if (s && s->platform == "youtube" && !s->isVod) {
                yt = true;
            }
        }
    }
    quotaLabel->setVisible(yt);
    quotaLabel->setText("YT ~" + cfg.number(quota.remaining()) + " u");
    quotaLabel->setToolTip(cfg.T("Approximate YouTube units left for sending and moderation"));
}

void MainWindow::refreshLanguage() {
    addButton->setToolTip(cfg.T("Add chats (Ctrl+T)"));
    gear->setToolTip(cfg.T("Settings (Ctrl+P)"));
    refreshTabs();
    refreshQuota();
    for (auto *p : findChildren<Pane *>()) {
        p->refresh();
    }
    if (settings) {
        settings->rebuildLanguage();
    }
}

static QStringList liveStreamChoices(const QJsonArray &streams) {
    QStringList names;
    for (const auto &stream : streams) {
        const auto item = stream.toObject();
        const auto title = str(item["title"]);
        names << (title.isEmpty() ? str(item["id"]) : title);
    }
    QStringList choices = names;
    for (int i = 0; i < names.size(); ++i)
        if (names.count(names[i]) > 1) choices[i] += " · " + str(streams[i].toObject()["id"]);
    return choices;
}

static QString suggestedMixKey(QString name) {
    name = name.normalized(QString::NormalizationForm_KD).toCaseFolded();
    name.remove(QRegularExpression("[^a-z0-9]"));
    if (name.endsWith("live") && name.size() >= 7) name.chop(4);
    return name.size() >= 3 ? name : QString();
}

void MainWindow::addChats(QString mode, Pane *anchor) {
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(cfg.T("Add chats"));
    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(label(cfg, "Paste a link or enter a channel name"));
    auto *combo = new QComboBox(dialog);
    combo->setObjectName("add-chat-layout");
    for (const auto &name : QStringList{"Separate tabs", "Side by side", "Stacked", "Mixed feed"}) combo->addItem(cfg.T(name));
    QStringList modes{"tabs", "horizontal", "vertical", "mix"};
    combo->setCurrentIndex(qMax(0, modes.indexOf(mode)));
    auto *entry = new QHBoxLayout;
    auto *platform = new QComboBox(dialog);
    platform->setObjectName("add-chat-platform");
    platform->addItems({cfg.T("Auto"), "Twitch", "Kick", "YouTube", cfg.T("VOD link")});
    auto *input = new QLineEdit(dialog);
    input->setObjectName("add-chat-input");
    input->setPlaceholderText(cfg.T("Link or name · separate chats with space"));
    input->setToolTip(cfg.T("For YouTube names with spaces, use quotation marks."));
    input->setClearButtonEnabled(true);
    entry->addWidget(platform); entry->addWidget(input, 1);
    layout->addLayout(entry);
    auto *youtubeHint = label(cfg, "For YouTube names with spaces, use quotation marks.", true);
    youtubeHint->setObjectName("add-chat-youtube-hint");
    youtubeHint->hide();
    layout->addWidget(youtubeHint);
    connect(platform, &QComboBox::currentIndexChanged, youtubeHint,
            [youtubeHint](int index) { youtubeHint->setVisible(index == 3); });
    layout->addWidget(combo);
    auto *saved = new QListWidget(dialog);
    saved->setObjectName("add-chat-saved");
    saved->setStyleSheet("QListWidget#add-chat-saved{background:#111;border:1px solid #333;outline:0;}"
                         "QListWidget#add-chat-saved::item{min-height:22px;padding:2px 4px;}"
                         "QListWidget#add-chat-saved::item:hover{background:#222;}"
                         "QListWidget#add-chat-saved::item:selected{background:#25394a;}"
                         "QListWidget#add-chat-saved::indicator{width:14px;height:14px;background:#171717;"
                         "border:1px solid #777;border-radius:2px;}"
                         "QListWidget#add-chat-saved::indicator:checked{background:#285b7a;border-color:#4a9cdb;"
                         "image:url(:/settings-check.svg);}");
    for (const auto &value : cfg.data["saved_channels"].toArray()) {
        const auto item = value.toObject();
        const auto platformName = str(item["platform"]);
        if (platformName != "twitch" && platformName != "kick" && platformName != "youtube") continue;
        auto *row = new QListWidgetItem((platformName == "twitch" ? "Twitch" : platformName == "kick" ? "Kick" : "YouTube") +
                                        QString(" · ") + str(item["title"]), saved);
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        row->setCheckState(Qt::Unchecked);
        row->setData(Qt::UserRole, item);
    }
    if (saved->count()) {
        saved->setFixedHeight(qMin(150, 6 + 27 * saved->count()));
        layout->addWidget(label(cfg, "Select saved channels"));
        layout->addWidget(saved);
    } else {
        saved->hide();
    }
    auto *vods = new QListWidget(dialog);
    vods->setObjectName("add-chat-vods");
    auto *vodLabel = label(cfg, "Select open VOD chats");
    layout->addWidget(vodLabel);
    layout->addWidget(vods);
    auto addVod = [this, vods, vodLabel](Source *source) {
        if (!source->isVod || (!source->header["vod_shown"].toBool() &&
            !(vodBridge && vodBridge->owners.contains(source->key) && source->vodReplay && source->vodReplay->announced))) return;
        for (int i = 0; i < vods->count(); ++i)
            if (vods->item(i)->data(Qt::UserRole).toString() == source->vodUrl.toString()) return;
        auto *row = new QListWidgetItem(source->name() + " · VOD", vods);
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        row->setCheckState(Qt::Unchecked);
        row->setData(Qt::UserRole, source->vodUrl.toString());
        vods->setFixedHeight(qMin(150, 6 + 27 * vods->count()));
        vodLabel->show(); vods->show();
    };
    vodLabel->hide(); vods->hide();
    for (auto *source : hub.sources) addVod(source);
    if (vodBridge) connect(vodBridge, &VodBridge::discovered, dialog, addVod);
    auto *liveLabel = label(cfg, "Live streams open in your browser");
    auto *liveList = new QListWidget(dialog); liveList->setObjectName("add-chat-browser-live");
    auto *suggestLabel = label(cfg, "Suggested mixed chats");
    auto *suggestions = new QListWidget(dialog); suggestions->setObjectName("add-chat-browser-mixes");
    liveList->setStyleSheet(saved->styleSheet().replace("add-chat-saved", "add-chat-browser-live"));
    layout->addWidget(liveLabel); layout->addWidget(liveList);
    layout->addWidget(suggestLabel); layout->addWidget(suggestions);
    auto refreshLive = [this, liveList, liveLabel, suggestions, suggestLabel] {
        QSet<QString> checked;
        for (int i=0;i<liveList->count();++i) if (liveList->item(i)->checkState()==Qt::Checked)
            checked.insert(liveList->item(i)->data(Qt::UserRole+1).toString());
        QMap<QString,QJsonObject> unique;
        if (vodBridge) for (const auto &item : vodBridge->liveItems) {
            const auto key = str(item["platform"]) + ':' + str(item["id"]);
            if (!key.endsWith(':')) unique[key]=item;
        }
        liveList->clear(); suggestions->clear();
        QMap<QString,QList<QJsonObject>> groups;
        for (auto it=unique.cbegin();it!=unique.cend();++it) {
            const auto item=it.value(); const QString platform=str(item["platform"]), name=str(item["name"]);
            auto *row=new QListWidgetItem(QString(platform=="youtube" ? "YouTube" : platform=="twitch" ? "Twitch" : "Kick") + " · " + name,liveList);
            row->setFlags(row->flags()|Qt::ItemIsUserCheckable);
            row->setCheckState(checked.contains(it.key()) ? Qt::Checked : Qt::Unchecked);
            row->setData(Qt::UserRole,item); row->setData(Qt::UserRole+1,it.key());
            const auto group=suggestedMixKey(name); if(!group.isEmpty()) groups[group]<<item;
        }
        for(auto it=groups.cbegin();it!=groups.cend();++it) if(it.value().size()>1) {
            QStringList names; QJsonArray keys;
            for(const auto &item:it.value()) {
                const QString platform=str(item["platform"]);
                names << (platform=="youtube" ? "YouTube" : platform=="twitch" ? "Twitch" : "Kick") + QString(" · ") + str(item["name"]);
                keys.append(platform + ':' + str(item["id"]));
            }
            auto *row=new QListWidgetItem(names.join("  +  "),suggestions); row->setData(Qt::UserRole,keys);
        }
        liveList->setFixedHeight(qMin(150,6+27*liveList->count()));
        suggestions->setFixedHeight(qMin(100,6+27*suggestions->count()));
        liveLabel->setVisible(liveList->count()>0); liveList->setVisible(liveList->count()>0);
        suggestLabel->setVisible(suggestions->count()>0); suggestions->setVisible(suggestions->count()>0);
    };
    auto chooseMix = [liveList,combo](QListWidgetItem *item) {
        QSet<QString> keys; for(const auto &value:item->data(Qt::UserRole).toJsonArray()) keys.insert(value.toString());
        for(int i=0;i<liveList->count();++i) if(keys.contains(liveList->item(i)->data(Qt::UserRole+1).toString()))
            liveList->item(i)->setCheckState(Qt::Checked);
        combo->setCurrentIndex(3);
    };
    connect(suggestions, &QListWidget::itemClicked, dialog, chooseMix);
    connect(suggestions, &QListWidget::itemActivated, dialog, chooseMix);
    refreshLive();
    if (vodBridge) connect(vodBridge, &VodBridge::liveStreamsChanged, dialog, refreshLive);
    auto *lookupStatus = new QLabel(cfg.T("Looking for live broadcasts…"), dialog);
    lookupStatus->setObjectName("add-chat-status"); lookupStatus->hide();
    layout->addWidget(lookupStatus);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Open)->setText(cfg.T("Open"));
    buttons->button(QDialogButtonBox::Cancel)->setText(cfg.T("Cancel"));
    auto *vod = new QPushButton(cfg.T("Browser extension setup…"), dialog);
    vod->setObjectName("add-vod-replay");
    layout->addWidget(vod);
    connect(vod, &QPushButton::clicked, dialog, [this, dialog] { dialog->accept(); showVod(); });
    layout->addWidget(buttons);
    QPointer<Pane> safe(anchor);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    auto open = [this, input, combo, modes, dialog, safe, platform, saved, vods, liveList, buttons, lookupStatus] {
        if (!buttons->button(QDialogButtonBox::Open)->isEnabled()) return;
        QList<QJsonObject> selections;
        QStringList entries;
        auto tokens = QRegularExpression(R"token("([^"]+)"|([^\s,]+))token").globalMatch(input->text());
        while (tokens.hasNext()) {
            const auto token = tokens.next();
            entries << (token.captured(1).isEmpty() ? token.captured(2) : token.captured(1));
        }
        for (const auto &entry : entries) {
            QString kind, id;
            const auto choice = platform->currentIndex();
            const auto video = videoId(entry);
            // An explicit live/video URL always wins over the selected platform and never
            // goes through channel search. Bare 11-character names remain ambiguous.
            QString vodPlatform, vodId, vodChannel;
            const bool vodLink = VodReplay::identify(QUrl(entry), &vodPlatform, &vodId, &vodChannel);
            if (vodLink && (choice == 4 || vodPlatform != "youtube")) { kind = "vod"; id = entry; }
            else if (choice == 4) { kind = "vod"; id.clear(); }
            else if (!video.isEmpty() && entry != video) { kind = "video"; id = video; }
            else if (choice == 3) {
                kind = "youtube"; id = entry;
            } else if (choice == 2) {
                kind = "kick";
                id = kickLogin(entry);
                if (id.isEmpty()) id = kickLogin("kick:" + entry);
            } else if (choice == 1) {
                kind = "twitch";
                id = twitchLogin(entry);
                if (id.isEmpty()) id = twitchLogin("@" + stripAt(entry));
            } else if (!(id = kickLogin(entry)).isEmpty()) {
                kind = "kick";
            } else if (!(id = twitchLogin(entry)).isEmpty()) {
                kind = "twitch";
            } else {
                kind = "twitch";
                id = twitchLogin("@" + stripAt(entry));
            }
            if (id.isEmpty()) {
                QMessageBox::warning(dialog, cfg.T("Add chats"), cfg.T("Enter a valid channel name or live link."));
                input->setFocus(); return;
            }
            selections << QJsonObject{{"platform", kind}, {"channel_id", id}};
        }
        for (int i = 0; i < saved->count(); ++i)
            if (saved->item(i)->checkState() == Qt::Checked)
                selections << saved->item(i)->data(Qt::UserRole).toJsonObject();
        for (int i = 0; i < vods->count(); ++i)
            if (vods->item(i)->checkState() == Qt::Checked)
                selections << QJsonObject{{"platform", "vod"}, {"channel_id", vods->item(i)->data(Qt::UserRole).toString()}};
        for (int i=0;i<liveList->count();++i) if(liveList->item(i)->checkState()==Qt::Checked) {
            const auto item=liveList->item(i)->data(Qt::UserRole).toJsonObject();
            selections << QJsonObject{{"platform",str(item["platform"])=="youtube" ? "video" : str(item["platform"])},
                                      {"channel_id",item["id"]}};
        }
        if (selections.isEmpty()) { input->setFocus(); return; }
        if (selections.size() > 32) {
            QMessageBox::warning(dialog, cfg.T("Add chats"), cfg.T("Choose at most 32 chats at a time."));
            return;
        }
        buttons->button(QDialogButtonBox::Open)->setEnabled(false);
        struct SelectionState { QList<QJsonObject> pending; QStringList links; };
        auto state = QSharedPointer<SelectionState>::create();
        state->pending = selections;
        QPointer<QDialog> safeDialog(dialog);
        const QString selectedMode = modes[combo->currentIndex()];
        auto step = QSharedPointer<std::function<void()>>::create();
        QWeakPointer<std::function<void()>> weakStep(step);
        *step = [this, safeDialog, safe, selectedMode, state, weakStep, buttons, lookupStatus] {
            if (!safeDialog) return;
            while (!state->pending.isEmpty()) {
                const auto item = state->pending.takeFirst();
                const auto kind = str(item["platform"]), id = str(item["channel_id"]);
                if (kind == "vod") state->links << "vod|" + id;
                else if (kind == "twitch") state->links << "twitch:" + id;
                else if (kind == "kick") state->links << "kick:" + id;
                else if (kind == "video") state->links << "https://www.youtube.com/watch?v=" + id;
                else if (kind == "youtube") {
                    lookupStatus->show();
                    auto next = weakStep.toStrongRef();
                    hub.savedStreams(item, [this, safeDialog, state, next, buttons, id, lookupStatus](QJsonObject result, QString error) {
                        if (!safeDialog || safeDialog->isHidden()) return;
                        lookupStatus->hide();
                        if (error.contains("HTTP 404") && id == videoId(id)) {
                            state->links << "https://www.youtube.com/watch?v=" + id;
                            if (next) (*next)();
                            return;
                        }
                        const auto channels = result["channels"].toArray();
                        if (error.isEmpty() && !channels.isEmpty()) {
                            QStringList choices;
                            for (const auto &channel : channels) {
                                const auto row = channel.toObject();
                                QStringList parts{str(row["title"])};
                                if (!str(row["handle"]).isEmpty()) parts << str(row["handle"]);
                                else parts << str(row["id"]);
                                if (!str(row["details"]).isEmpty()) parts << str(row["details"]);
                                choices << parts.join(" · ");
                            }
                            bool ok = false;
                            const auto selected = QInputDialog::getItem(safeDialog, cfg.T("Select a YouTube channel"),
                                cfg.T("Matching channels"), choices, 0, false, &ok);
                            if (!ok) { buttons->button(QDialogButtonBox::Open)->setEnabled(true); return; }
                            state->pending.prepend(QJsonObject{{"platform", "youtube"},
                                {"channel_id", channels[choices.indexOf(selected)].toObject()["id"]}});
                            if (next) (*next)();
                            return;
                        }
                        const auto streams = result["streams"].toArray();
                        if (!error.isEmpty() || streams.isEmpty()) {
                            buttons->button(QDialogButtonBox::Open)->setEnabled(true);
                            if (!error.isEmpty()) QMessageBox::warning(safeDialog, cfg.T("Add chats"), cfg.T(error));
                            else QMessageBox::information(safeDialog, cfg.T("Add chats"),
                                cfg.T("This channel has no current live broadcasts."));
                            return;
                        }
                        int index = 0;
                        if (streams.size() > 1) {
                            const auto titles = liveStreamChoices(streams);
                            bool ok = false;
                            const auto selected = QInputDialog::getItem(safeDialog, cfg.T("Select a live broadcast"),
                                cfg.T("Current broadcasts"), titles, 0, false, &ok);
                            if (!ok) { buttons->button(QDialogButtonBox::Open)->setEnabled(true); return; }
                            index = titles.indexOf(selected);
                        }
                        state->links << "https://www.youtube.com/watch?v=" + str(streams[index].toObject()["id"]);
                        if (next) (*next)();
                    });
                    return;
                }
            }
            state->links.removeDuplicates();
            openChats(state->links, selectedMode, safe);
            safeDialog->accept();
        };
        (*step)();
    };
    connect(buttons, &QDialogButtonBox::accepted, dialog, open);
    connect(input, &QLineEdit::returnPressed, dialog, open);
    auto *shortcut = new QShortcut(QKeySequence("Ctrl+Return"), dialog);
    connect(shortcut, &QShortcut::activated, dialog, open);
    dialog->resize(520, saved->count() ? 190 + qMin(150, 6 + 27 * saved->count()) : 165);
    dialog->show();
    darkTitlebar(dialog);
    input->setFocus();
}

void MainWindow::openChats(const QStringList &links, QString mode, Pane *anchor) {
    QList<Source *> list;
    QStringList invalid;
    for (const auto &link : links) {
        if (link.startsWith("vod|")) {
            auto *source = hub.vodSource(QUrl(link.mid(4)));
            if (source) { if (vodBridge) vodBridge->allow(source->key); list << source; }
            else invalid << link;
            if (list.size() >= 32) break;
            continue;
        }
        QString kick = kickLogin(link), tw = twitchLogin(link), yt = videoId(link);
        if (!kick.isEmpty()) {
            list << hub.source("kick", kick);
        } else if (!tw.isEmpty()) {
            list << hub.source("twitch", tw);
        } else if (!yt.isEmpty()) {
            list << hub.source("youtube", yt);
        } else {
            invalid << link;
        }
        if (list.size() >= 32) {
            break;
        }
    }
    if (list.isEmpty()) {
        if (!invalid.isEmpty()) {
            QMessageBox::warning(this, cfg.T("Add chats"),
                                 cfg.T("Enter a YouTube live link, Twitch @channel or Kick link."));
        }
        return;
    }
    if (mode == "tabs") {
        for (auto *s : list) {
            addWorkspace({s});
        }
    } else if (mode == "mix") {
        if (anchor) {
            QList<Source *> all;
            for (auto s : anchor->sources) {
                if (s) {
                    all << s;
                }
            }
            for (auto s : list) {
                if (!all.contains(s)) {
                    all << s;
                }
            }
            anchor->setSources(all);
        } else {
            addWorkspace(list);
        }
    } else {
        Workspace *w = anchor ? qobject_cast<Workspace *>(anchor->parentWidget()) : current();
        if (anchor) {
            for (int i = 0; i < tabs->count(); ++i) {
                auto *candidate = qobject_cast<Workspace *>(tabs->widget(i));
                if (candidate && candidate->panes().contains(anchor)) {
                    w = candidate;
                }
            }
        }
        if (!w) {
            w = addWorkspace({list.takeFirst()});
            anchor = w->focusedPane;
        }
        for (auto *s : list) {
            anchor = w->add({s}, anchor, mode == "vertical" ? Qt::Vertical : Qt::Horizontal);
        }
    }
    refreshTabs();
    refreshQuota();
    sessionTimer.start();
}

void MainWindow::showSettings(QString page) {
    if (!settings) {
        settings = new SettingsWindow(this);
    }
    settings->showPage(page);
    settings->show();
    settings->raise();
    settings->activateWindow();
    darkTitlebar(settings);
}

void MainWindow::gearMenu() {
    QMenu m(this);
    m.addAction(cfg.T("Settings…"), this, [this] { showSettings(); });
    auto *top = m.addAction(cfg.T("Always on top"));
    top->setCheckable(true);
    top->setChecked(cfg.data["topmost"].toBool());
    connect(top, &QAction::toggled, this, [this](bool v) {
        cfg.data["topmost"] = v;
        cfg.save();
        applyTopmost();
    });
    m.addSeparator();
    m.addAction(cfg.T("Pause all YouTube chats"), this, [this] { allPause(true); });
    m.addAction(cfg.T("Resume all YouTube chats"), this, [this] { allPause(false); });
    m.addAction(cfg.T("Reconnect all chats"), this, [this] {
        for (auto s : hub.sources) {
            s->reconnect();
        }
    });
    m.addAction(cfg.T("Saved channels…"), this, [this] { showSettings("Saved channels"); });
    m.addAction(cfg.T("Check for updates…"), &updater, &Updater::show);
    m.exec(gear->mapToGlobal(QPoint(0, gear->height())));
}

void MainWindow::allPause(bool pause) {
    for (auto s : hub.sources) {
        if (s->platform == "youtube" && !s->isVod) {
            s->setPaused(pause);
        }
    }
    sessionTimer.start();
}

void MainWindow::paneMenu(Pane *p, QPoint point) {
    QMenu menu(this);
    populatePaneMenu(menu, p);
    menu.exec(point);
}

void MainWindow::populatePaneMenu(QMenu &menu, Pane *p) {
    if (!p) {
        return;
    }
    for (auto source : p->sources) {
        if (source) {
            auto *channel = menu.addMenu(QIcon(QPixmap::fromImage(channelIcon(source, &images, 20))),
                                         source->name());
            if (source->isVod) {
                channel->addAction(cfg.T("Open VOD in browser"), source, [source] { QDesktopServices::openUrl(source->vodUrl); });
                channel->addAction(cfg.T("Copy VOD link"), source, [source] { QGuiApplication::clipboard()->setText(source->vodUrl.toString()); });
                if (p->sources.size() > 1) channel->addAction(cfg.T("Remove from mixed feed"), p, [p, source] {
                    QList<Source *> keep; for (auto s : p->sources) if (s && s != source) keep << s;
                    p->setSources(keep);
                });
                continue;
            }
            auto *openStream = channel->addAction(cfg.T("Open stream in browser"), source, [source] {
                QDesktopServices::openUrl(QUrl(browserUrl(source->platform, source->id, "stream")));
            });
            openStream->setObjectName("open-stream-browser");
            auto *copyStream = channel->addAction(cfg.T("Copy stream link"), source, [source] {
                QGuiApplication::clipboard()->setText(browserUrl(source->platform, source->id, "stream"));
            });
            copyStream->setObjectName("copy-stream-link");
            for (const auto &pair : QList<QPair<QString, QString>>{{"player", "Open player in browser"},
                                                                   {"chat", "Open chat in browser"}}) {
                channel->addAction(cfg.T(pair.second), this, [source, pair] {
                    QDesktopServices::openUrl(QUrl(browserUrl(source->platform, source->id, pair.first)));
                });
            }
            channel->addSeparator();
            channel->addAction(cfg.T("Reconnect"), source, &Source::reconnect);
            if (source->platform == "youtube") {
                channel->addAction(cfg.T(source->paused ? "Resume chat" : "Pause chat"), source,
                                   [source] { source->setPaused(!source->paused); });
                auto *apiFallback = channel->addAction(cfg.T("Official API fallback (uses quota)"));
                apiFallback->setObjectName("youtube-api-fallback");
                apiFallback->setCheckable(true);
                apiFallback->setChecked(source->apiFallbackEnabled);
                connect(apiFallback, &QAction::triggered, this, [this, source, apiFallback](bool enabled) {
                    if (enabled) {
                        const auto answer = QMessageBox::question(
                            this, cfg.T("Official YouTube API fallback"),
                            cfg.T("If web chat is rate limited, use your signed-in YouTube account's "
                                  "official API. Each request costs 5 units; at 18.25 seconds, "
                                  "about 986 units per hour per chat, plus setup and other actions. "
                                  "Multiple chats use more. Enable this fallback?"));
                        if (answer != QMessageBox::Yes) {
                            apiFallback->setChecked(false);
                            return;
                        }
                    }
                    source->setApiFallback(enabled);
                });
                channel->addAction(cfg.T("Official API interval…"), this, [this, source] {
                    bool ok;
                    double seconds = QInputDialog::getDouble(
                        this, cfg.T("Official YouTube API interval"),
                        cfg.T("Seconds between API requests (5 units each; minimum 18.25 seconds)"),
                        source->apiInterval, 18.25, 600, 2, &ok);
                    if (!ok) return;
                    source->apiInterval = seconds;
                    auto all = cfg.data["chat_settings"].toObject();
                    auto settings = all[source->key].toObject();
                    settings["youtube_api_interval_seconds"] = seconds;
                    all[source->key] = settings;
                    cfg.data["chat_settings"] = all;
                    cfg.save();
                });
                channel->addAction(cfg.T("Request interval…"), this, [this, source] {
                    bool ok;
                    double interval = QInputDialog::getDouble(
                        this, cfg.T("Chat request interval"),
                        cfg.T("Seconds between requests (YouTube's minimum is respected)"), source->interval,
                        4, 600, 2, &ok);
                    if (ok) {
                        source->interval = interval;
                        auto all = cfg.data["chat_settings"].toObject();
                        auto settings = all[source->key].toObject();
                        settings["web_poll_interval_seconds"] = interval;
                        all[source->key] = settings;
                        cfg.data["chat_settings"] = all;
                        cfg.save();
                    }
                });
            }
            channel->addAction(cfg.T("Save channel"), this, [this, source] { saveChannel(source); });
            if (hub.canEditChannel(source)) {
                auto *edit = channel->addAction(
                    cfg.T(source->platform == "youtube" ? "Change title…" : "Change title / category…"),
                    this, [this, source] { editChannel(source); });
                edit->setObjectName("channel-edit");
            }
            if (p->sources.size() > 1) {
                channel->addAction(cfg.T("Remove from mixed feed"), p, [p, source] {
                    QList<Source *> keep;
                    for (auto s : p->sources) {
                        if (s && s != source) {
                            keep << s;
                        }
                    }
                    p->setSources(keep);
                });
            }
        }
    }
    auto *phone = menu.addAction(cfg.T("Open on phone…"), this, [this, p] { showPhone(p); });
    phone->setObjectName("open-phone");
    menu.addSeparator();
    menu.addAction(cfg.T("Search messages…") + "\tCtrl+F", p, &Pane::showSearch);
    auto *historyMenu = menu.addMenu(cfg.T("Filters and history"));
    auto *vod = historyMenu->addAction(cfg.T("Browser extension setup…"), this, &MainWindow::showVod);
    vod->setObjectName("open-vod-replay");
    auto *layoutMenu = menu.addMenu(cfg.T("Layout"));
    menu.addAction(cfg.T("Rename tab…"), this, [this] {
        if (auto *w = current()) {
            bool ok = false;
            auto name = QInputDialog::getText(this, cfg.T("Rename tab"), cfg.T("Name"), QLineEdit::Normal,
                                              w->title, &ok);
            if (ok) {
                w->title = name.trimmed();
                refreshTabs();
                sessionTimer.start();
            }
        }
    });
    auto *zoom = layoutMenu->addAction(cfg.T("Zoom / restore pane"), p, [this, p] {
        if (auto *w = current()) {
            w->toggleZoom(p);
        }
    });
    auto *balance = layoutMenu->addAction(cfg.T("Balance splits"), this, [this] {
        if (auto *w = current()) {
            for (auto *s : w->findChildren<QSplitter *>()) {
                QList<int> sizes;
                for (int i = 0; i < s->count(); ++i) {
                    sizes << 1;
                }
                s->setSizes(sizes);
            }
        }
    });
    const bool hasSplits = current() && current()->panes().size() > 1;
    zoom->setEnabled(hasSplits);
    balance->setEnabled(hasSplits);
    layoutMenu->addSeparator();
    layoutMenu->addAction(cfg.T("Add chats in a split…"), this, [this, p] { addChats("horizontal", p); });
    layoutMenu->addAction(cfg.T("Add channels to this mixed feed…"), this, [this, p] { addChats("mix", p); });
    layoutMenu->addSeparator();
    layoutMenu->addAction(cfg.T("Split horizontally"), this, [this, p] {
        if (auto *w = current()) {
            QList<Source *> s;
            for (auto source : p->sources) {
                if (source) {
                    s << source;
                }
            }
            w->add(s, p, Qt::Horizontal);
        }
    });
    layoutMenu->addAction(cfg.T("Split vertically"), this, [this, p] {
        if (auto *w = current()) {
            QList<Source *> s;
            for (auto source : p->sources) {
                if (source) {
                    s << source;
                }
            }
            w->add(s, p, Qt::Vertical);
        }
    });
    historyMenu->addAction(cfg.T("Chat filters…"), this, [this, p] { filters(p); });
    historyMenu->addAction(cfg.T("Message limit…"), this, [this, p] { setMessageCap(p); });
    historyMenu->addSeparator();
    historyMenu->addAction(cfg.T("Clear displayed messages"), p, [p] { p->view->setMessages({}); });
    menu.addSeparator();
    menu.addAction(cfg.T(hasSplits ? "Close pane" : "Close tab") + "\tCtrl+W", this, [this, p] {
        for (int i = 0; i < tabs->count(); ++i) {
            auto *w = qobject_cast<Workspace *>(tabs->widget(i));
            if (w && w->panes().contains(p)) {
                if (w->panes().size() == 1) {
                    closeTab(i);
                } else {
                    w->closePane(p);
                }
                break;
            }
        }
        QTimer::singleShot(0, this, &MainWindow::pruneSources);
    });
}

void MainWindow::startVodBridge(quint16 port) {
    if (vodBridge) return;
    vodBridge = new VodBridge(&hub, this, port);
    if (!vodBridge->server.isListening()) statusBar()->showMessage(cfg.T("VOD browser connection is unavailable."), 10000);
}

void MainWindow::showVod() {
    QMessageBox box(QMessageBox::Information, cfg.T("Browser extension setup"),
        cfg.T("Install the included browser extension. Open a live stream or VOD in the browser, then select its chat with the + button.\n\n"
              "Chrome/Edge: open Extensions, enable Developer mode, choose Load unpacked and select the extension folder.\n\n"
              "Firefox: download the signed XPI from the GitHub release. In about:addons, use the gear menu → Install Add-on From File.\n\n"
              "After updating the app, reload the unpacked Chrome/Edge extension."), QMessageBox::Close, this);
    auto *folder = box.addButton(cfg.T("Extension folder"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == folder) {
        const QDir app(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
        const auto path = app.filePath("browser-extension");
#else
        const auto path = app.filePath("../share/yt-chatterino/browser-extension");
#endif
        QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::cleanPath(path)));
    }
}

void MainWindow::showPhone(Pane *target) {
    if (!target) return;
    if (phoneServer && phoneServer->isActive() && phoneServer->pane != target) {
        QMessageBox::information(this, cfg.T("Open on phone"),
                                 cfg.T("Phone access is active in another pane. Stop it there first."));
        return;
    }
    if (phoneDialog && phoneTarget == target) {
        phoneDialog->show(); phoneDialog->raise(); phoneDialog->activateWindow(); return;
    }
    if (phoneDialog) phoneDialog->close();
    auto *dialog = new QDialog(this);
    phoneDialog = dialog;
    phoneTarget = target;
    dialog->setObjectName("phone-dialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(cfg.T("Open on phone"));
    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(label(cfg, "Choose the computer's local network, then start phone access.", true));
    auto *interfaces = new QComboBox(dialog);
    interfaces->setObjectName("phone-network");
    QSet<QString> seen;
    for (const auto &networkInterface : QNetworkInterface::allInterfaces()) {
        if (!networkInterface.flags().testFlag(QNetworkInterface::IsUp) ||
            networkInterface.flags().testFlag(QNetworkInterface::IsLoopBack)) continue;
        for (const auto &entry : networkInterface.addressEntries()) {
            const auto ip = entry.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol ||
                !(ip.isInSubnet(QHostAddress("10.0.0.0"), 8) ||
                  ip.isInSubnet(QHostAddress("172.16.0.0"), 12) ||
                  ip.isInSubnet(QHostAddress("192.168.0.0"), 16))) continue;
            if (seen.contains(ip.toString())) continue;
            seen.insert(ip.toString());
            interfaces->addItem(networkInterface.humanReadableName() + " · " + ip.toString(), ip.toString());
        }
    }
    layout->addWidget(interfaces);
    auto *hint = label(cfg, "Keep the app open. Use a trusted local network; this connection uses local HTTP.", true);
    hint->setWordWrap(true); layout->addWidget(hint);
    auto *qr = new QLabel(dialog);
    qr->setObjectName("phone-qr"); qr->setAlignment(Qt::AlignCenter); qr->setMinimumHeight(220);
    layout->addWidget(qr);
    auto *info = new QLabel(dialog);
    info->setObjectName("phone-address"); info->setTextFormat(Qt::PlainText);
    info->setTextInteractionFlags(Qt::TextSelectableByMouse); info->setAlignment(Qt::AlignCenter);
    info->setWordWrap(true); layout->addWidget(info);
    auto *row = new QHBoxLayout;
    auto *start = new QPushButton(cfg.T("Start phone access"), dialog);
    start->setObjectName("phone-start");
    auto *stop = new QPushButton(cfg.T("Stop"), dialog);
    stop->setObjectName("phone-stop"); stop->setEnabled(false);
    auto *close = new QPushButton(cfg.T("Close"), dialog);
    row->addWidget(start); row->addWidget(stop); row->addStretch(); row->addWidget(close);
    layout->addLayout(row);
    if (!phoneServer) {
        phoneServer = new PhoneServer(&hub, this);
        connect(phoneServer, &PhoneServer::stopped, this, [this] {
            if (phoneTarget && phoneTarget->phoneButton) phoneTarget->phoneButton->hide();
        });
    }
    QPointer<Pane> safe(target);
    const auto renderPairing = [this, interfaces, qr, info, start, stop] {
        const auto url = phoneServer->address();
        const auto code = phoneServer->pairingCode();
        const auto qrText = (url + "/#pair=" + code).toUtf8();
        const auto codeImage = qrcodegen::QrCode::encodeText(qrText.constData(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int side = codeImage.getSize(), border = 4, scale = qMax(1, 220 / (side + border * 2));
        QImage image((side + border * 2) * scale, (side + border * 2) * scale, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setPen(Qt::NoPen); painter.setBrush(Qt::black);
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x)
            if (codeImage.getModule(x, y)) painter.drawRect((x + border) * scale, (y + border) * scale, scale, scale);
        painter.end();
        qr->setPixmap(QPixmap::fromImage(image));
        info->setText(url + "\n" + cfg.T("Code: {code} · expires in 5 minutes", {{"code", code}}));
        interfaces->setEnabled(false); start->setEnabled(false); stop->setEnabled(true);
    };
    connect(start, &QPushButton::clicked, dialog, [this, safe, interfaces, dialog, renderPairing] {
        if (!safe || interfaces->currentIndex() < 0 ||
            !phoneServer->start(safe, QHostAddress(interfaces->currentData().toString()))) {
            QMessageBox::warning(dialog, cfg.T("Open on phone"),
                                 cfg.T("Could not start phone access on that network.")); return;
        }
        safe->phoneButton->show();
        renderPairing();
    });
    connect(stop, &QPushButton::clicked, dialog, [this] { if (phoneServer) phoneServer->stop(); });
    connect(phoneServer, &PhoneServer::stopped, dialog, [interfaces, qr, info, start, stop] {
        interfaces->setEnabled(true); start->setEnabled(true); stop->setEnabled(false); qr->clear(); info->clear();
    });
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        if (phoneDialog == dialog) phoneDialog = nullptr;
    });
    connect(close, &QPushButton::clicked, dialog, &QDialog::close);
    start->setEnabled(interfaces->count() > 0);
    if (phoneServer->isActive() && phoneServer->pane == target) {
        phoneServer->renewPairingCode();
        renderPairing();
    } else if (!interfaces->count()) info->setText(cfg.T("No private IPv4 network was found."));
    dialog->resize(450, 430); dialog->show(); darkTitlebar(dialog);
}

void MainWindow::tabMenu(int index, QPoint position) {
    if (index < 0) {
        QMenu m(this);
        m.addAction(cfg.T("Add chats…"), this, [this] { addChats(); });
        m.exec(position);
        return;
    }
    tabs->setCurrentIndex(index);
    if (currentPane()) {
        paneMenu(currentPane(), position);
    }
}

void MainWindow::messageMenu(Pane *pane, Message message, QPoint position) {
    QMenu m(this);
    populateMessageMenu(m, pane, message);
    m.exec(position);
}

void MainWindow::populateMessageMenu(QMenu &m, Pane *pane, Message message) {
    auto *copy = m.addAction(cfg.T("Copy"), pane->view, &ChatView::copySelection);
    copy->setEnabled(!pane->view->selectedText().isEmpty());
    const QString copiedBody = visibleMessageText(message, cfg);
    m.addAction(cfg.T("Copy message"), this,
                [message, copiedBody] { QApplication::clipboard()->setText(message.name + ": " + copiedBody); });
    m.addAction(cfg.T("Select all"), pane->view, [pane] {
        if (pane->view->messages.isEmpty()) {
            return;
        }
        pane->view->selectionStart = {0, 0};
        int last = pane->view->messages.size() - 1;
        pane->view->selectionEnd = {last, pane->view->document(last)->characterCount() - 1};
        pane->view->viewport()->update();
    });
    auto source = hub.sources.value(message.source);
    if (source && !message.system) {
        m.addSeparator();
        m.addAction(cfg.T("User information"), this, [=, this] { showUser(pane, source, message); });
        m.addAction(cfg.T("Highlight this user"), this,
                    [=, this] { highlightUser(message.login.isEmpty() ? message.name : message.login); });
        m.addAction(cfg.T("Mention"), pane, [pane, message] {
            pane->input->insert('@' + (message.login.isEmpty() ? message.name : message.login) + ' ');
            pane->input->setFocus();
        });
        if (source->platform == "twitch" || source->platform == "kick") {
            auto *reply =
                m.addAction(cfg.T("Reply to message"), pane, [pane, message] { pane->startReply(message); });
            reply->setEnabled(!message.deleted && !message.id.isEmpty());
            if (!message.reply.isEmpty()) {
                m.addAction(cfg.T("Open reply thread"), this, [=, this] { showThread(source, message); });
            }
        }
        if (hub.hasModerationRole(source)) {
            auto *moderation = m.addMenu(cfg.T("Moderation"));
            moderation->setObjectName("moderation-menu");
            for (const auto &action : QList<QPair<QString, QString>>{{"delete", "Delete this message"},
                                                                     {"timeout60", "Timeout · 1 minute"},
                                                                     {"timeout600", "Timeout · 10 minutes"},
                                                                     {"timeout3600", "Timeout · 1 hour"},
                                                                     {"ban", "Ban user…"},
                                                                     {"unban", "Unban user"}}) {
                if (source->platform == "kick" && !accounts.kickAllows({action.first == "delete"
                        ? "moderation:chat_message:manage" : "moderation:ban"})) continue;
                if (action.first == "unban" && source->platform != "twitch" && source->platform != "kick") {
                    continue;
                }
                auto *a = moderation->addAction(cfg.T(action.second), this, [=, this] {
                    if (action.first == "ban" &&
                        QMessageBox::question(this, cfg.T("Ban user"),
                                              cfg.T("Ban {name} from {channel}?",
                                                    {{"name", message.name}, {"channel", source->name()}})) !=
                            QMessageBox::Yes) {
                        return;
                    }
                    QString operation = action.first;
                    int duration = 0;
                    if (operation.startsWith("timeout")) {
                        duration = operation.mid(7).toInt();
                        operation = "timeout";
                    }
                    QPointer<MainWindow> safe(this);
                    hub.moderate(source, message, operation, duration, [safe](QJsonObject, QString error) {
                        if (safe && !error.isEmpty()) {
                            QMessageBox::warning(safe, safe->cfg.T("Moderation"), safe->cfg.T(error));
                        }
                    });
                });
                if (action.first == "delete" && source->platform == "twitch" && message.uid == source->room()) {
                    a->setEnabled(false);
                }
            }
        }
    }
}

void MainWindow::highlightUser(QString name) {
    auto settings = cfg.feature("highlight"), users = settings["users"].toObject();
    QString key = stripAt(name);
    if (users.contains(key)) {
        users.remove(key);
    } else {
        users[key] = "#227400";
    }
    settings["users"] = users;
    cfg.setFeature("highlight", settings);
}

void MainWindow::setMessageCap(Pane *pane) {
    bool ok;
    int cap = QInputDialog::getInt(this, cfg.T("Message limit"),
                                   cfg.T("Retained messages in this pane (100–20000):"), cfg.cap(pane->key()),
                                   100, 20000, 100, &ok);
    if (!ok) {
        return;
    }
    auto limits = cfg.data["chat_message_limits"].toObject();
    limits[pane->key()] = cap;
    cfg.data["chat_message_limits"] = limits;
    cfg.save();
    emit cfg.appearanceChanged();
}

void MainWindow::filters(Pane *p) {
    auto *d = new QDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(cfg.T("Chat filters"));
    auto *f = new QFormLayout(d);
    QMap<QString, QPlainTextEdit *> fields;
    for (const auto &pair :
         QList<QPair<QString, QString>>{{"hidden_users", "Hidden users"},
                                        {"include_terms", "Only messages containing"},
                                        {"exclude_terms", "Exclude messages containing"}}) {
        auto *edit = new QPlainTextEdit(d);
        QStringList list;
        for (const auto &v : p->filter[pair.first].toArray()) {
            list << v.toString();
        }
        edit->setPlainText(list.join('\n'));
        edit->setMaximumHeight(90);
        fields[pair.first] = edit;
        f->addRow(cfg.T(pair.second), edit);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, d);
    f->addRow(buttons);
    QPointer<Pane> pane(p);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, d, [=, this] {
        if (!pane) {
            return;
        }
        auto filter = pane->filter;
        for (auto i = fields.begin(); i != fields.end(); ++i) {
            filter[i.key()] = arrayOf(words(i.value()->toPlainText()));
        }
        auto all = cfg.data["chat_filters"].toObject();
        all[pane->key()] = filter;
        cfg.data["chat_filters"] = all;
        cfg.save();
        emit cfg.filtersChanged(pane->key());
    });
    connect(buttons, &QDialogButtonBox::rejected, d, &QDialog::close);
    d->resize(460, 400);
    d->show();
    darkTitlebar(d);
}

void MainWindow::saveChannel(Source *s) {
    QString id = s->platform == "youtube" ? s->room() : s->id;
    if (id.isEmpty()) {
        QMessageBox::information(this, cfg.T("Saved channels"),
                                 cfg.T("Wait for the channel details to load."));
        return;
    }
    auto saved = cfg.data["saved_channels"].toArray();
    for (const auto &v : saved) {
        auto item = v.toObject();
        if (str(item["platform"]) == s->platform && str(item["channel_id"]) == id) {
            return;
        }
    }
    saved.append(QJsonObject{{"platform", s->platform}, {"channel_id", id}, {"title", s->name()}});
    cfg.data["saved_channels"] = saved;
    cfg.save();
    statusBar()->showMessage(cfg.T("Channel saved."), 5000);
}

void MainWindow::openSaved(QJsonObject item) {
    if (str(item["platform"]) == "twitch" || str(item["platform"]) == "kick") {
        addWorkspace({hub.source(str(item["platform"]), str(item["channel_id"]))});
        return;
    }
    statusBar()->showMessage(cfg.T("Looking for current live broadcasts…"));
    QPointer<MainWindow> safe(this);
    hub.savedStreams(item, [safe](QJsonObject d, QString error) {
        if (!safe) {
            return;
        }
        safe->statusBar()->clearMessage();
        if (!error.isEmpty()) {
            QMessageBox::warning(safe, safe->cfg.T("Saved channels"), safe->cfg.T(error));
            return;
        }
        auto streams = d["streams"].toArray();
        if (streams.isEmpty()) {
            QMessageBox::information(safe, safe->cfg.T("Saved channels"),
                                     safe->cfg.T("This channel has no current live broadcasts."));
            return;
        }
        int index = 0;
        if (streams.size() > 1) {
            const auto titles = liveStreamChoices(streams);
            bool ok;
            QString selected =
                QInputDialog::getItem(safe, safe->cfg.T("Select a live broadcast"),
                                      safe->cfg.T("Current broadcasts"), titles, 0, false, &ok);
            if (!ok) {
                return;
            }
            index = titles.indexOf(selected);
        }
        safe->addWorkspace({safe->hub.source("youtube", str(streams[index].toObject()["id"]))});
    });
}

void MainWindow::editChannel(Source *source) {
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(cfg.T(source->platform == "youtube" ? "Change title" : "Change title / category"));
    auto *layout = new QFormLayout(dialog);
    auto *title = new QLineEdit(str(source->header["title"]), dialog);
    layout->addRow(cfg.T("Title"), title);
    if (source->platform == "kick" && !source->header["live"].toBool()) {
        auto *offlineHint = label(cfg, "Kick does not show the current title while offline. Leave this blank to keep it.", true);
        layout->addRow(offlineHint);
    }
    auto *category = new QLineEdit(str(source->header["category"]), dialog);
    auto *categories = new QComboBox(dialog);
    categories->setVisible(source->platform != "youtube");
    category->setVisible(source->platform != "youtube");
    if (source->platform != "youtube") {
        layout->addRow(cfg.T("Category"), category);
        layout->addRow(categories);
        auto *hint = label(cfg, "Type a category, then choose a result before saving.", true);
        layout->addRow(hint);
        auto *timer = new QTimer(dialog);
        timer->setSingleShot(true); timer->setInterval(450);
        auto lookup = [this, category, categories, source = QPointer<Source>(source)] {
            const QString queryText = category->text().trimmed();
            if (queryText.isEmpty()) { categories->clear(); return; }
            QPointer<QComboBox> safe(categories);
            QPointer<QLineEdit> safeInput(category);
            auto received = [safe, safeInput, queryText](QJsonObject d, QString e) {
                if (!safe || !safeInput || !e.isEmpty() || safeInput->text().trimmed() != queryText) return;
                safe->clear();
                for (const auto &value : d["data"].toArray()) {
                    const auto item = value.toObject();
                    const auto name = str(item["name"]), id = str(item["id"]);
                    if (!name.isEmpty() && !id.isEmpty()) safe->addItem(name, id);
                }
            };
            if (source && source->platform == "kick") {
                const auto query = Network::query("https://api.kick.com/public/v1/categories", {{"q", queryText}}).query(QUrl::FullyEncoded);
                accounts.kickApi("categories?" + query, received);
            } else {
                accounts.helix("search/categories", received, "GET", {{"query", queryText}, {"first", "20"}});
            }
        };
        connect(category, &QLineEdit::textEdited, dialog, [timer, categories] { categories->clear(); timer->start(); });
        connect(timer, &QTimer::timeout, dialog, lookup);
        connect(category, &QLineEdit::editingFinished, dialog, [timer, lookup] { if (timer->isActive()) { timer->stop(); lookup(); } });
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dialog);
    layout->addRow(buttons);
    QPointer<Source> safeSource(source);
    QPointer<QDialog> safe(dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, dialog, [=, this] {
        if (!safeSource) {
            return;
        }
        if (safeSource->platform != "youtube" &&
            category->text().trimmed().compare(str(safeSource->header["category"]), Qt::CaseInsensitive) != 0 &&
            categories->currentData().toString().isEmpty()) {
            QMessageBox::warning(safe, cfg.T("Edit channel"), cfg.T("Search and select a category before saving."));
            return;
        }
        buttons->setEnabled(false);
        hub.editChannel(safeSource, title->text(), categories->currentData().toString(),
                        [=, this](QJsonObject, QString e) {
                            if (!safe) {
                                return;
                            }
                            buttons->setEnabled(true);
                            if (!e.isEmpty()) {
                                QMessageBox::warning(safe, cfg.T("Edit channel"), cfg.T(e));
                                return;
                            }
                            if (safeSource) {
                                safeSource->refreshHeader();
                            }
                            safe->accept();
                        });
    });
    dialog->resize(540, 190);
    dialog->show();
    darkTitlebar(dialog);
}

void MainWindow::quickSwitch() {
    QStringList titles;
    for (int i = 0; i < tabs->count(); ++i) {
        titles << tabs->tabText(i);
    }
    if (titles.isEmpty()) {
        return;
    }
    bool ok;
    QString picked = QInputDialog::getItem(this, cfg.T("Switch chat"), cfg.T("Chat"), titles,
                                           tabs->currentIndex(), true, &ok);
    if (ok && titles.contains(picked)) {
        tabs->setCurrentIndex(titles.indexOf(picked));
    }
}

void MainWindow::globalSearch() {
    auto *d = new QDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setObjectName("global-search");
    d->setWindowTitle(cfg.T("Search all chats"));
    auto *l = new QVBoxLayout(d);
    auto *input = new QLineEdit(d);
    input->setObjectName("global-search-input");
    input->setClearButtonEnabled(true);
    input->setPlaceholderText(cfg.T("Search messages…"));
    auto *results = new QListWidget(d);
    results->setObjectName("global-search-results");
    results->setUniformItemSizes(true);
    auto *status = new QLabel(d);
    status->setObjectName("global-search-status");
    status->setStyleSheet("color:#8b8b94;");
    l->addWidget(input);
    l->addWidget(status);
    l->addWidget(results);
    auto *searchTimer = new QTimer(d);
    searchTimer->setSingleShot(true);
    searchTimer->setInterval(120);
    connect(&cfg, &Config::appearanceChanged, d, [input, results, status, searchTimer] {
        results->clear();
        status->clear();
        if (!input->text().isEmpty()) searchTimer->start();
    });
    connect(input, &QLineEdit::textChanged, d, [results, status, searchTimer](const QString &query) {
        results->clear();
        status->clear();
        if (query.isEmpty()) {
            searchTimer->stop();
        } else {
            searchTimer->start();
        }
    });
    connect(searchTimer, &QTimer::timeout, d, [this, input, results, status] {
        const QString query = input->text();
        int count = 0;
        for (int i = 0; i < tabs->count() && count < 2000; ++i) {
            auto *w = qobject_cast<Workspace *>(tabs->widget(i));
            if (!w) {
                continue;
            }
            for (auto *p : w->panes()) {
                for (const auto &m : p->view->messages) {
                    if (count >= 2000) {
                        break;
                    }
                    const QString body = visibleMessageText(m, cfg);
                    if ((m.name + ' ' + body).contains(query, Qt::CaseInsensitive)) {
                        auto *item = new QListWidgetItem(
                            tabs->tabText(i) + " · " + m.name + ": " + body.left(180), results);
                        item->setData(Qt::UserRole, w->uid);
                        item->setData(Qt::UserRole + 1, p->uid);
                        item->setData(Qt::UserRole + 2, m.seq);
                        ++count;
                    }
                }
            }
        }
        status->setText(count ? cfg.T(count == 2000 ? "Showing up to {count} results" : "Results: {count}",
                                      {{"count", QString::number(count)}})
                              : cfg.T("No matching messages."));
    });
    connect(results, &QListWidget::itemActivated, d, [this, d, status](QListWidgetItem *item) {
        for (int i = 0; i < tabs->count(); ++i) {
            auto *w = qobject_cast<Workspace *>(tabs->widget(i));
            if (!w || w->uid != item->data(Qt::UserRole).toString()) {
                continue;
            }
            for (auto *p : w->panes()) {
                if (p->uid == item->data(Qt::UserRole + 1).toString()) {
                    for (int row = 0; row < p->view->messages.size(); ++row) {
                        if (p->view->messages[row].seq == item->data(Qt::UserRole + 2).toLongLong()) {
                            tabs->setCurrentIndex(i);
                            w->focusedPane = p;
                            p->view->jumpRow(row);
                            refreshQuota();
                            d->accept();
                            return;
                        }
                    }
                }
            }
            break;
        }
        status->setText(cfg.T("This message is no longer available."));
    });
    d->resize(700, 480);
    d->show();
    darkTitlebar(d);
    input->setFocus();
}

void MainWindow::changeFont(int delta, bool reset) {
    auto a = cfg.appearance();
    int body = reset ? 12 : qBound(7, a["message_font_size"].toInt(12) + delta, 48),
        name = reset ? 12 : qBound(7, a["username_font_size"].toInt(12) + delta, 48);
    a["message_font_size"] = body;
    a["username_font_size"] = name;
    a["font_size"] = body;
    cfg.setAppearance(a);
}

void MainWindow::closeTab(int index) {
    if (index < 0) {
        return;
    }
    auto *w = qobject_cast<Workspace *>(tabs->widget(index));
    if (!w) {
        return;
    }
    closedTabs.append(w->serialize());
    while (closedTabs.size() > 10) {
        closedTabs.removeFirst();
    }
    tabs->removeTab(index);
    w->deleteLater();
    QTimer::singleShot(0, this, &MainWindow::pruneSources);
    sessionTimer.start();
    refreshTabs();
    refreshQuota();
}

void MainWindow::pruneSources() {
    QSet<QString> needed;
    for (int i = 0; i < tabs->count(); ++i) {
        if (auto *w = qobject_cast<Workspace *>(tabs->widget(i))) {
            for (auto *p : w->panes()) {
                for (auto s : p->sources) {
                    if (s) {
                        needed.insert(s->key);
                    }
                }
            }
        }
    }
    for (auto thread : threads) {
        if (thread && thread->source) {
            needed.insert(thread->source->key);
        }
    }
    for (auto card : userCards) {
        if (card) {
            needed.insert(card->property("source_key").toString());
        }
    }
    for (auto s : hub.sources) {
        if (!needed.contains(s->key)) {
            if (s->isVod && vodBridge && vodBridge->owners.contains(s->key)) {
                if (s->header["vod_shown"].toBool()) {
                    s->header["vod_shown"] = false;
                    if (s->vodReplay) s->vodReplay->disconnectPlayback();
                    s->history.clear(); s->seen.clear(); s->seenOrder.clear();
                }
                continue;
            }
            if (s->isVod && vodBridge) vodBridge->suppress(s->key);
            hub.remove(s);
        }
    }
}

void MainWindow::applyTopmost() {
    bool visible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, cfg.data["topmost"].toBool());
    if (visible) {
        show();
    }
    darkTitlebar(this);
}

void MainWindow::saveSession() {
    if (restoring) {
        return;
    }
    QJsonArray sources, workspaces;
    for (auto s : hub.sources) {
        if (!s->isVod || s->header["vod_shown"].toBool()) sources.append(s->spec());
    }
    for (int i = 0; i < tabs->count(); ++i) {
        if (auto *w = qobject_cast<Workspace *>(tabs->widget(i))) {
            workspaces.append(w->serialize());
        }
    }
    QRect normal = isMaximized() ? normalGeometry() : geometry();
    QString geo = QString("%1x%2%3%4%5%6")
                      .arg(normal.width())
                      .arg(normal.height())
                      .arg(normal.x() < 0 ? "-" : "+")
                      .arg(qAbs(normal.x()))
                      .arg(normal.y() < 0 ? "-" : "+")
                      .arg(qAbs(normal.y()));
    auto window = cfg.data["session"].toObject()["window"].toObject();
    window["qt_geometry"] = QString::fromLatin1(saveGeometry().toBase64());
    window["state"] = isMaximized() ? "zoomed" : "normal";
    window["geometry"] = geo;
    cfg.data["session"] =
        QJsonObject{{"version", 1},       {"sources", sources},
                    {"tabs", workspaces}, {"active", current() ? current()->uid : QString()},
                    {"geometry", geo},    {"window", window},
                    {"native_version", 2}};
    cfg.save();
}

void MainWindow::restoreSession() {
    restoring = true;
    auto session = cfg.data["session"].toObject();
    int count = 0;
    for (const auto &v : session["sources"].toArray()) {
        if (++count > 128) {
            break;
        }
        auto spec = v.toObject();
        QString p = str(spec["platform"]), id = str(spec["id"]);
        Source *source = nullptr;
        if (spec["vod"].toBool()) {
            const QUrl url(str(spec["url"]));
            auto *candidate = hub.vodSource(url);
            if (candidate && candidate->platform == p && candidate->id == id) {
                source = candidate;
                const auto name = str(spec["channel"]).left(200), title = str(spec["title"]).left(500);
                if (!name.isEmpty()) source->header["channel"] = name;
                if (!title.isEmpty()) source->header["title"] = title;
                source->header["vod_shown"] = true;
            } else if (candidate) hub.remove(candidate);
        } else if (p == "twitch" && twitchLogin('@' + id) == id) source = hub.source(p, id);
        else if (p == "kick" && kickLogin("kick:" + id) == id) source = hub.source(p, id);
        else if (p == "youtube" && videoId(id) == id) source = hub.source(p, id);
        if (source && !source->isVod) source->paused = spec["paused"].toBool();
    }
    count = 0;
    for (const auto &v : session["tabs"].toArray()) {
        if (++count > 48) {
            break;
        }
        auto *w = new Workspace(&hub, tabs);
        connect(w, &Workspace::paneAdded, this, &MainWindow::attachPane);
        connect(w, &Workspace::changed, this, [this] { sessionTimer.start(); });
        w->restore(v.toObject());
        if (w->panes().isEmpty()) {
            delete w;
            continue;
        }
        int index = tabs->addTab(w, w->title);
        if (w->uid == str(session["active"])) {
            tabs->setCurrentIndex(index);
        }
    }
    auto window = session["window"].toObject();
    bool restored = false;
    auto encoded = str(window["qt_geometry"]);
    if (!encoded.isEmpty()) {
        restored = restoreGeometry(QByteArray::fromBase64(encoded.toLatin1()));
    }
    if (!restored) {
        QString legacy = str(window["geometry"]);
        if (legacy.isEmpty()) {
            legacy = str(session["geometry"]);
        }
        auto match =
            QRegularExpression("^(\\d+)x(\\d+)(\\+-\\d+|[+-]\\d+)(\\+-\\d+|[+-]\\d+)$").match(legacy);
        if (match.hasMatch()) {
            QString x = match.captured(3), y = match.captured(4);
            x.replace("+-", "-");
            y.replace("+-", "-");
            QRect rect(x.toInt(), y.toInt(), qBound(340, match.captured(1).toInt(), 32000),
                       qBound(300, match.captured(2).toInt(), 32000));
            bool intersects = false;
            for (auto *screen : QGuiApplication::screens()) {
                if (screen->availableGeometry().intersects(rect)) {
                    intersects = true;
                }
            }
            if (!intersects) {
                rect.moveTopLeft(QGuiApplication::primaryScreen()->availableGeometry().topLeft() +
                                 QPoint(40, 40));
            }
            setGeometry(rect);
        }
        if (str(window["state"]) == "zoomed") {
            setWindowState(windowState() | Qt::WindowMaximized);
        }
    }
    restoring = false;
    refreshTabs();
    refreshQuota();
}

void MainWindow::closeEvent(QCloseEvent *e) {
    saveSession();
    hub.twitch.close();
    hub.liveTimer.stop();
    for (auto s : hub.sources) {
        s->stop();
    }
    e->accept();
}

void MainWindow::moveEvent(QMoveEvent *e) {
    QMainWindow::moveEvent(e);
    if (!restoring) {
        sessionTimer.start();
    }
}

void MainWindow::resizeEvent(QResizeEvent *e) {
    QMainWindow::resizeEvent(e);
    if (!restoring) {
        sessionTimer.start();
    }
}

bool MainWindow::eventFilter(QObject *o, QEvent *e) {
    if (o == tabs->tabBar()) {
        if (e->type() == QEvent::Wheel) {
            auto *w = static_cast<QWheelEvent *>(e);
            if (tabs->count()) {
                tabs->setCurrentIndex(
                    (tabs->currentIndex() + (w->angleDelta().y() < 0 ? 1 : tabs->count() - 1)) %
                    tabs->count());
            }
            return true;
        }
        if (e->type() == QEvent::MouseButtonRelease) {
            auto *m = static_cast<QMouseEvent *>(e);
            if (m->button() == Qt::MiddleButton) {
                int index = tabs->tabBar()->tabAt(m->position().toPoint());
                if (index < 0) {
                    addChats();
                } else {
                    closeTab(index);
                }
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(o, e);
}

void MainWindow::showThread(Source *source, Message selected) {
    if (!source || (source->platform != "twitch" && source->platform != "kick") || selected.reply.isEmpty()) {
        return;
    }
    QString root;
    collectThread(source->history, selected, &root);
    if (root.isEmpty()) {
        return;
    }
    QString key = source->key + ':' + root;
    if (threads.value(key)) {
        threads[key]->show();
        threads[key]->raise();
        return;
    }
    while (threads.size() >= 8) {
        auto it = threads.begin();
        if (it.value()) {
            it.value()->close();
        }
        threads.erase(it);
    }
    auto *thread = new ThreadWindow(this, source, selected, root);
    threads[key] = thread;
    connect(thread, &QObject::destroyed, this, [this, key] {
        threads.remove(key);
        pruneSources();
    });
    thread->show();
    darkTitlebar(thread);
}

ThreadWindow::ThreadWindow(MainWindow *m, Source *s, Message selected, QString id)
    : QDialog(m), main(m), source(s), root(std::move(id)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setProperty("source_key", s->key);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    pane = new Pane(&main->hub, {s}, this, true);
    pane->threadId = root;
    pane->view->threadMode = true;
    layout->addWidget(pane, 1);
    auto *notice = label(main->cfg, "Only messages retained in this app are available.", true);
    layout->addWidget(notice);
    connect(&main->cfg, &Config::languageChanged, this, [this, notice] {
        notice->setText(main->cfg.T("Only messages retained in this app are available."));
    });
    parentMessage.id = root;
    parentMessage.source = s->key;
    bool direct = str(selected.reply["id"]) == root;
    parentMessage.name = direct ? str(selected.reply["name"]) : str(selected.reply["thread_login"]);
    if (parentMessage.name.isEmpty()) {
        parentMessage.name = str(selected.reply["login"]);
    }
    parentMessage.login = parentMessage.name;
    parentMessage.uid = direct ? str(selected.reply["user_id"]) : QString();
    parentMessage.text = direct ? str(selected.reply["text"]) : main->cfg.T("[Earlier message unavailable]");
    parentMessage.seq = -qAbs(selected.seq) - 10000000;
    QString found;
    messages = collectThread(s->history, selected, &found);
    connect(s, &Source::message, this, &ThreadWindow::incoming);
    connect(s, &Source::changed, this, &ThreadWindow::rebuild);
    connect(s, &Source::redacted, this, [this](QJsonObject target) {
        if (str(target["message_id"]) == root ||
            (!str(target["user_id"]).isEmpty() && str(target["user_id"]) == parentMessage.uid) ||
            target["all"].toBool()) {
            parentMessage.deleted = true;
        }
        rebuild();
    });
    connect(&main->cfg, &Config::languageChanged, this, &ThreadWindow::rebuild);
    connect(&main->cfg, &Config::appearanceChanged, this, &ThreadWindow::rebuild);
    main->attachPane(pane);
    rebuild();
    resize(500, 420);
    pane->input->setFocus();
}

void ThreadWindow::incoming(Message m) {
    if (!source || m.system) {
        return;
    }
    QHash<QString, Message> index;
    for (const auto &old : messages) {
        index[old.id] = old;
    }
    if (m.id != root && threadRoot(m, index) != root) {
        return;
    }
    for (const auto &old : messages) {
        if (old.id == m.id) {
            return;
        }
    }
    messages << m;
    while (messages.size() > main->cfg.cap(source->key)) {
        messages.removeFirst();
    }
    if (m.id == root) {
        parentMessage = m;
        rebuild();
    } else if (pane->view->messages.size() >= main->cfg.cap(source->key)) {
        rebuild();
    } else {
        pane->view->append(m);
    }
}

void ThreadWindow::rebuild() {
    if (!source) {
        return;
    }
    QHash<QString, Message> index;
    for (const auto &m : source->history) {
        index[m.id] = m;
    }
    for (auto &m : messages) {
        if (index.contains(m.id)) {
            m = index[m.id];
        }
    }
    if (index.contains(root)) {
        parentMessage = index[root];
    }
    QVector<Message> rows{parentMessage};
    for (const auto &m : messages) {
        if (m.id != root) {
            rows << m;
        }
    }
    pane->view->messageCap = main->cfg.cap(source->key);
    if (rows.size() > pane->view->messageCap) {
        rows.remove(1, rows.size() - pane->view->messageCap);
    }
    pane->view->setMessages(rows);
    pane->reply = parentMessage;
    if (pane->reply.deleted) {
        for (auto i = messages.crbegin(); i != messages.crend(); ++i) {
            if (!i->deleted) {
                pane->reply = *i;
                break;
            }
        }
    }
    setWindowTitle(
        main->cfg.T("Reply thread · @{name} in {channel}",
                    {{"name", parentMessage.login.isEmpty() ? parentMessage.name : parentMessage.login},
                     {"channel", source->name()}}));
}

void MainWindow::showUser(Pane *pane, Source *source, Message message) {
    if (!source) {
        return;
    }
    QString key = source->key + ':' + (message.uid.isEmpty() ? message.login : message.uid);
    if (userCards.value(key)) {
        userCards[key]->show();
        userCards[key]->raise();
        return;
    }
    auto *d = new QDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setProperty("source_key", source->key);
    d->setWindowTitle(message.name + " · " + source->name());
    auto *layout = new QVBoxLayout(d);
    auto *header = new QHBoxLayout;
    auto *avatar = new QLabel(d);
    avatar->setFixedSize(80, 80);
    avatar->setAlignment(Qt::AlignCenter);
    header->addWidget(avatar);
    auto *info = new QVBoxLayout;
    auto *name = label(cfg, message.name);
    name->setFont(cfg.usernameFont());
    info->addWidget(name);
    auto *details = label(cfg, "Loading public profile…", true);
    info->addWidget(details);
    auto *followDetails = label(cfg, "", true);
    followDetails->setObjectName("user-card-follow-date");
    followDetails->setWordWrap(true);
    info->addWidget(followDetails);
    followDetails->hide();
    auto *followButton = button(cfg, "Enable follow dates", d, [] {});
    followButton->setObjectName("user-card-follow-permission");
    followButton->hide();
    info->addWidget(followButton, 0, Qt::AlignLeft);
    info->addWidget(label(cfg, "ID: " + message.uid, true));
    header->addLayout(info, 1);
    layout->addLayout(header);
    auto *bio = label(cfg, "");
    layout->addWidget(bio);
    QStringList badges;
    for (const auto &b : message.badges) {
        badges << b.label;
    }
    auto months = QRegularExpression("(?:^|,)subscriber/(\\d+)").match(message.badgeInfo);
    if (months.hasMatch()) {
        badges << cfg.T("Subscribed for {months} months", {{"months", months.captured(1)}});
    }
    layout->addWidget(label(cfg, badges.join(" · "), true));
    auto *actions = new QHBoxLayout;
    actions->addWidget(button(cfg, "Open profile", d, [source = QPointer<Source>(source), message] {
        if (!source) {
            return;
        }
        QString url = source->platform == "twitch" ? "https://www.twitch.tv/" + message.login
                      : source->platform == "kick" ? "https://kick.com/" + message.login
                                                   : "https://www.youtube.com/channel/" + message.uid;
        QDesktopServices::openUrl(QUrl(url));
    }));
    actions->addWidget(button(cfg, "Highlight", d, [this, message] {
        highlightUser(message.login.isEmpty() ? message.name : message.login);
    }));
    QPointer<Pane> safePane(pane);
    actions->addWidget(button(cfg, "Mention", d, [safePane, message] {
        if (safePane) {
            safePane->input->insert('@' + (message.login.isEmpty() ? message.name : message.login) + ' ');
            safePane->input->setFocus();
        }
    }));
    layout->addLayout(actions);
    auto *moderationRow = new QWidget(d);
    moderationRow->setObjectName("user-card-moderation");
    auto *mods = new QHBoxLayout(moderationRow);
    mods->setContentsMargins(0, 0, 0, 0);
    for (const auto &pair :
         QList<QPair<QString, int>>{{"Unban", -1}, {"1m", 60}, {"10m", 600}, {"1h", 3600}, {"Ban", 0}}) {
        if (source->platform == "kick" && !accounts.kickAllows({"moderation:ban"})) continue;
        if (pair.second == -1 && source->platform != "twitch" && source->platform != "kick") {
            continue;
        }
        auto *b = button(cfg, pair.first, d, [this, s = QPointer<Source>(source), message, pair] {
            if (!s) {
                return;
            }
            if (!pair.second &&
                QMessageBox::question(this, cfg.T("Ban user"),
                                      cfg.T("Ban {name}?", {{"name", message.name}})) != QMessageBox::Yes) {
                return;
            }
            hub.moderate(s, message,
                         pair.second < 0 ? "unban"
                         : pair.second   ? "timeout"
                                         : "ban",
                         qMax(0, pair.second), [this](QJsonObject, QString e) {
                             if (!e.isEmpty()) {
                                 QMessageBox::warning(this, cfg.T("Moderation"), cfg.T(e));
                             }
                         });
        });
        b->setEnabled(!roles(message).contains("owner"));
        mods->addWidget(b);
    }
    layout->addWidget(moderationRow);
    QPointer<Source> permissionSource(source);
    auto updateModeration = [this, permissionSource, moderationRow] {
        moderationRow->setVisible(permissionSource && hub.hasModerationRole(permissionSource) &&
            (permissionSource->platform != "kick" || accounts.kickAllows({"moderation:ban"})));
    };
    updateModeration();
    connect(source, &Source::permissionsChanged, d, updateModeration);
    connect(source, &Source::message, d, [updateModeration](Message) { updateModeration(); });
    connect(&accounts, &Accounts::changed, d, updateModeration);
    auto *count = label(cfg, "Recent messages retained in this channel", true);
    layout->addWidget(count);
    auto *view = new ChatView(&hub, d);
    view->messageCap = qMin(500, cfg.cap(source->key));
    layout->addWidget(view, 1);
    QPointer<Source> safeSource(source);
    auto refresh = [safeSource, message, view, count, this] {
        if (!safeSource) {
            return;
        }
        QVector<Message> list;
        for (const auto &m : safeSource->history) {
            if (!m.system && ((!message.uid.isEmpty() && message.uid == m.uid) ||
                              (message.uid.isEmpty() && (stripAt(m.login) == stripAt(message.login) ||
                                                         stripAt(m.name) == stripAt(message.login))))) {
                list << m;
            }
        }
        view->setMessages(list);
        count->setText(cfg.T("{count} retained messages · up to 500 shown",
                             {{"count", QString::number(qMin(500, int(list.size())))}}));
    };
    auto *refreshTimer = new QTimer(d);
    refreshTimer->setObjectName("user-card-history-refresh");
    refreshTimer->setSingleShot(true);
    refreshTimer->setInterval(150);
    connect(refreshTimer, &QTimer::timeout, d, refresh);
    auto scheduleRefresh = [refreshTimer] { if (!refreshTimer->isActive()) refreshTimer->start(); };
    connect(source, &Source::message, d, [scheduleRefresh](Message) { scheduleRefresh(); });
    connect(source, &Source::changed, d, scheduleRefresh);
    connect(&cfg, &Config::appearanceChanged, d, scheduleRefresh);
    refresh();
    QPointer<QDialog> safe(d);
    hub.profile(source, message,
                [this, safe, details, bio, avatar, followDetails, followButton, source = QPointer<Source>(source),
                 message](QJsonObject data, QString e) {
                    if (!safe) {
                        return;
                    }
                    QStringList lines;
                    if (source && source->platform == "twitch") {
                        if (data.contains("created_at")) {
                            lines << cfg.T("Created: {date}", {{"date", cfg.date(str(data["created_at"]))}});
                        }
                        if (data.contains("followers")) {
                            lines << cfg.T("Followers: {count}",
                                           {{"count", cfg.number(data["followers"].toInteger())}});
                        }
                        bio->setText(str(data["description"]).left(240));
                    } else {
                        auto sn = data["snippet"].toObject(), stats = data["statistics"].toObject();
                        if (sn.contains("publishedAt")) {
                            lines << cfg.T("Created: {date}", {{"date", cfg.date(str(sn["publishedAt"]))}});
                        }
                        if (!stats["hiddenSubscriberCount"].toBool() && stats.contains("subscriberCount")) {
                            lines << cfg.T(
                                "Subscribers: {count}",
                                {{"count", cfg.number(str(stats["subscriberCount"]).toLongLong())}});
                        }
                        bio->setText(str(sn["description"]).left(240));
                    }
                    details->setText(lines.isEmpty() ? cfg.T(e.isEmpty() ? "Public details unavailable" : e)
                                                     : lines.join('\n'));
                    const QString profileId = !str(data["id"]).isEmpty() ? str(data["id"]) : message.uid;
                    auto refreshFollow = [this, safe, source, followDetails, followButton, profileId] {
                        if (!safe || !source) return;
                        followButton->hide();
                        followDetails->hide();
                        hub.followSince(source, profileId, [this, safe, source, followDetails, followButton](QJsonObject result, QString) {
                            if (!safe || !source) return;
                            const QString status = str(result["status"]);
                            if (status == "following" && !str(result["date"]).isEmpty()) {
                                const bool youtube = source->platform == "youtube";
                                followDetails->setText(cfg.T(youtube ? "Subscribed since: {date}" : "Following since: {date}",
                                                             {{"date", cfg.date(str(result["date"]))}}));
                                followDetails->show();
                            } else if (status == "permission") {
                                followButton->setProperty("scope", result["scope"].toString());
                                followButton->show();
                            }
                        });
                    };
                    connect(followButton, &QPushButton::clicked, safe, [this, safe, followButton, followDetails, refreshFollow] {
                        const QString scope = followButton->property("scope").toString();
                        if (scope.isEmpty()) return;
                        followButton->setEnabled(false);
                        QSet<QString> requested{scope};
                        if (scope == "moderator:read:followers") requested.insert("user:read:moderated_channels");
                        accounts.requireTwitch(requested, [this, safe, followButton, followDetails, refreshFollow](QString, QString error) {
                            if (!safe) return;
                            followButton->setEnabled(true);
                            if (!error.isEmpty()) { followDetails->setText(cfg.T(error)); followDetails->show(); return; }
                            refreshFollow();
                        });
                    });
                    refreshFollow();
                    QString url = str(data["profile_image_url"]);
                    if (url.isEmpty()) {
                        url = message.avatar;
                    }
                    if (!url.isEmpty()) {
                        auto setAvatar = [this, safe, avatar, url] {
                            if (!safe) {
                                return;
                            }
                            auto image = images.get({url});
                            if (!image.isNull()) {
                                avatar->setPixmap(QPixmap::fromImage(
                                    image.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                            }
                        };
                        setAvatar();
                        connect(&images, &Images::ready, safe, [url, setAvatar](QString ready) {
                            if (ready == url) {
                                setAvatar();
                            }
                        });
                    }
                });
    userCards[key] = d;
    connect(d, &QObject::destroyed, this, [this, key] {
        userCards.remove(key);
        pruneSources();
    });
    d->resize(560, 620);
    d->show();
    darkTitlebar(d);
}
} // namespace yc
