#include "application.hpp"

using namespace yc;

static void process() {
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

static bool settle(ChatView *view) {
    QElapsedTimer timeout, quiet;
    timeout.start();
    quiet.start();
    while (timeout.elapsed() < 3000) {
        process();
        if (view->resizeTimer.isActive() || view->layoutWidth != view->viewport()->width()) {
            quiet.restart();
        }
        if (quiet.elapsed() >= 30) {
            process();
            return true;
        }
        QThread::msleep(1);
    }
    process();
    qCritical("O layout nativo não concluiu dentro do limite.");
    return false;
}

int main(int argc, char **argv) {
    QElapsedTimer startup;
    startup.start();
    QApplication application(argc, argv);
    configureApplication(application);
    QCommandLineParser arguments;
    arguments.addOption({"output", "JSON result path", "path"});
    arguments.addOption({"profile", "Isolated benchmark profile directory", "path"});
    arguments.process(application);
    const auto output = arguments.value("output"), profile = arguments.value("profile");
    if (output.isEmpty() || profile.isEmpty() || QDir(profile).absolutePath() == QDir::homePath()) {
        qCritical("Use --output and a separate --profile directory.");
        return 2;
    }
    MainWindow window(profile, true);
    int requests = 0;
    window.net.transport = [&](QUrl, QByteArray, QByteArray, QMap<QByteArray, QByteArray>,
                               BytesCallback done) {
        ++requests;
        done({}, "Network disabled for benchmark");
    };
    auto appearance = window.cfg.appearance();
    appearance["font_size"] = 12;
    appearance["message_font_size"] = 12;
    appearance["username_font_size"] = 12;
    appearance["show_timestamps"] = true;
    appearance["alternate_rows"] = true;
    appearance["time_format"] = "HH:mm";
    window.cfg.data["appearance"] = appearance;
    window.cfg.data["automatic_updates"] = false;
    auto *source = window.hub.source("twitch", "benchmark");
    source->header = {
        {"channel", "benchmark"}, {"title", "Benchmark"}, {"category", "Just Chatting"}, {"live", false}};
    auto *pane = window.addWorkspace({source})->focusedPane.data();
    window.setGeometry(550, 80, 520, 760);
    window.show();
    darkTitlebar(&window);
    process();
    pane->view->viewport()->repaint();
    const auto startupMs = startup.nsecsElapsed() / 1.0e6;
    writeObject(output + ".ready", {{"ready", true}});
    if (!settle(pane->view)) {
        return 3;
    }
    QVector<Message> messages;
    const auto stamp = QDateTime(QDate(2026, 10, 24), QTime(12, 34), QTimeZone::UTC);
    for (int i = 0; i < 2000; ++i) {
        Message message;
        message.seq = i + 1;
        message.id = QString::number(i);
        message.source = source->key;
        message.name = QString("User%1").arg(i % 20, 2, 10, QChar('0'));
        message.text = QString("Message %1: Retained chat benchmark with a link https://example.com/%1 and a "
                               "longer sentence that wraps in a narrow window.")
                           .arg(i);
        message.color = "#4a9cdb";
        message.stamp = stamp;
        message.history = true;
        messages << message;
    }
    source->history = messages;
    QElapsedTimer loading;
    loading.start();
    pane->reload();
    process();
    pane->view->viewport()->repaint();
    if (!settle(pane->view)) {
        return 3;
    }
    const auto loadMs = loading.nsecsElapsed() / 1.0e6;
    QElapsedTimer resizing;
    resizing.start();
    const QList<QSize> sizes{{360, 680}, {900, 760}, {520, 760}, {680, 640}};
    for (int i = 0; i < 80; ++i) {
        window.resize(sizes[i % sizes.size()]);
        process();
    }
    if (!settle(pane->view)) {
        return 3;
    }
    const auto resizeMs = resizing.nsecsElapsed() / 1.0e6;
    QJsonObject result{{"version", APP_VERSION},
                       {"startup_internal_ms", startupMs},
                       {"load_2000_ms", loadMs},
                       {"resize_80_ms", resizeMs},
                       {"messages", pane->view->messages.size()},
                       {"cached_documents", pane->view->documents.size()},
                       {"network_requests", requests},
                       {"device_pixel_ratio", window.devicePixelRatioF()},
                       {"chat_font_pixels", QFontMetrics(window.cfg.messageFont()).height()},
                       {"qt", qVersion()}};
    const bool saved = writeObject(output, result);
    window.close();
    return saved && !requests && result.value("messages").toInt() == 2000 ? 0 : 1;
}
