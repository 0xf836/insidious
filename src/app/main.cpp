#include "ui/main_window.h"

#include <QApplication>
#include <QCommandLineParser>

#include <vips/vips8>

int main(int argc, char *argv[])
{
    if (VIPS_INIT(argv[0])) {
        vips_error_exit(nullptr);
    }

    vips_cache_set_max_mem(256 * 1024 * 1024);
    vips_cache_set_max_files(64);
    vips_cache_set_max(256);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Insidious"));
    QApplication::setOrganizationName(QStringLiteral("Insidious"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QApplication::translate("main", "Native Linux image viewer."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("path"), QApplication::translate("main", "Image file or folder to open."));
    parser.process(app);

    insidious::ui::MainWindow window;
    window.show();

    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        window.openPath(positional.first());
    }

    const int result = app.exec();
    vips_shutdown();
    return result;
}
