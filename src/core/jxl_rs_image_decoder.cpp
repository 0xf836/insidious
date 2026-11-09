#include "core/jxl_rs_image_decoder.h"

#include "core/archive_collection_source.h"
#include "core/jxl_rs_bridge.h"
#include "core/render_memory_budget.h"
#include "core/vips_raster_renderer.h"

#include <QFile>
#include <QObject>
#include <algorithm>
#include <limits>
#include <memory>

namespace insidious::core {

DecodedImage decodeJxlRs(const ImageEntry &entry, const DecodeRequest &request, const QString &archivePassword)
{
    constexpr qint64 SourceByteLimit = 256LL * 1024 * 1024;
    DecodedImage result;
    result.errorReason = DecodeErrorReason::Other;
    QByteArray bytes;
    if (entry.source == ImageEntrySource::Archive) {
        auto read = readArchiveEntry(entry, archivePassword, SourceByteLimit);
        if (!read.ok()) {
            result.errorMessage = read.errorMessage;
            return result;
        }
        bytes = std::move(read.data);
    } else {
        QFile file(entry.absolutePath);
        if (!file.open(QIODevice::ReadOnly)) {
            result.errorMessage = file.errorString();
            return result;
        }
        if (file.size() > SourceByteLimit) {
            result.errorMessage = QObject::tr("JPEG XL source exceeds the memory limit.");
            return result;
        }
        bytes = file.read(SourceByteLimit + 1);
        if (bytes.size() > SourceByteLimit || file.error() != QFile::NoError) {
            result.errorMessage = QObject::tr("Could not read JPEG XL within the memory limit.");
            return result;
        }
    }

    if (request.maxDecodedBytes <= 0) {
        result.errorMessage = QObject::tr("JPEG XL raster exceeds the memory limit.");
        result.errorReason = DecodeErrorReason::RenderTooLarge;
        return result;
    }
    char error[512] = {};
    InsidiousJxl *rawHandle = nullptr;
    InsidiousJxlInfo info {};
    const size_t sampleLimit = static_cast<size_t>(std::min<quint64>(
        request.maxDecodedBytes, std::numeric_limits<size_t>::max()));
    const int status = insidious_jxl_open(reinterpret_cast<const unsigned char *>(bytes.constData()),
        static_cast<size_t>(bytes.size()), sampleLimit, &rawHandle, &info, error, sizeof(error));
    std::unique_ptr<InsidiousJxl, decltype(&insidious_jxl_close)> handle(rawHandle, insidious_jxl_close);
    if (status != 0) {
        result.errorMessage = QObject::tr("jxl-rs: %1").arg(QString::fromUtf8(error));
        result.errorReason = status == 2 ? DecodeErrorReason::RenderTooLarge : DecodeErrorReason::Other;
        return result;
    }

    // shortcut: decode the full source for every uncached render; add source reuse if the experiment is retained.
    const auto native = estimateRenderMemory(QSize(info.width, info.height), info.bands, 1.0);
    if (native.overflowed || native.bytes > request.maxDecodedBytes
        || static_cast<quint64>(native.bytes) > std::numeric_limits<size_t>::max()) {
        result.errorMessage = QObject::tr("JPEG XL raster exceeds the memory limit.");
        result.errorReason = DecodeErrorReason::RenderTooLarge;
        return result;
    }
    std::unique_ptr<void, decltype(&vips_tracked_free)> pixels(
        vips_tracked_malloc(static_cast<size_t>(native.bytes)), vips_tracked_free);
    if (!pixels) {
        result.errorMessage = QObject::tr("Decoded image memory allocation failed.");
        return result;
    }
    const int decodeStatus = insidious_jxl_decode(handle.get(), static_cast<unsigned char *>(pixels.get()),
        static_cast<size_t>(native.bytes), error, sizeof(error));
    if (decodeStatus != 0) {
        result.errorMessage = QObject::tr("jxl-rs: %1").arg(QString::fromUtf8(error));
        result.errorReason = decodeStatus == 2 ? DecodeErrorReason::RenderTooLarge : DecodeErrorReason::Other;
        return result;
    }
    auto image = vips::VImage::new_from_memory(pixels.get(), static_cast<size_t>(native.bytes),
        info.width, info.height, info.bands, VIPS_FORMAT_UCHAR);
    g_object_set_data_full(G_OBJECT(image.get_image()), "insidious-jxl-raster", pixels.release(), vips_tracked_free);
    struct InvalidateRaster {
        vips::VImage root;
        ~InvalidateRaster() { vips_image_invalidate_all(root.get_image()); }
    } invalidate { image };
    image = image.copy(vips::VImage::option()->set("interpretation",
        info.bands <= 2 ? VIPS_INTERPRETATION_B_W : VIPS_INTERPRETATION_sRGB));
    image.set(VIPS_META_ORIENTATION, info.orientation);
    if (info.iccSize != 0) {
        vips_image_set_blob_copy(image.get_image(), VIPS_META_ICC_NAME, info.icc, info.iccSize);
    }
    handle.reset();
    bytes.clear();
    result = renderVipsImage(image, request);
    result.animated = false;
    return result;
}

} // namespace insidious::core
