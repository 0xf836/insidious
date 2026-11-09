#include "core/animation_decode_session.h"
#include "core/archive_collection_source.h"
#include "core/render_memory_budget.h"
#include "core/vips_raster_renderer.h"
#include "core/jxl_rs_image_decoder.h"
#include <cmath>
#include <limits>

#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <algorithm>

namespace insidious::core {
struct AnimationDecodeSession::Data {
    QByteArray bytes;
    vips::VImage sequence;
    vips::VImage current;
    vips::VImage previous;
    AnimationInfo info;
    QString error;
    int index = -1;
    int previousIndex = -1;
    ~Data() { if (!sequence.is_null()) { vips_image_invalidate_all(sequence.get_image()); } }
    qint64 nativeBytes = 0;
    bool jxl = false;
    std::unique_ptr<JxlRasterDecoder> jxlDecoder;
    QVector<ImageMetadataField> metadata;
};
AnimationDecodeSession::AnimationDecodeSession() : d(std::make_unique<Data>()) {}
AnimationDecodeSession::~AnimationDecodeSession() = default;
const AnimationInfo &AnimationDecodeSession::info() const { return d->info; }
QString AnimationDecodeSession::errorMessage() const { return d->error; }
int AnimationDecodeSession::frameIndex() const { return d->index; }
bool AnimationDecodeSession::candidate(const ImageEntry &entry)
{
    const QString suffix = QFileInfo(entry.source == ImageEntrySource::Archive ? entry.archiveEntryPath : entry.absolutePath).suffix().toLower();
    return suffix == QLatin1String("gif") || suffix == QLatin1String("webp") || suffix == QLatin1String("jxl");
}

bool AnimationDecodeSession::open(const ImageEntry &entry, const QString &password)
{
    d = std::make_unique<Data>();
    if (!candidate(entry)) { return true; }
    if (entry.source == ImageEntrySource::Archive) {
        auto read = readArchiveEntry(entry, password, SourceByteLimit);
        if (!read.ok()) { d->error = read.errorMessage; return false; }
        d->bytes = std::move(read.data);
    } else {
        QFile file(entry.absolutePath);
        if (!file.open(QIODevice::ReadOnly)) { d->error = file.errorString(); return false; }
        if (file.size() > SourceByteLimit) {
            d->error = QObject::tr("Animation source exceeds the memory limit."); return false;
        }
        d->bytes = file.read(SourceByteLimit + 1);
        if (d->bytes.size() > SourceByteLimit || file.error() != QFile::NoError) {
            d->error = QObject::tr("Could not read animation within the memory limit."); return false;
        }
    }
    d->jxl = QFileInfo(entry.source == ImageEntrySource::Archive ? entry.archiveEntryPath : entry.absolutePath).suffix().compare(QLatin1String("jxl"), Qt::CaseInsensitive) == 0;
    if (!restart()) { return false; }
    if (!d->info.animated()) {
        d = std::make_unique<Data>();
        return true;
    }
    return advance();
}

bool AnimationDecodeSession::restart()
{
    try {
        d->previous = d->current;
        d->previousIndex = d->index;
        d->current = {};
        if (!d->sequence.is_null()) { vips_image_invalidate_all(d->sequence.get_image()); }
        d->sequence = {};
        d->index = -1;
        if (d->jxl) {
            if (d->info.delaysMs.isEmpty()) {
                JxlRasterDecoder scanner;
                if (!scanner.openBytes(d->bytes, true)) { d->error = scanner.errorMessage(); return false; }
                const auto &header = scanner.info();
                if (!header.animated) { return true; }
                if (header.totalPlays > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
                    d->error = QObject::tr("Invalid JPEG XL loop count."); return false;
                }
                d->info.canvasSize = QSize(header.width, header.height);
                d->info.totalPlays = static_cast<int>(header.totalPlays);
                const auto native = estimateRenderMemory(d->info.canvasSize, header.bands, 1.0, header.bytesPerSample);
                if (native.overflowed || native.bytes > RasterByteLimit / 2) {
                    d->error = QObject::tr("Animation canvas exceeds the memory limit."); return false;
                }
                d->nativeBytes = native.bytes;
                bool more = true;
                while (more) {
                    double delay = -1;
                    if (!scanner.readFrame(nullptr, &delay, &more, RasterByteLimit / 2)) { d->error = scanner.errorMessage(); return false; }
                    if (delay < 0) { d->info = {}; return true; }
                    if (!std::isfinite(delay) || delay > std::numeric_limits<int>::max()) {
                        d->error = QObject::tr("Invalid JPEG XL frame duration."); return false;
                    }
                    d->info.delaysMs.append(delay == 0 ? 100 : std::max(10, static_cast<int>(std::lround(delay))));
                    if (d->info.delaysMs.size() > 65536) { d->error = QObject::tr("Animation has too many frames."); return false; }
                }
                if (!scanner.finishMetadata(&d->metadata)) { d->error = scanner.errorMessage(); return false; }
            }
            d->jxlDecoder = std::make_unique<JxlRasterDecoder>();
            if (!d->jxlDecoder->openBytes(d->bytes)) { d->error = d->jxlDecoder->errorMessage(); return false; }
            return true;
        }
        d->sequence = vips::VImage::new_from_buffer(d->bytes.constData(), d->bytes.size(), "",
            vips::VImage::option()->set("n", -1)->set("access", "sequential"));
        d->info = {};
        if (d->sequence.get_typeof("delay") == 0 || d->sequence.get_typeof(VIPS_META_PAGE_HEIGHT) == 0) { return true; }
        auto delays = d->sequence.get_array_int("delay");
        if (delays.size() < 2 || std::any_of(delays.begin(), delays.end(), [](int ms) { return ms < 0; })) { return true; }
        const int height = d->sequence.get_int(VIPS_META_PAGE_HEIGHT);
        if (height <= 0 || d->sequence.height() / height != static_cast<int>(delays.size()) || d->sequence.height() % height != 0) {
            throw std::runtime_error(QObject::tr("Invalid animation frame layout.").toStdString());
        }
        d->info.canvasSize = QSize(d->sequence.width(), height);
        for (int delay : delays) { d->info.delaysMs.append(delay == 0 ? 100 : std::max(10, delay)); }
        d->info.totalPlays = d->sequence.get_typeof("loop") ? std::max(0, d->sequence.get_int("loop")) : 1;
        const auto native = estimateRenderMemory(d->info.canvasSize, d->sequence.bands(), 1.0,
            static_cast<int>(vips_format_sizeof(d->sequence.format())));
        if (native.overflowed || native.bytes > RasterByteLimit / 2) {
            throw std::runtime_error(QObject::tr("Animation canvas exceeds the memory limit.").toStdString());
        }
        d->nativeBytes = native.bytes;
        return true;
    } catch (const std::exception &error) { d->error = QString::fromUtf8(error.what()); return false; }
}

bool AnimationDecodeSession::advance()
{
    try {
        if (!d->info.animated() || d->index + 1 >= d->info.delaysMs.size()) { return false; }
        const int next = d->index + 1;
        if (!d->current.is_null()) { d->previous = {}; }
        if (d->jxl) {
            vips::VImage image;
            double duration;
            bool more;
            if (!d->jxlDecoder->readFrame(&image, &duration, &more, RasterByteLimit / 2)) { d->error = d->jxlDecoder->errorMessage(); return false; }
            if (!d->current.is_null()) { d->previous = d->current; d->previousIndex = d->index; }
            d->current = image;
            d->index = next;
            return true;
        }
        auto image = d->sequence.extract_area(0, next * d->info.canvasSize.height(),
            d->info.canvasSize.width(), d->info.canvasSize.height()).copy_memory();
        // A detached frame must not retain sequence geometry.
        image.remove(VIPS_META_PAGE_HEIGHT);
        if (!d->current.is_null()) { d->previous = d->current; d->previousIndex = d->index; }
        d->current = image;
        vips_image_invalidate_all(d->sequence.get_image());
        d->index = next;
        return true;
    } catch (const std::exception &error) { d->error = QString::fromUtf8(error.what()); return false; }
}

DecodedImage AnimationDecodeSession::render(const DecodeRequest &request, qint64 retainedDisplayBytes, int frameIndex) const
{
    DecodeRequest bounded = request;
    // Reserve both native frame copies during advance and the displayed raster.
    const qint64 available = RasterByteLimit - 2 * d->nativeBytes - retainedDisplayBytes;
    bounded.maxDecodedBytes = std::min(request.maxDecodedBytes, available / 3);
    if (bounded.maxDecodedBytes <= 0) {
        DecodedImage result;
        result.errorMessage = QObject::tr("Animation exceeds the raster memory limit.");
        result.errorReason = DecodeErrorReason::RenderTooLarge;
        return result;
    }
    auto result = renderVipsImage(frameIndex >= 0 && frameIndex == d->previousIndex && !d->previous.is_null() ? d->previous : d->current, bounded);
    if (d->jxl) { result.metadataFields = d->metadata; }
    return result;
}
}
