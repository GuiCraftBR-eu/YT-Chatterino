#include "application.hpp"
#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>
#endif
int main(int argc, char **argv) {
#ifdef Q_OS_WIN
    SetCurrentProcessExplicitAppUserModelID(L"GuiCraftBR.YTChatterino");
#endif
    QApplication app(argc, argv);
    app.setApplicationName("YT-Chatterino");
    app.setApplicationVersion(APP_VERSION);
    app.setOrganizationName("YT-Chatterino");
    app.setWindowIcon(QIcon(":/ytchatterino.ico"));
    app.setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#000000"));
    palette.setColor(QPalette::Base, QColor("#000000"));
    palette.setColor(QPalette::AlternateBase, QColor("#0e0e0e"));
    palette.setColor(QPalette::WindowText, QColor("#efeff1"));
    palette.setColor(QPalette::Text, QColor("#efeff1"));
    palette.setColor(QPalette::Button, QColor("#181818"));
    palette.setColor(QPalette::ButtonText, QColor("#efeff1"));
    palette.setColor(QPalette::Highlight, QColor("#365f85"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, QColor("#181818"));
    palette.setColor(QPalette::ToolTipText, QColor("#efeff1"));
    app.setPalette(palette);
    app.setStyleSheet("QMainWindow,QDialog{background:#000;color:#efeff1;} "
                      "QLineEdit,QTextEdit,QPlainTextEdit,QSpinBox,QDoubleSpinBox,QComboBox{background:#111;"
                      "color:#efeff1;border:1px solid #333;padding:3px;selection-background-color:#365f85;} "
                      "QPushButton,QToolButton{background:#181818;color:#efeff1;border:0;padding:5px;} "
                      "QPushButton:hover,QToolButton:hover{background:#303030;} QTabWidget::pane{border:0;} "
                      "QTabBar::tab{background:#000;color:#ccc;padding:5px 10px;border-top:2px solid "
                      "transparent;} QTabBar::tab:selected{background:#181818;color:#fff;border-top:2px "
                      "solid #4a9cdb;} QScrollBar:vertical{background:#000;width:9px;} "
                      "QScrollBar::handle:vertical{background:#242424;min-height:24px;} "
                      "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;} "
                      "QSplitter::handle{background:#292929;width:3px;height:3px;} "
                      "QMenu{background:#111;color:#efeff1;border:1px solid #333;} "
                      "QMenu::item:selected{background:#303030;} QCheckBox{color:#efeff1;} "
                      "QTabBar::close-button{image:url(:/close.svg);width:12px;height:12px;} "
                      "QTabBar::close-button:hover{background:#333;} "
                      "QGroupBox{border:1px solid #222;margin-top:8px;padding-top:8px;} "
                      "QGroupBox::title{color:#aaa;} QLabel{background:transparent;}");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"profile", "Use an isolated profile directory.", "path"});
    parser.addOption({"apply-update", "Apply a prepared native update.", "manifest"});
    parser.addOption({"updated", "Acknowledge a completed update.", "manifest"});
    parser.process(app);
    if (parser.isSet("apply-update")) {
        return yc::Updater::applyUpdate(parser.value("apply-update"));
    }
    yc::MainWindow window(parser.value("profile"));
    window.show();
    yc::darkTitlebar(&window);
    if (parser.isSet("updated")) {
        const auto manifest = parser.value("updated");
        QTimer::singleShot(500, &window, [manifest] { yc::Updater::acknowledge(manifest); });
    }
    return app.exec();
}
