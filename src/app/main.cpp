#include "ui/main_window.h"
#include "build_version.h"

#include <QApplication>
#include <QCommandLineParser>

#include <glib.h>
#include <vips/vips8>

#include <iostream>
#include <string_view>

namespace {

bool contains(std::string_view text, std::string_view needle)
{
    return text.find(needle) != std::string_view::npos;
}

bool isVersionArgument(const char *argument)
{
    return argument != nullptr
        && (std::string_view(argument) == "--version" || std::string_view(argument) == "-v");
}

bool isOptionalVipsModuleDependencyWarning(const char *message)
{
    if (message == nullptr) {
        return false;
    }

    const std::string_view text(message);
    if (!contains(text, "unable to load") || !contains(text, "cannot open shared object file")) {
        return false;
    }

    return contains(text, "/vips-openslide.so");
}

void vipsLogHandler(const gchar *logDomain, GLogLevelFlags logLevel, const gchar *message, gpointer userData)
{
    if ((logLevel & G_LOG_LEVEL_WARNING) != 0 && isOptionalVipsModuleDependencyWarning(message)) {
        return;
    }

    g_log_default_handler(logDomain, logLevel, message, userData);
}

} // namespace

int main(int argc, char *argv[])
{
    for (int index = 1; index < argc; ++index) {
        if (isVersionArgument(argv[index])) {
            std::cout << "Insidious " << insidious::build::Version << '\n';
            return 0;
        }
    }

    const guint vipsLogHandlerId = g_log_set_handler(
        "VIPS",
        static_cast<GLogLevelFlags>(G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION),
        vipsLogHandler,
        nullptr);

    if (VIPS_INIT(argv[0])) {
        vips_error_exit(nullptr);
    }

    vips_cache_set_max_mem(256 * 1024 * 1024);
    vips_cache_set_max_files(64);
    vips_cache_set_max(256);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Insidious"));
    QApplication::setOrganizationName(QStringLiteral("Insidious"));
    QApplication::setApplicationVersion(QString::fromLatin1(insidious::build::Version));

    QCommandLineParser parser;
    parser.setApplicationDescription(QApplication::translate("main", "Native Linux image viewer."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("path"), QApplication::translate("main", "Image file, folder, or archive to open."));
    parser.process(app);

    insidious::ui::MainWindow window;
    window.show();

    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        window.openPath(positional.first());
    }

    const int result = app.exec();
    vips_shutdown();
    g_log_remove_handler("VIPS", vipsLogHandlerId);
    return result;
}
