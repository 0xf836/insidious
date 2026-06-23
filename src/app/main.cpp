#include "ui/main_window.h"

#include <QApplication>
#include <QCommandLineParser>

#include <glib.h>
#include <vips/vips8>

#include <string_view>

namespace {

bool contains(std::string_view text, std::string_view needle)
{
    return text.find(needle) != std::string_view::npos;
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

    return contains(text, "/vips-magick.so") || contains(text, "/vips-openslide.so");
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
    g_log_remove_handler("VIPS", vipsLogHandlerId);
    return result;
}
