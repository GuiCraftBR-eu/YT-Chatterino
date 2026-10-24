#pragma once
#include "chat.hpp"
#include <memory>

namespace yc {
class ChatView : public QAbstractScrollArea {
    Q_OBJECT
  public:
    ChatView(Hub *hub, QWidget *parent = nullptr);
    Hub *hub;
    QVector<Message> messages;
    QVector<int> heights, offsets;
    QHash<qint64, QSharedPointer<QTextDocument>> documents;
    QHash<qint64, qint64> documentUse;
    qint64 documentClock = 0;

    struct Details {
        int nameStart = 0, nameEnd = 0, replyEnd = 0;
        QHash<int, QString> imageCodes;
        QSet<QString> urls;
        QHash<QString, QSize> imageSizes;
    };

    QHash<qint64, Details> details;
    bool follow = true, mixed = false, threadMode = false;
    int messageCap = 2000;
    QString query;
    QPushButton *latest;
    QTimer resizeTimer;
    QPoint pressPoint;
    QPair<int, int> selectionStart{-1, 0}, selectionEnd{-1, 0};
    bool dragging = false;
    qint64 synthetic = -1;
    int layoutWidth = 0;
    QStringList ownNames;
    void setMessages(QVector<Message> list);
    void append(Message message);
    void trim();
    void invalidate();
    void rebuildOffsets();
    void reflow();
    int estimate(const Message &message) const;
    int rowAt(int y) const;
    QPair<int, int> positionAt(QPoint point);
    QSharedPointer<QTextDocument> document(int row);
    void jumpLatest();
    void jumpRow(int row);
    QString selectedText();
    QString messageText(int row);
    void copySelection();
    int search(const QString &text, int step = 0);
  signals:
    void userClicked(yc::Message message);
    void threadClicked(yc::Message message);
    void messageMenuRequested(yc::Message message, QPoint global);
    void focused();

  protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void wheelEvent(QWheelEvent *) override;
};

class Composer : public QLineEdit {
    Q_OBJECT
  public:
    explicit Composer(QWidget *parent = nullptr) : QLineEdit(parent) {}

    QStringList candidates;
    QStringList completions;
    QString completionHead, completionTail, completionValue, lastSent;
    int completionIndex = -1;
  signals:
    void completeRequested();
    void escapePressed();

  protected:
    bool event(QEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
};

class Pane : public QWidget {
    Q_OBJECT
  public:
    Pane(Hub *hub, QList<Source *> sources, QWidget *parent = nullptr, bool detached = false);
    ~Pane() override;
    Hub *hub;
    QList<QPointer<Source>> sources;
    QString uid;
    bool detached;
    QWidget *header, *replyBar, *searchBar;
    QLabel *headerText, *headerIcon, *replyLabel, *countLabel, *searchCount;
    QComboBox *target;
    Composer *input;
    QPushButton *sendButton, *emoteButton;
    ChatView *view;
    QLineEdit *searchInput;
    Message reply;
    bool sending = false;
    QJsonObject filter;
    std::function<bool(const Message &)> extraFilter;
    QString threadId;
    void refresh();
    void reload();
    void incoming(Source *source, Message message);
    void send();
    void startReply(Message message);
    void cancelReply();
    void openEmotes();
    void showSearch();
    void setSources(QList<Source *> sources);
    Source *targetSource() const;
    QString key() const;
    QJsonObject serialize() const;
  signals:
    void focused(Pane *pane);
    void menuRequested(Pane *pane, QPoint point);
    void userRequested(Pane *pane, Source *source, yc::Message message);
    void threadRequested(Source *source, yc::Message message);
    void messageMenu(Pane *pane, yc::Message message, QPoint point);

  protected:
    bool eventFilter(QObject *, QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
};

class Workspace : public QWidget {
    Q_OBJECT
  public:
    Workspace(Hub *hub, QWidget *parent = nullptr);
    Hub *hub;
    QString uid, title;
    QPointer<Pane> focusedPane;
    QVBoxLayout *layout;
    QPointer<Pane> zoomed;
    QList<QPointer<QWidget>> zoomHidden;
    void toggleZoom(Pane *pane);
    QList<Pane *> panes() const;
    Pane *add(QList<Source *> sources, Pane *anchor = nullptr, Qt::Orientation orientation = Qt::Horizontal);
    void closePane(Pane *pane);
    QJsonObject serialize() const;
    QJsonObject serializeNode(QWidget *node) const;
    QWidget *restoreNode(const QJsonObject &node, int depth = 0);
    void restore(const QJsonObject &saved);
  signals:
    void paneAdded(Pane *pane);
    void changed();
};
} // namespace yc
