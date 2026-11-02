#include "core/animation_decode_session.h"
#include "core/archive_collection_source.h"
#include "core/render_memory_budget.h"
#include "core/vips_raster_renderer.h"

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
    return renderVipsImage(frameIndex >= 0 && frameIndex == d->previousIndex && !d->previous.is_null() ? d->previous : d->current, bounded);
}
}
