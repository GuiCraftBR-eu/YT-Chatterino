#pragma once
#include "updater.hpp"
#include "view.hpp"

namespace yc {
class SettingsWindow;
class ThreadWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    MainWindow(QString profile = {}, bool testMode = false);
    ~MainWindow() override;
    Config cfg;
    Network net;
    Quota quota;
    Accounts accounts;
    Images images;
    Hub hub;
    Updater updater;
    QTabWidget *tabs;
    QLabel *quotaLabel;
    QToolButton *updateArrow, *gear, *addButton, *layoutButton;
    QPointer<SettingsWindow> settings;
    QTranslator *standardTranslator = nullptr;
    QHash<QString, QPointer<QDialog>> userCards;
    QHash<QString, QPointer<ThreadWindow>> threads;
    QTimer sessionTimer;
    bool restoring = false, testMode = false;
    QJsonArray closedTabs;
    Workspace *current() const;
    Pane *currentPane() const;
    Workspace *addWorkspace(QList<Source *> sources, QString title = {});
    void attachPane(Pane *pane);
    void pruneSources();
    void addChats(QString mode = "tabs", Pane *anchor = nullptr);
    void openChats(const QStringList &links, QString mode = "tabs", Pane *anchor = nullptr);
    void showSettings(QString page = "Accounts");
    void gearMenu();
    void paneMenu(Pane *pane, QPoint position);
    void populatePaneMenu(QMenu &menu, Pane *pane);
    void tabMenu(int index, QPoint position);
    void messageMenu(Pane *pane, Message message, QPoint position);
    void showUser(Pane *pane, Source *source, Message message);
    void showThread(Source *source, Message message);
    void editChannel(Source *source);
    void filters(Pane *pane);
    void saveChannel(Source *source);
    void openSaved(QJsonObject channel);
    void highlightUser(QString name);
    void setMessageCap(Pane *pane);
    void allPause(bool pause);
    void quickSwitch();
    void globalSearch();
    void changeFont(int delta, bool reset = false);
    void closeTab(int index);
    void refreshTabs();
    void refreshQuota();
    void refreshLanguage();
    void saveSession();
    void restoreSession();
    void applyTopmost();

  protected:
    void closeEvent(QCloseEvent *) override;
    void moveEvent(QMoveEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
};

class ThreadWindow : public QDialog {
    Q_OBJECT
  public:
    ThreadWindow(MainWindow *main, Source *source, Message selected, QString root);
    MainWindow *main;
    QPointer<Source> source;
    Pane *pane;
    QString root;
    Message parentMessage;
    QVector<Message> messages;
    void incoming(Message message);
    void rebuild();
};

class SettingsWindow : public QDialog {
    Q_OBJECT
  public:
    explicit SettingsWindow(MainWindow *main);
    MainWindow *main;
    QLineEdit *search;
    QLabel *heading;
    QTreeWidget *navigation;
    QListWidget *results;
    QStackedWidget *pages;
    QHash<QString, QWidget *> built;
    QString currentPage;
    QMap<QString, QStringList> searchItems;
    void showPage(QString page);
    QWidget *buildPage(QString page);
    void searchSettings();
    void rebuildLanguage();
    QWidget *accountsPage();
    QWidget *appearancePage();
    QWidget *messagesPage();
    QWidget *highlightsPage();
    QWidget *notificationsPage();
    QWidget *savedPage();
    QWidget *quotaPage();
    QWidget *updatesPage();
    QWidget *shortcutsPage();
    QWidget *tutorialPage();
    void showYoutubeGuide();
};
} // namespace yc
