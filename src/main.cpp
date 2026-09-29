#include <QApplication>
#include "ui/mainwindow.h"
#include "services/librarycontroller.h"
#include <QCommandLineParser>
#include <QIcon>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Virgil");
    QCoreApplication::setApplicationName("Virgil Inferno");
    QCoreApplication::setApplicationVersion("0.1.0");
    QGuiApplication::setDesktopFileName("virgil-desktop");
    QApplication::setWindowIcon(QIcon::fromTheme(
        "virgil-desktop", QIcon(":/virgil/virgil-logo.png")));
    QCommandLineParser parser;
    parser.setApplicationDescription("Virgil Inferno");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"library", "Open an existing library instead of the remembered one.", "directory"});
    parser.process(app);
    virgil::MainWindow window;
    window.show();
    QTimer::singleShot(0, &window, [&] {
        if (parser.isSet("library")) window.controller()->openLibrary(parser.value("library"));
        else window.openRememberedLibrary();
    });
    return app.exec();
}
