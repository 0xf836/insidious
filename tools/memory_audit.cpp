#include "core/archive_collection_source.h"
#include "core/animation_decode_session.h"
#include <QtConcurrent>
#include <QTemporaryDir>
#include "core/folder_collection_source.h"
#include "core/memory_image_cache.h"
#include "core/render_memory_budget.h"
#include "core/vips_image_decoder.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <vips/vips8>

namespace core = insidious::core;

namespace {
QString fixtureExtension = QStringLiteral("jpg");

constexpr qint64 MiB = 1024LL * 1024LL;

struct ScenarioResult {
    QString scenario;
    QString source;
    QString fixture;
    qint64 sourceBytes = 0;
    qint64 archiveEntryBufferBytes = 0;
    qint64 renderEstimateBytes = 0;
    qint64 displayedBytes = 0;
    qint64 cacheBytes = 0;
    qint64 peakRssBytes = 0;
    QString notes;
};

QString argValue(const QStringList &arguments, const QString &name)
{
    const qsizetype index = arguments.indexOf(name);
    if (index < 0 || index + 1 >= arguments.size()) {
        return {};
    }
    return arguments.at(index + 1);
}

qint64 statusValueBytes(const QString &name)
{
    QFile file(QStringLiteral("/proc/self/status"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }

    QTextStream stream(&file);
    QString line;
    while (stream.readLineInto(&line)) {
        if (!line.startsWith(name + QLatin1Char(':'))) {
            continue;
        }
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 2) {
            return 0;
        }
        bool ok = false;
        const qint64 valueKiB = parts.at(1).toLongLong(&ok);
        return ok ? valueKiB * 1024LL : 0;
    }

    return 0;
}

double bytesToMiB(qint64 bytes)
{
    return static_cast<double>(bytes) / static_cast<double>(MiB);
}

void printResult(const ScenarioResult &result)
{
    QTextStream out(stdout);
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(2);
    out << "scenario=" << result.scenario << '\n';
    out << "source=" << result.source << '\n';
    out << "fixture=" << result.fixture << '\n';
    out << "source_mib=" << bytesToMiB(result.sourceBytes) << '\n';
    out << "archive_entry_buffer_mib=" << bytesToMiB(result.archiveEntryBufferBytes) << '\n';
    out << "render_estimate_mib=" << bytesToMiB(result.renderEstimateBytes) << '\n';
    out << "displayed_mib=" << bytesToMiB(result.displayedBytes) << '\n';
    out << "cache_mib=" << bytesToMiB(result.cacheBytes) << '\n';
    out << "peak_rss_mib=" << bytesToMiB(result.peakRssBytes) << '\n';
    out << "notes=" << result.notes << '\n';
}

core::DecodeRequest fitRequest()
{
    core::DecodeRequest request;
    request.viewportSize = QSize(1920, 1080);
    request.fitToWindow = true;
    request.exifAutorotate = true;
    return request;
}

core::DecodeRequest zoomRequest()
{
    core::DecodeRequest request;
    request.viewportSize = QSize(1920, 1080);
    request.fitToWindow = false;
    request.regionRendering = true;
    request.scaleFactor = 2.0;
    request.exifAutorotate = true;
    return request;
}

qint64 renderEstimateFor(const core::DecodedImage &decoded)
{
    return decoded.image.isNull() ? 0 : decoded.image.sizeInBytes();
}

core::DecodedImage decodeOrFail(const core::ImageEntry &entry, const core::DecodeRequest &request, const QString &archivePassword = {})
{
    core::VipsImageDecoder decoder;
    core::DecodedImage decoded = decoder.decode(entry, request, archivePassword);
    if (!decoded.ok()) {
        QTextStream(stderr) << "decode failed: " << decoded.errorMessage << '\n';
        std::exit(2);
    }
    return decoded;
}

core::ImageEntry filesystemEntry(const QString &path)
{
    const core::ScanResult collection = core::singleImageCollection(path);
    if (!collection.ok() || collection.entries.isEmpty()) {
        QTextStream(stderr) << "invalid filesystem fixture: " << path << '\n';
        std::exit(2);
    }
    return collection.entries.first();
}

ScenarioResult filesystemFit(const QString &fixtureDir)
{
    const QString path = QDir(fixtureDir).filePath((QStringLiteral("large-fs.") + fixtureExtension));
    const core::ImageEntry entry = filesystemEntry(path);
    const core::DecodeRequest request = fitRequest();
    const core::DecodedImage decoded = decodeOrFail(entry, request);

    ScenarioResult result;
    result.scenario = QStringLiteral("filesystem-fit");
    result.source = QStringLiteral("filesystem");
    result.fixture = QFileInfo(path).fileName();
    result.sourceBytes = QFileInfo(path).size();
    result.renderEstimateBytes = renderEstimateFor(decoded);
    result.displayedBytes = decoded.image.sizeInBytes();
    result.peakRssBytes = statusValueBytes(QStringLiteral("VmHWM"));
    result.notes = QStringLiteral("single display-sized fit decode");
    return result;
}

ScenarioResult archiveFit(const QString &fixtureDir)
{
    const QString path = QDir(fixtureDir).filePath(QStringLiteral("large-archive.zip"));
    const core::ScanResult scan = core::scanArchive(path);
    if (!scan.ok() || scan.entries.isEmpty()) {
        QTextStream(stderr) << "invalid archive fixture: " << scan.errorMessage << '\n';
        std::exit(2);
    }

    const core::ImageEntry entry = scan.entries.first();
    const core::DecodeRequest request = fitRequest();
    const core::DecodedImage decoded = decodeOrFail(entry, request);

    ScenarioResult result;
    result.scenario = QStringLiteral("archive-fit");
    result.source = QStringLiteral("archive");
    result.fixture = QFileInfo(path).fileName();
    result.sourceBytes = QFileInfo(path).size();
    result.renderEstimateBytes = renderEstimateFor(decoded);
    result.displayedBytes = decoded.image.sizeInBytes();
    result.peakRssBytes = statusValueBytes(QStringLiteral("VmHWM"));
    result.notes = QStringLiteral("streamed archive entry plus fit decode");
    return result;
}

ScenarioResult zoomRender(const QString &fixtureDir)
{
    const QString path = QDir(fixtureDir).filePath((QStringLiteral("large-fs.") + fixtureExtension));
    const core::ImageEntry entry = filesystemEntry(path);
    const core::DecodeRequest request = zoomRequest();
    const core::DecodedImage decoded = decodeOrFail(entry, request);

    ScenarioResult result;
    result.scenario = QStringLiteral("zoom-render");
    result.source = QStringLiteral("filesystem");
    result.fixture = QFileInfo(path).fileName();
    result.sourceBytes = QFileInfo(path).size();
    result.renderEstimateBytes = renderEstimateFor(decoded);
    result.displayedBytes = decoded.image.sizeInBytes();
    result.peakRssBytes = statusValueBytes(QStringLiteral("VmHWM"));
    result.notes = QStringLiteral("scale=2 region-based zoom decode");
    return result;
}

ScenarioResult prefetchSimulation(const QString &fixtureDir)
{
    const QStringList names = {
        (QStringLiteral("prefetch-1.") + fixtureExtension),
        (QStringLiteral("prefetch-2.") + fixtureExtension),
        (QStringLiteral("prefetch-3.") + fixtureExtension),
    };
    const core::DecodeRequest request = fitRequest();
    core::MemoryImageCache cache;
    QVector<core::DecodedImage> retained;
    qint64 sourceBytes = 0;

    for (const QString &name : names) {
        const QString path = QDir(fixtureDir).filePath(name);
        sourceBytes += QFileInfo(path).size();
        const core::ImageEntry entry = filesystemEntry(path);
        core::DecodedImage decoded = decodeOrFail(entry, request);
        cache.insert(core::cacheKeyFor(entry, request), decoded);
        if (name == QStringLiteral("prefetch-2.") + fixtureExtension) {
            retained.append(decoded);
        }
    }

    qint64 displayedBytes = 0;
    for (const core::DecodedImage &decoded : retained) {
        displayedBytes += decoded.image.sizeInBytes();
    }

    ScenarioResult result;
    result.scenario = QStringLiteral("prefetch-simulation");
    result.source = QStringLiteral("filesystem");
    result.fixture = (QStringLiteral("prefetch-*.") + fixtureExtension);
    result.sourceBytes = sourceBytes;
    result.renderEstimateBytes = displayedBytes;
    result.displayedBytes = displayedBytes;
    result.cacheBytes = cache.bytesUsed();
    result.peakRssBytes = statusValueBytes(QStringLiteral("VmHWM"));
    result.notes = QStringLiteral("current displayed image plus current/neighbor cache entries");
    return result;
}

ScenarioResult concurrentDecode(const QString &fixtureDir)
{
    QList<QFuture<core::DecodedImage>> jobs;
    const auto request = fitRequest();
    for (int i = 1; i <= 3; ++i) {
        const auto entry = filesystemEntry(QDir(fixtureDir).filePath(QStringLiteral("prefetch-%1.%2").arg(i).arg(fixtureExtension)));
        jobs.append(QtConcurrent::run([entry, request]() { return decodeOrFail(entry, request); }));
    }
    ScenarioResult result;
    result.scenario = QStringLiteral("concurrent-decode");
    result.source = QStringLiteral("filesystem");
    result.fixture = QStringLiteral("prefetch-*.") + fixtureExtension;
    for (auto &job : jobs) { result.displayedBytes += job.result().image.sizeInBytes(); }
    result.peakRssBytes = statusValueBytes(QStringLiteral("VmHWM"));
    result.notes = QStringLiteral("three concurrent source decodes with retained display results");
    return result;
}

ScenarioResult animationPlayback()
{
    QTemporaryDir directory;
    if (!directory.isValid()) { std::exit(2); }
    const auto path = directory.filePath(QStringLiteral("animation.jxl"));
    {
        auto first = vips::VImage::black(1024, 768, vips::VImage::option()->set("bands", 4))
            .new_from_image({255, 0, 0, 128}).copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB));
        auto frames = first.join(first.new_from_image({0, 0, 255, 255}), VIPS_DIRECTION_VERTICAL);
        frames.set(VIPS_META_PAGE_HEIGHT, 768);
        frames.set("delay", std::vector<int>{40, 80});
        frames.jxlsave(path.toUtf8().constData(), vips::VImage::option()->set("lossless", true)->set("effort", 1));
    }
    core::AnimationDecodeSession session;
    if (!session.open(filesystemEntry(path))) { QTextStream(stderr) << session.errorMessage(); std::exit(2); }
    core::DecodedImage displayed;
    qint64 warm = 0;
    for (int loop = 0; loop < 100; ++loop) {
        if (loop && (!session.restart() || !session.advance())) { std::exit(2); }
        displayed = session.render(fitRequest());
        if (!displayed.ok() || !session.advance()) { std::exit(2); }
        displayed = session.render(fitRequest());
        if (!displayed.ok()) { std::exit(2); }
        if (loop == 10) { warm = vips_tracked_get_mem(); }
    }
    ScenarioResult result;
    result.scenario = QStringLiteral("animation-playback");
    result.source = QStringLiteral("filesystem");
    result.fixture = QStringLiteral("animation.jxl");
    result.sourceBytes = QFileInfo(path).size();
    result.displayedBytes = displayed.image.sizeInBytes();
    result.peakRssBytes = statusValueBytes(QStringLiteral("VmHWM"));
    result.notes = QStringLiteral("100 two-frame loops; tracked growth after warmup: %1 bytes").arg(static_cast<qint64>(vips_tracked_get_mem()) - warm);
    return result;
}

void printUsage()
{
    QTextStream(stderr)
        << "Usage: insidious_memory_audit --fixture-dir DIR --scenario "
        << "filesystem-fit|archive-fit|zoom-render|prefetch-simulation|concurrent-decode|animation-playback [--format jpg|jxl]\n";
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.contains(QStringLiteral("--help")) || arguments.contains(QStringLiteral("-h"))) {
        printUsage();
        return 0;
    }

    const QString fixtureDir = argValue(arguments, QStringLiteral("--fixture-dir"));
    const QString scenario = argValue(arguments, QStringLiteral("--scenario"));
    const QString format = argValue(arguments, QStringLiteral("--format"));
    if (!format.isEmpty()) {
        if (format != QLatin1String("jpg") && format != QLatin1String("jxl")) { printUsage(); return 2; }
        fixtureExtension = format;
    }
    if (fixtureDir.isEmpty() || scenario.isEmpty()) {
        printUsage();
        return 1;
    }

    if (VIPS_INIT(argv[0])) {
        QTextStream(stderr) << vips_error_buffer() << '\n';
        return 2;
    }

    vips_cache_set_max_mem(256 * 1024 * 1024);
    vips_cache_set_max_files(64);
    vips_cache_set_max(256);

    int result = 0;
    if (scenario == QLatin1String("filesystem-fit")) {
        printResult(filesystemFit(fixtureDir));
    } else if (scenario == QLatin1String("archive-fit")) {
        printResult(archiveFit(fixtureDir));
    } else if (scenario == QLatin1String("zoom-render")) {
        printResult(zoomRender(fixtureDir));
    } else if (scenario == QLatin1String("prefetch-simulation")) {
        printResult(prefetchSimulation(fixtureDir));
    } else if (scenario == QLatin1String("concurrent-decode")) {
        printResult(concurrentDecode(fixtureDir));
    } else if (scenario == QLatin1String("animation-playback")) {
        printResult(animationPlayback());
    } else {
        printUsage();
        result = 1;
    }

    vips_shutdown();
    return result;
}
