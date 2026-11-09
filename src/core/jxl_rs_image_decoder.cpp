#include "core/jxl_rs_image_decoder.h"
#include "core/archive_entry_stream.h"
#include "core/render_memory_budget.h"
#include "core/vips_raster_renderer.h"

#include <QBuffer>
#include <QFile>
#include <QObject>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>
#include <libexif/exif-data.h>

namespace insidious::core {
namespace {
QVector<ImageMetadataField> exifFields(const unsigned char *bytes, size_t size)
{
    QVector<ImageMetadataField> fields;
    if (size < 4) { return fields; }
    const quint32 offset = qFromBigEndian<quint32>(bytes);
    if (offset > size - 4) { return fields; }
    QByteArray exif("Exif\0\0", 6);
    exif.append(reinterpret_cast<const char *>(bytes + 4 + offset), static_cast<qsizetype>(size - 4 - offset));
    std::unique_ptr<ExifData, decltype(&exif_data_unref)> data(
        exif_data_new_from_data(reinterpret_cast<const unsigned char *>(exif.constData()), exif.size()), exif_data_unref);
    if (!data) { return fields; }
    struct Field { ExifIfd ifd; unsigned tag; const char *label; };
    for (const auto &field : { Field{EXIF_IFD_0, 0x0110, "Camera"}, {EXIF_IFD_0, 0x010f, "Make"},
        {EXIF_IFD_EXIF, 0xa434, "Lens"}, {EXIF_IFD_EXIF, 0x9003, "Date taken"},
        {EXIF_IFD_0, 0x0132, "Date"}, {EXIF_IFD_EXIF, 0x9011, "Time offset"},
        {EXIF_IFD_EXIF, 0x9010, "Time offset"}, {EXIF_IFD_0, 0x0131, "Software"},
        {EXIF_IFD_EXIF, 0x829a, "Exposure"}, {EXIF_IFD_EXIF, 0x829d, "Aperture"},
        {EXIF_IFD_EXIF, 0x8827, "ISO"}, {EXIF_IFD_EXIF, 0x920a, "Focal length"},
        {EXIF_IFD_EXIF, 0x9204, "Exposure bias"} }) {
        auto *entry = exif_content_get_entry(data->ifd[field.ifd], static_cast<ExifTag>(field.tag));
        if (!entry) { continue; }
        char value[1024] = {};
        exif_entry_get_value(entry, value, sizeof(value));
        const QString text = QString::fromUtf8(value).trimmed();
        if (!text.isEmpty() && std::none_of(fields.begin(), fields.end(), [&](const auto &item) { return item.name == QLatin1String(field.label); })) {
            fields.append({QString::fromLatin1(field.label), text});
        }
    }
    return fields;
}
}

struct JxlRasterDecoder::Data {
    std::unique_ptr<ArchiveEntryStreamReader> archive;
    QFile file;
    QBuffer buffer;
    InsidiousJxl *handle = nullptr;
    InsidiousJxlInfo info {};
    QString error;
    DecodeErrorReason reason = DecodeErrorReason::Other;
    bool scanOnly = false;
    ~Data() { insidious_jxl_close(handle); }
    static int64_t read(void *context, unsigned char *bytes, size_t size) noexcept {
        auto *self = static_cast<Data *>(context);
        try {
            const qint64 count = std::min<size_t>(size, std::numeric_limits<qint64>::max());
            qint64 n;
            if (self->archive) { n = self->archive->read(reinterpret_cast<char *>(bytes), count); }
            else if (self->buffer.isOpen()) { n = self->buffer.read(reinterpret_cast<char *>(bytes), count); }
            else { n = self->file.read(reinterpret_cast<char *>(bytes), count); }
            if (n < 0) { self->error = self->archive ? self->archive->errorMessage() : self->file.errorString(); }
            return n;
        } catch (...) { self->error = QObject::tr("Could not read JPEG XL source."); return -1; }
    }
    bool status(int status, const char *message) {
        if (status == 0) { return true; }
        if (error.isEmpty()) { error = QObject::tr("jxl-rs: %1").arg(QString::fromUtf8(message)); }
        reason = status == 2 ? DecodeErrorReason::RenderTooLarge : DecodeErrorReason::Other;
        return false;
    }
};
JxlRasterDecoder::JxlRasterDecoder() : d(std::make_unique<Data>()) {}
JxlRasterDecoder::~JxlRasterDecoder() = default;
void JxlRasterDecoder::close() { d = std::make_unique<Data>(); }
const InsidiousJxlInfo &JxlRasterDecoder::info() const { return d->info; }
QString JxlRasterDecoder::errorMessage() const { return d->error; }
DecodeErrorReason JxlRasterDecoder::errorReason() const { return d->reason; }
bool JxlRasterDecoder::initialize(qint64 size, bool scanOnly)
{
    d->scanOnly = scanOnly;
    char error[512] = {};
    return d->status(insidious_jxl_open(Data::read, d.get(), static_cast<size_t>(std::max<qint64>(0, size)),
        scanOnly, NativeByteLimit, &d->handle, &d->info, error, sizeof(error)), error);
}
bool JxlRasterDecoder::open(const ImageEntry &entry, const QString &password, bool scanOnly)
{
    d = std::make_unique<Data>();
    if (entry.source == ImageEntrySource::Archive) {
        d->archive = std::make_unique<ArchiveEntryStreamReader>(entry, password);
        if (!d->archive->open()) { d->error = d->archive->errorMessage(); return false; }
        return initialize(entry.fileSize, scanOnly);
    }
    d->file.setFileName(entry.absolutePath);
    if (!d->file.open(QIODevice::ReadOnly)) { d->error = d->file.errorString(); return false; }
    return initialize(d->file.size(), scanOnly);
}
bool JxlRasterDecoder::openBytes(const QByteArray &bytes, bool scanOnly)
{
    d = std::make_unique<Data>();
    d->buffer.setData(bytes);
    if (!d->buffer.open(QIODevice::ReadOnly)) { d->error = d->buffer.errorString(); return false; }
    return initialize(bytes.size(), scanOnly);
}
bool JxlRasterDecoder::readFrame(vips::VImage *image, double *durationMs, bool *hasMore, qint64 nativeLimit)
{
    const auto native = estimateRenderMemory(QSize(d->info.width, d->info.height), d->info.bands, 1.0, d->info.bytesPerSample);
    if (native.overflowed || native.bytes > nativeLimit || static_cast<quint64>(native.bytes) > std::numeric_limits<size_t>::max()) {
        d->error = QObject::tr("JPEG XL source raster exceeds the memory limit.");
        d->reason = DecodeErrorReason::RenderTooLarge;
        return false;
    }
    std::unique_ptr<void, decltype(&vips_tracked_free)> pixels(nullptr, vips_tracked_free);
    if (!d->scanOnly) {
        pixels.reset(vips_tracked_malloc(native.bytes));
        if (!pixels) { d->error = QObject::tr("Decoded image memory allocation failed."); return false; }
    }
    char error[512] = {};
    int more = 0;
    if (!d->status(insidious_jxl_decode(d->handle, static_cast<unsigned char *>(pixels.get()), native.bytes,
        durationMs, &more, error, sizeof(error)), error)) { return false; }
    *hasMore = more != 0;
    if (d->scanOnly) { return true; }
    const bool transposed = d->info.orientation >= 5;
    auto raster = vips::VImage::new_from_memory(pixels.get(), native.bytes,
        transposed ? d->info.height : d->info.width, transposed ? d->info.width : d->info.height,
        d->info.bands, d->info.bytesPerSample == 1 ? VIPS_FORMAT_UCHAR : VIPS_FORMAT_USHORT);
    g_object_set_data_full(G_OBJECT(raster.get_image()), "insidious-jxl-raster", pixels.release(), vips_tracked_free);
    const auto interpretation = d->info.bands <= 2
        ? (d->info.bytesPerSample == 1 ? VIPS_INTERPRETATION_B_W : VIPS_INTERPRETATION_GREY16)
        : (d->info.bytesPerSample == 1 ? VIPS_INTERPRETATION_sRGB : VIPS_INTERPRETATION_RGB16);
    *image = raster.copy(vips::VImage::option()->set("interpretation", interpretation));
    // jxl 0.8 applies output orientation even when adjust_orientation is false; restore raw coordinates lazily.
    if (d->info.orientation != 1) {
        image->set(VIPS_META_ORIENTATION, d->info.orientation == 6 ? 8 : d->info.orientation == 8 ? 6 : d->info.orientation);
        *image = image->autorot();
    }
    image->set(VIPS_META_ORIENTATION, d->info.orientation);
    vips_image_set_blob_copy(image->get_image(), VIPS_META_ICC_NAME, d->info.icc, d->info.iccSize);
    return true;
}
bool JxlRasterDecoder::finishMetadata(QVector<ImageMetadataField> *fields)
{
    const unsigned char *bytes = nullptr;
    size_t size = 0;
    char error[512] = {};
    if (!d->status(insidious_jxl_finish(d->handle, &bytes, &size, error, sizeof(error)), error)) { return false; }
    *fields = exifFields(bytes, size);
    return true;
}

DecodedImage decodeJxlRs(const ImageEntry &entry, const DecodeRequest &request, const QString &archivePassword)
{
    JxlRasterDecoder decoder;
    vips::VImage image;
    double duration = -1;
    bool more = false;
    DecodedImage result;
    if (!decoder.open(entry, archivePassword) || !decoder.readFrame(&image, &duration, &more)) {
        result.errorMessage = decoder.errorMessage(); result.errorReason = decoder.errorReason(); return result;
    }
    struct Invalidate { vips::VImage image; ~Invalidate() { vips_image_invalidate_all(image.get_image()); } } scope {image};
    QVector<ImageMetadataField> fields;
    bool animated = false;
    if (decoder.info().animated) {
        JxlRasterDecoder scanner;
        // A usable poster survives discovery failures; playback startup reports the same error.
        animated = true;
        if (scanner.open(entry, archivePassword, true)) {
            bool scanMore = true;
            bool validTiming = true;
            bool complete = true;
            int frames = 0;
            while (scanMore) {
                double delay = -1;
                if (!scanner.readFrame(nullptr, &delay, &scanMore)) { complete = false; break; }
                validTiming = validTiming && std::isfinite(delay) && delay >= 0;
                if (++frames > 65536) { complete = false; break; }
            }
            if (complete) {
                animated = validTiming && frames > 1;
                scanner.finishMetadata(&fields);
            }
        }
    } else if (!decoder.finishMetadata(&fields)) {
        result.errorMessage = decoder.errorMessage(); result.errorReason = decoder.errorReason(); return result;
    }
    decoder.close();
    result = renderVipsImage(image, request);
    result.metadataFields = fields;
    result.animated = animated;
    return result;
}
} // namespace insidious::core
