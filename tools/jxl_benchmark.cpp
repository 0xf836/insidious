#include "core/folder_collection_source.h"
#include "core/jxl_rs_image_decoder.h"
#include "core/vips_image_decoder.h"
#include "core/vips_raster_renderer.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <algorithm>
#include <memory>

using namespace insidious::core;
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    bool validCount = false;
    const int count = args.size() == 6 ? args[4].toInt(&validCount) : 0;
    if (args.size() != 6 || !validCount || count < 1 || count > 1000
        || (args[1] != "jxl-rs" && args[1] != "libvips") || (args[3] != "native" && args[3] != "fit")) {
        QTextStream(stderr) << "Usage: insidious_jxl_benchmark jxl-rs|libvips INPUT native|fit REPETITIONS OUTPUT.json\n";
        return 2;
    }
    if (VIPS_INIT(argv[0])) { return 2; }
    int status = 0;
    try {
        vips_cache_set_max(0);
        const auto entry = imageEntryFromFileInfo(QFileInfo(args[2]));
        DecodeRequest request;
        request.viewportSize = QSize(1920, 1080);
        QJsonArray times;
        std::vector<double> sorted;
        QByteArray expectedHash;
        for (int i = -2; i < count; ++i) {
            QElapsedTimer timer;
            timer.start();
            DecodedImage display;
            vips::VImage native;
            if (args[1] == "libvips") {
                native = vips::VImage::new_from_file(args[2].toUtf8().constData(),
                    vips::VImage::option()->set("access", "sequential")->set("revalidate", true));
                if (args[3] == "fit") { display = renderVipsImage(native, request); }
                else { native = native.copy_memory(); }
            } else if (args[3] == "fit") {
                display = VipsImageDecoder().decode(entry, request);
            } else {
                JxlRasterDecoder decoder;
                double delay;
                bool more;
                if (!decoder.open(entry, {}) || !decoder.readFrame(&native, &delay, &more)) {
                    throw std::runtime_error(decoder.errorMessage().toStdString());
                }
            }
            const double milliseconds = timer.nsecsElapsed() / 1e6;
            QByteArray hash;
            if (args[3] == "fit") {
                if (!display.ok()) { throw std::runtime_error(display.errorMessage.toStdString()); }
                hash = QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(display.image.constBits()),
                    display.image.sizeInBytes()), QCryptographicHash::Sha256).toHex();
            } else {
                size_t size;
                std::unique_ptr<void, decltype(&g_free)> bytes(native.write_to_memory(&size), g_free);
                hash = QCryptographicHash::hash(QByteArrayView(static_cast<const char *>(bytes.get()), size), QCryptographicHash::Sha256).toHex();
            }
            if (expectedHash.isEmpty()) { expectedHash = hash; }
            if (hash != expectedHash) { throw std::runtime_error("Decoded pixels changed between repetitions"); }
            if (i >= 0) { times.append(milliseconds); sorted.push_back(milliseconds); }
            if (!native.is_null()) { vips_image_invalidate_all(native.get_image()); }
        }
        std::sort(sorted.begin(), sorted.end());
        const double median = (sorted[(sorted.size() - 1) / 2] + sorted[sorted.size() / 2]) / 2;
        const QJsonObject result {{"backend", args[1]}, {"mode", args[3]}, {"image", QFileInfo(args[2]).fileName()},
            {"milliseconds", times}, {"median_ms", median}, {"sha256", QString::fromLatin1(expectedHash)},
            {"warmups", 2}, {"viewport_width", 1920}, {"viewport_height", 1080},
            {"rayon_threads", QString::fromLocal8Bit(qgetenv("RAYON_NUM_THREADS"))}, {"vips_threads", vips_concurrency_get()}};
        QFile output(args[5]);
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { throw std::runtime_error("Cannot create output file"); }
        const auto json = QJsonDocument(result).toJson();
        if (output.write(json) != json.size()) { throw std::runtime_error("Cannot write benchmark output"); }
        QTextStream(stdout) << args[1] << ' ' << args[3] << ": " << median << " ms\n";
    } catch (const std::exception &error) { QTextStream(stderr) << error.what() << '\n'; status = 1; }
    vips_shutdown();
    return status;
}
