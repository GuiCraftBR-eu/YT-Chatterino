#include "application.hpp"
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
    setWindowTitle("YT-Chatterino");
    setWindowIcon(QIcon(":/ytchatterino.ico"));
    resize(520, 760);
    setMinimumSize(340, 300);
    tabs = new QTabWidget(this);
    tabs->setTabsClosable(true);
    tabs->setMovable(true);
    tabs->setDocumentMode(true);
    setCentralWidget(tabs);
    tabs->tabBar()->installEventFilter(this);
    tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *strip = new QWidget(tabs);
    auto *sl = new QHBoxLayout(strip);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(1);
    addButton = new QToolButton(strip);
    addButton->setText("+");
    layoutButton = new QToolButton(strip);
    layoutButton->setText("▦");
    quotaLabel = new QLabel(strip);
    quotaLabel->setStyleSheet("color:#8b8b94;font-size:10px;");
    updateArrow = new QToolButton(strip);
    updateArrow->setText("⬇");
    updateArrow->setStyleSheet("color:#78c34b;");
    updateArrow->hide();
    gear = new QToolButton(strip);
    gear->setText("⚙");
    sl->addWidget(addButton);
    sl->addWidget(layoutButton);
    sl->addWidget(quotaLabel);
    sl->addWidget(updateArrow);
    sl->addWidget(gear);
    tabs->setCornerWidget(strip, Qt::TopRightCorner);
    connect(addButton, &QToolButton::clicked, this, [this] { addChats(); });
    connect(layoutButton, &QToolButton::clicked, this, [this] { addChats("horizontal", currentPane()); });
    connect(gear, &QToolButton::clicked, this, [this] { showSettings(); });
    gear->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(gear, &QWidget::customContextMenuRequested, this, [this] { gearMenu(); });
    connect(updateArrow, &QToolButton::clicked, &updater, &Updater::show);
    connect(&updater, &Updater::changed, this, [this] {
        updateArrow->setVisible(!updater.available.isEmpty());
        updateArrow->setToolTip(
            cfg.T("Update to {version}", {{"version", str(updater.available["version"])}}));
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
            if (s->platform == "twitch") {
                s->emoteLoaded.clear();
                s->badgesLoaded = 0;
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
    w->add(sources);
    tabs->setCurrentIndex(tabs->addTab(w, title));
    refreshTabs();
    return w;
}

void MainWindow::attachPane(Pane *p) {
    connect(p, &Pane::focused, this, [this](Pane *pane) {
        if (current()) {
            current()->focusedPane = pane;
        }
        refreshQuota();
    });
    connect(p, &Pane::menuRequested, this, &MainWindow::paneMenu);
    connect(p, &Pane::messageMenu, this, &MainWindow::messageMenu);
    connect(p, &Pane::userRequested, this, &MainWindow::showUser);
    connect(p, &Pane::threadRequested, this, &MainWindow::showThread);
}

void MainWindow::refreshTabs() {
    if (!tabs->count()) {
        auto *empty = new QWidget(tabs);
        empty->setObjectName("empty-placeholder");
        auto *layout = new QVBoxLayout(empty);
        auto *hint = new QLabel(cfg.T("Click + to add chats.\nUse ▦ to split or mix channels."), empty);
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
        tabs->setTabText(i, title.isEmpty() ? cfg.T("Chat") : title);
        tabs->setTabToolTip(i, title);
    }
}

void MainWindow::refreshQuota() {
    bool yt = false;
    if (auto *p = currentPane()) {
        for (auto s : p->sources) {
            if (s && s->platform == "youtube") {
                yt = true;
            }
        }
    }
    quotaLabel->setVisible(yt);
    quotaLabel->setText("YT ~" + cfg.number(quota.remaining()) + " u");
    quotaLabel->setToolTip(cfg.T("Approximate YouTube units left for sending and moderation"));
}

void MainWindow::refreshLanguage() {
    addButton->setToolTip(cfg.T("Add chats"));
    layoutButton->setToolTip(cfg.T("Split or mix chats"));
    gear->setToolTip(cfg.T("Settings"));
    refreshTabs();
    refreshQuota();
    for (auto *p : findChildren<Pane *>()) {
        p->refresh();
    }
    if (settings) {
        settings->rebuildLanguage();
    }
}

void MainWindow::addChats(QString mode, Pane *anchor) {
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(cfg.T("Add chats"));
    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(label(cfg, "YouTube links or Twitch @channels · one per line"));
    auto *input = new QPlainTextEdit(dialog);
    input->setMinimumHeight(90);
    layout->addWidget(input);
    auto *combo = new QComboBox(dialog);
    for (const auto &name : QStringList{"Separate tabs", "Side by side", "Stacked", "Mixed feed"}) {
        combo->addItem(cfg.T(name));
    }
    QStringList modes{"tabs", "horizontal", "vertical", "mix"};
    combo->setCurrentIndex(qMax(0, modes.indexOf(mode)));
    layout->addWidget(combo);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Open)->setText(cfg.T("Open"));
    buttons->button(QDialogButtonBox::Cancel)->setText(cfg.T("Cancel"));
    layout->addWidget(buttons);
    QPointer<Pane> safe(anchor);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    auto open = [this, input, combo, modes, dialog, safe] {
        openChats(input->toPlainText().split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts),
                  modes[combo->currentIndex()], safe);
        dialog->accept();
    };
    connect(buttons, &QDialogButtonBox::accepted, dialog, open);
    auto *shortcut = new QShortcut(QKeySequence("Ctrl+Return"), dialog);
    connect(shortcut, &QShortcut::activated, dialog, open);
    dialog->resize(480, 260);
    dialog->show();
    darkTitlebar(dialog);
    input->setFocus();
}

void MainWindow::openChats(const QStringList &links, QString mode, Pane *anchor) {
    QList<Source *> list;
    QStringList invalid;
    for (const auto &link : links) {
        QString tw = twitchLogin(link), yt = videoId(link);
        if (!tw.isEmpty()) {
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
                                 cfg.T("Enter a YouTube live link or Twitch @channel."));
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
        if (s->platform == "youtube") {
            s->setPaused(pause);
        }
    }
    sessionTimer.start();
}

void MainWindow::paneMenu(Pane *p, QPoint point) {
    QMenu menu(this);
    for (auto source : p->sources) {
        if (source) {
            auto *channel = menu.addMenu(source->name());
            for (const auto &pair : QList<QPair<QString, QString>>{{"stream", "Open stream in browser"},
                                                                   {"player", "Open player in browser"},
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
            channel->addAction(cfg.T("Edit title / category…"), this,
                               [this, source] { editChannel(source); });
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
    menu.addSeparator();
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
    menu.addAction(cfg.T("Zoom / restore pane"), p, [this, p] {
        if (auto *w = current()) {
            w->toggleZoom(p);
        }
    });
    menu.addAction(cfg.T("Balance splits"), this, [this] {
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
    menu.addAction(cfg.T("Add chats in a split…"), this, [this, p] { addChats("horizontal", p); });
    menu.addAction(cfg.T("Add channels to this mixed feed…"), this, [this, p] { addChats("mix", p); });
    menu.addAction(cfg.T("Split horizontally"), this, [this, p] {
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
    menu.addAction(cfg.T("Split vertically"), this, [this, p] {
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
    menu.addAction(cfg.T("Chat filters…"), this, [this, p] { filters(p); });
    menu.addAction(cfg.T("Search"), p, &Pane::showSearch);
    menu.addAction(cfg.T("Message limit…"), this, [this, p] { setMessageCap(p); });
    menu.addAction(cfg.T("Clear displayed messages"), p, [p] { p->view->setMessages({}); });
    menu.addSeparator();
    menu.addAction(cfg.T("Close pane"), this, [this, p] {
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
    menu.exec(point);
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
    auto *copy = m.addAction(cfg.T("Copy"), pane->view, &ChatView::copySelection);
    copy->setEnabled(!pane->view->selectedText().isEmpty());
    m.addAction(cfg.T("Copy message"), this,
                [message] { QApplication::clipboard()->setText(message.name + ": " + message.text); });
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
        if (source->platform == "twitch") {
            auto *reply =
                m.addAction(cfg.T("Reply to message"), pane, [pane, message] { pane->startReply(message); });
            reply->setEnabled(!message.deleted && !message.id.isEmpty());
            if (!message.reply.isEmpty()) {
                m.addAction(cfg.T("Open reply thread"), this, [=, this] { showThread(source, message); });
            }
        }
        auto *moderation = m.addMenu(cfg.T("Moderation"));
        for (const auto &action : QList<QPair<QString, QString>>{{"delete", "Delete this message"},
                                                                 {"timeout60", "Timeout · 1 minute"},
                                                                 {"timeout600", "Timeout · 10 minutes"},
                                                                 {"timeout3600", "Timeout · 1 hour"},
                                                                 {"ban", "Ban user…"},
                                                                 {"unban", "Unban user"}}) {
            if (action.first == "unban" && source->platform != "twitch") {
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
    m.exec(position);
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
        pane->filter = filter;
        pane->reload();
    });
    connect(buttons, &QDialogButtonBox::rejected, d, &QDialog::close);
    d->resize(460, 400);
    d->show();
    darkTitlebar(d);
}

void MainWindow::saveChannel(Source *s) {
    QString id = s->platform == "twitch" ? s->id : s->room();
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
    if (str(item["platform"]) == "twitch") {
        addWorkspace({hub.source("twitch", str(item["channel_id"]))});
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
            QStringList titles;
            for (const auto &s : streams) {
                titles << str(s.toObject()["title"]);
            }
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
    dialog->setWindowTitle(cfg.T("Edit title / category"));
    auto *layout = new QFormLayout(dialog);
    auto *title = new QLineEdit(str(source->header["title"]), dialog);
    layout->addRow(cfg.T("Title"), title);
    auto *category = new QLineEdit(str(source->header["category"]), dialog);
    auto *categories = new QComboBox(dialog);
    categories->setVisible(source->platform == "twitch");
    category->setVisible(source->platform == "twitch");
    if (source->platform == "twitch") {
        layout->addRow(cfg.T("Category"), category);
        layout->addRow(categories);
        connect(category, &QLineEdit::editingFinished, dialog, [this, category, categories] {
            if (category->text().trimmed().isEmpty()) {
                return;
            }
            QPointer<QComboBox> safe(categories);
            accounts.helix("search/categories",
                           [safe](QJsonObject d, QString e) {
                               if (!safe || !e.isEmpty()) {
                                   return;
                               }
                               safe->clear();
                               for (const auto &v : d["data"].toArray()) {
                                   auto o = v.toObject();
                                   safe->addItem(str(o["name"]), str(o["id"]));
                               }
                           },
                           "GET", {{"query", category->text()}, {"first", "20"}});
        });
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
    d->setWindowTitle(cfg.T("Search all chats"));
    auto *l = new QVBoxLayout(d);
    auto *input = new QLineEdit(d);
    input->setPlaceholderText(cfg.T("Search messages…"));
    auto *results = new QListWidget(d);
    l->addWidget(input);
    l->addWidget(results);
    connect(input, &QLineEdit::textChanged, d, [this, results](QString query) {
        results->clear();
        if (query.isEmpty()) {
            return;
        }
        int count = 0;
        for (int i = 0; i < tabs->count(); ++i) {
            auto *w = qobject_cast<Workspace *>(tabs->widget(i));
            if (!w) {
                continue;
            }
            for (auto *p : w->panes()) {
                for (const auto &m : p->view->messages) {
                    if ((m.name + ' ' + m.text).contains(query, Qt::CaseInsensitive)) {
                        auto *item = new QListWidgetItem(
                            tabs->tabText(i) + " · " + m.name + ": " + m.text.left(180), results);
                        item->setData(Qt::UserRole, i);
                        item->setData(Qt::UserRole + 1, p->uid);
                        item->setData(Qt::UserRole + 2, m.seq);
                        if (++count >= 2000) {
                            return;
                        }
                    }
                }
            }
        }
    });
    connect(results, &QListWidget::itemActivated, d, [this, d](QListWidgetItem *item) {
        int i = item->data(Qt::UserRole).toInt();
        tabs->setCurrentIndex(i);
        if (auto *w = current()) {
            for (auto *p : w->panes()) {
                if (p->uid == item->data(Qt::UserRole + 1).toString()) {
                    w->focusedPane = p;
                    for (int row = 0; row < p->view->messages.size(); ++row) {
                        if (p->view->messages[row].seq == item->data(Qt::UserRole + 2).toLongLong()) {
                            p->view->jumpRow(row);
                        }
                    }
                }
            }
        }
        d->accept();
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
        sources.append(s->spec());
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
        if (p == "twitch" && twitchLogin('@' + id) == id) {
            hub.source(p, id);
        } else if (p == "youtube" && videoId(id) == id) {
            hub.source(p, id);
        }
        auto source = hub.sources.value(p == "twitch" ? "twitch:" + id : id);
        if (source) {
            source->paused = spec["paused"].toBool();
        }
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
    if (!source || source->platform != "twitch" || selected.reply.isEmpty()) {
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
            parentMessage.text = main->cfg.T("[message deleted]");
            parentMessage.emotes.clear();
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
    header->addWidget(avatar);
    auto *info = new QVBoxLayout;
    auto *name = label(cfg, message.name);
    name->setFont(cfg.usernameFont());
    info->addWidget(name);
    auto *details = label(cfg, "Loading public profile…", true);
    info->addWidget(details);
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
    auto *mods = new QHBoxLayout;
    for (const auto &pair :
         QList<QPair<QString, int>>{{"Unban", -1}, {"1m", 60}, {"10m", 600}, {"1h", 3600}, {"Ban", 0}}) {
        if (pair.second == -1 && source->platform != "twitch") {
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
    layout->addLayout(mods);
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
                              (message.uid.isEmpty() && stripAt(m.login) == stripAt(message.login)))) {
                list << m;
            }
        }
        view->setMessages(list);
        count->setText(cfg.T("{count} retained messages · up to 500 shown",
                             {{"count", QString::number(qMin(500, int(list.size())))}}));
    };
    connect(source, &Source::message, d, [refresh](Message) { refresh(); });
    connect(source, &Source::changed, d, refresh);
    connect(&cfg, &Config::appearanceChanged, d, refresh);
    refresh();
    QPointer<QDialog> safe(d);
    hub.profile(source, message,
                [this, safe, details, bio, avatar, source = QPointer<Source>(source),
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
