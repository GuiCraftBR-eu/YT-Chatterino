#include "application.hpp"
#include <algorithm>

namespace yc {
namespace {
struct ActivityRow {
    QPointer<Source> source;
    Message event;
    QVector<Message> recipients;
    QStringList recipientNames;
    QString key;
};

QString activityGroup(const Source *source, const Message &event) {
    const QString group = str(event.activity.value("group"));
    return source && !group.isEmpty() ? source->key + ":" + group : QString();
}

QString activityDescription(const Message &event) {
    const QString description = event.alertText.isEmpty() ? event.text : event.alertText;
    return event.name.isEmpty() ? description : event.name + " · " + description;
}
}

void MainWindow::showRecentActivity(Pane *pane, Source *only) {
    if (!pane) return;
    QList<QPointer<Source>> selected;
    for (auto source : pane->sources)
        if (source && !source->isVod && (!only || source == only)) selected << source;
    if (selected.isEmpty()) return;

    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("recent-activity-dialog");
    dialog->setWindowTitle(cfg.T("Recent activity"));
    dialog->setStyleSheet("QDialog#recent-activity-dialog{background:#111116;color:#efeff1;}");
    dialog->resize(760, 480);
    auto *layout = new QVBoxLayout(dialog);
    auto *title = new QLabel(cfg.T("Recent activity"), dialog);
    title->setStyleSheet("font-size:18px;font-weight:600;color:#f5f4f8;");
    layout->addWidget(title);
    auto *description = new QLabel(cfg.T("Subscriptions, gifts and support received in chat while connected. Earlier events and external donations may be unavailable."), dialog);
    description->setWordWrap(true);
    description->setStyleSheet("color:#aaaab3;");
    layout->addWidget(description);
    auto *filter = new QComboBox(dialog);
    filter->setObjectName("activity-filter");
    filter->setStyleSheet("QComboBox{background:#24242a;color:#efeff1;border:1px solid #45454e;border-radius:4px;padding:5px 8px;}");
    filter->addItem(cfg.T("All activity"), "all");
    filter->addItem(cfg.T("Subscriptions and memberships"), "subscriptions");
    filter->addItem(cfg.T("Gifted subscriptions"), "gifts");
    filter->addItem(cfg.T("Bits and support"), "support");
    layout->addWidget(filter, 0, Qt::AlignLeft);
    auto *tree = new QTreeWidget(dialog);
    tree->setObjectName("recent-activity-tree");
    tree->setColumnCount(3);
    tree->setHeaderLabels({cfg.T("Activity"), cfg.T("Channel"), cfg.T("Time")});
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    tree->setAlternatingRowColors(true);
    tree->setRootIsDecorated(true);
    tree->setStyleSheet("QTreeWidget{background:#141418;color:#efeff1;alternate-background-color:#1b1b20;border:1px solid #38383f;border-radius:5px;outline:0;}"
                        "QTreeWidget::item{padding:6px 3px;} QTreeWidget::item:selected{background:#443467;color:white;}"
                        "QHeaderView::section{background:#222229;color:#dedee5;border:0;padding:7px;}");
    layout->addWidget(tree, 1);
    auto *empty = new QLabel(cfg.T("No activity received yet."), dialog);
    empty->setObjectName("activity-empty"); empty->setAlignment(Qt::AlignCenter);
    empty->setStyleSheet("color:#aaaab3;padding:12px;");
    layout->addWidget(empty);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(close, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(close);

    const auto rebuild = [this, selected, tree, filter, empty] {
        QSet<QString> expanded;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto *item = tree->topLevelItem(i);
            if (item->isExpanded()) expanded.insert(item->data(0, Qt::UserRole).toString());
        }
        const int scroll = tree->verticalScrollBar()->value();
        tree->clear();
        QVector<ActivityRow> rows;
        QHash<QString, int> groups;
        for (const auto &source : selected) if (source) {
            for (const auto &event : source->activityHistory) {
                if (str(event.activity["type"]) != "gift_bundle") continue;
                QString key = activityGroup(source, event);
                if (key.isEmpty()) key = source->key + ":single:" + (event.id.isEmpty() ? QString::number(event.seq) : event.id);
                if (groups.contains(key)) continue;
                ActivityRow row; row.source = source; row.event = event; row.key = key;
                for (const auto &name : event.activity["recipients"].toArray())
                    if (!str(name).isEmpty()) row.recipientNames << str(name).left(100);
                groups.insert(key, rows.size()); rows << row;
            }
        }
        for (const auto &source : selected) if (source) {
            for (const auto &event : source->activityHistory) {
                const QString type = str(event.activity["type"]);
                if (type == "gift_bundle") continue;
                const QString group = activityGroup(source, event);
                if (type == "gift_recipient" && !group.isEmpty() && groups.contains(group)) {
                    rows[groups[group]].recipients << event; continue;
                }
                ActivityRow row; row.source = source; row.event = event;
                row.key = source->key + ":event:" + (event.id.isEmpty() ? QString::number(event.seq) : event.id);
                if (type == "gift_recipient") row.recipients << event;
                rows << row;
            }
        }
        std::stable_sort(rows.begin(), rows.end(), [](const ActivityRow &a, const ActivityRow &b) {
            return messageOrder(b.event, a.event);
        });
        const QString category = filter->currentData().toString();
        for (const auto &row : rows) {
            if (!row.source) continue;
            const QString type = str(row.event.activity["type"]), kind = row.event.alertKind;
            const bool gift = type == "gift_bundle" || type == "gift_recipient";
            const bool subscription = gift || type == "subscription" || kind == "subscription" || kind == "prime";
            if ((category == "subscriptions" && !subscription) || (category == "gifts" && !gift) ||
                (category == "support" && subscription)) continue;
            const QString platform = row.source->platform == "twitch" ? "Twitch" : row.source->platform == "youtube" ? "YouTube" : "Kick";
            const QString when = row.event.stamp.isValid() ? row.event.stamp.toLocalTime().toString("yyyy-MM-dd HH:mm:ss") : QString();
            auto *item = new QTreeWidgetItem(tree, {activityDescription(row.event), platform + " · " + row.source->name(), when});
            item->setData(0, Qt::UserRole, row.key);
            item->setIcon(0, QIcon(QPixmap::fromImage(channelIcon(row.source, &images, 20))));
            if (gift) {
                for (const auto &name : row.recipientNames) new QTreeWidgetItem(item, {name});
                for (const auto &recipient : row.recipients) {
                    const QString name = str(recipient.activity["recipient"]);
                    new QTreeWidgetItem(item, {name.isEmpty() ? activityDescription(recipient) : name});
                }
                if (!item->childCount()) new QTreeWidgetItem(item, {cfg.T("Recipient details unavailable")});
                item->setExpanded(expanded.contains(row.key));
            }
        }
        empty->setVisible(tree->topLevelItemCount() == 0);
        tree->verticalScrollBar()->setValue(scroll);
    };
    auto *refresh = new QTimer(dialog); refresh->setSingleShot(true); refresh->setInterval(100);
    connect(refresh, &QTimer::timeout, dialog, rebuild);
    connect(filter, qOverload<int>(&QComboBox::currentIndexChanged), dialog, [rebuild](int) { rebuild(); });
    for (const auto &source : selected) if (source) {
        connect(source, &Source::message, dialog, [refresh](const Message &message) {
            if (!message.alertKind.isEmpty()) refresh->start();
        });
        connect(source, &Source::changed, dialog, [refresh] { refresh->start(); });
        connect(source, &QObject::destroyed, dialog, [refresh] { refresh->start(); });
    }
    rebuild();
    dialog->show();
}
} // namespace yc
