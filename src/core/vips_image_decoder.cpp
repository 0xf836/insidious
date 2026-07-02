#include "core/vips_image_decoder.h"

#include "core/archive_entry_stream.h"
#include "core/render_memory_budget.h"

#include <QImage>
#include <QObject>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include <vips/vips8>

namespace insidious::core {
namespace {

vips::VImage applyViewRotation(vips::VImage image, int rotationDegrees)
{
    switch (((rotationDegrees % 360) + 360) % 360) {
    case 90:
        return image.rot90();
    case 180:
        return image.rot180();
    case 270:
        return image.rot270();
    default:
        return image;
    }
}

vips::VImage normalizeBands(vips::VImage image)
{
    if (image.bands() == 1) {
        return vips::VImage::bandjoin(std::vector<vips::VImage> { image, image, image });
    }

    if (image.bands() == 2) {
        vips::VImage gray = image.extract_band(0);
        vips::VImage alpha = image.extract_band(1);
        return vips::VImage::bandjoin(std::vector<vips::VImage> { gray, gray, gray, alpha });
    }

    image = image.colourspace(VIPS_INTERPRETATION_sRGB);

    if (image.bands() > 4) {
        image = image.extract_band(0, vips::VImage::option()->set("n", 4));
    }

    if (image.bands() == 4 || image.bands() == 3) {
        return image;
    }

    if (image.bands() == 1) {
        return vips::VImage::bandjoin(std::vector<vips::VImage> { image, image, image });
    }

    if (image.bands() == 2) {
        vips::VImage gray = image.extract_band(0);
        vips::VImage alpha = image.extract_band(1);
        return vips::VImage::bandjoin(std::vector<vips::VImage> { gray, gray, gray, alpha });
    }

    return image.extract_band(0, vips::VImage::option()->set("n", 3));
}

VipsKernel scalingKernel(ScalingQuality quality)
{
    switch (quality) {
    case ScalingQuality::FastLinear:
        return VIPS_KERNEL_LINEAR;
    case ScalingQuality::BalancedMitchell:
        return VIPS_KERNEL_MITCHELL;
    case ScalingQuality::HighLanczos3:
        return VIPS_KERNEL_LANCZOS3;
    case ScalingQuality::SharpestMks2021:
        return VIPS_KERNEL_MKS2021;
    }
    return VIPS_KERNEL_LANCZOS3;
}

double fitScaleFor(QSize imageSize, QSize viewportSize)
{
    if (imageSize.isEmpty() || viewportSize.isEmpty()) {
        return 1.0;
    }

    const double horizontal = static_cast<double>(viewportSize.width()) / static_cast<double>(imageSize.width());
    const double vertical = static_cast<double>(viewportSize.height()) / static_cast<double>(imageSize.height());
    return std::max(0.01, std::min(horizontal, vertical));
}

double sanitizedDevicePixelRatio(double devicePixelRatio)
{
    if (!std::isfinite(devicePixelRatio) || devicePixelRatio <= 0.0) {
        return 1.0;
    }
    return std::clamp(devicePixelRatio, 0.25, 8.0);
}

QSize scaledSizeFor(QSize imageSize, double scale)
{
    if (imageSize.isEmpty() || !std::isfinite(scale) || scale <= 0.0) {
        return QSize();
    }

    return QSize(
        static_cast<int>(std::ceil(static_cast<double>(imageSize.width()) * scale)),
        static_cast<int>(std::ceil(static_cast<double>(imageSize.height()) * scale)));
}

int clampedInt(int value, int minimum, int maximum)
{
    return std::clamp(value, minimum, maximum);
}

QRect clampedDisplayRegion(QRectF region, QSize fullDisplaySize)
{
    if (fullDisplaySize.isEmpty()) {
        return QRect();
    }

    const QRectF fullRect(QPointF(0.0, 0.0), QSizeF(fullDisplaySize));
    region = region.intersected(fullRect);
    if (region.isEmpty()) {
        return QRect();
    }

    const int left = clampedInt(static_cast<int>(std::floor(region.left())), 0, fullDisplaySize.width() - 1);
    const int top = clampedInt(static_cast<int>(std::floor(region.top())), 0, fullDisplaySize.height() - 1);
    const int right = clampedInt(static_cast<int>(std::ceil(region.right())), left + 1, fullDisplaySize.width());
    const int bottom = clampedInt(static_cast<int>(std::ceil(region.bottom())), top + 1, fullDisplaySize.height());
    return QRect(left, top, right - left, bottom - top);
}

QRect regionDisplayRect(QSize fullDisplaySize, QSize viewportSize, QPointF panOffset)
{
    if (fullDisplaySize.isEmpty()) {
        return QRect();
    }
    if (viewportSize.isEmpty()) {
        return QRect(QPoint(0, 0), fullDisplaySize);
    }

    const QPointF fullTopLeft(
        (viewportSize.width() - fullDisplaySize.width()) / 2.0 + panOffset.x(),
        (viewportSize.height() - fullDisplaySize.height()) / 2.0 + panOffset.y());
    const QRectF viewportInDisplay(
        -fullTopLeft.x(),
        -fullTopLeft.y(),
        viewportSize.width(),
        viewportSize.height());
    QRect visible = clampedDisplayRegion(viewportInDisplay, fullDisplaySize);

    if (visible.isEmpty()) {
        const QPointF center = viewportInDisplay.center();
        const int x = clampedInt(static_cast<int>(std::floor(center.x())), 0, fullDisplaySize.width() - 1);
        const int y = clampedInt(static_cast<int>(std::floor(center.y())), 0, fullDisplaySize.height() - 1);
        visible = QRect(x, y, 1, 1);
    }

    const int overscanX = std::max(128, viewportSize.width() / 2);
    const int overscanY = std::max(128, viewportSize.height() / 2);
    return clampedDisplayRegion(
        QRectF(visible).adjusted(-overscanX, -overscanY, overscanX, overscanY),
        fullDisplaySize);
}

QRect sourceRegionForDisplayRegion(QRect displayRegion, double scale, QSize imageSize)
{
    if (displayRegion.isEmpty() || imageSize.isEmpty() || !std::isfinite(scale) || scale <= 0.0) {
        return QRect();
    }

    const int left = clampedInt(static_cast<int>(std::floor(displayRegion.left() / scale)), 0, imageSize.width() - 1);
    const int top = clampedInt(static_cast<int>(std::floor(displayRegion.top() / scale)), 0, imageSize.height() - 1);
    const int right = clampedInt(static_cast<int>(std::ceil((displayRegion.left() + displayRegion.width()) / scale)), left + 1, imageSize.width());
    const int bottom = clampedInt(static_cast<int>(std::ceil((displayRegion.top() + displayRegion.height()) / scale)), top + 1, imageSize.height());
    return QRect(left, top, right - left, bottom - top);
}

void freeVipsMemory(void *memory)
{
    g_free(memory);
}

QString vipsErrorMessage(const char *message)
{
    const QString text = QString::fromUtf8(message == nullptr ? "" : message).trimmed();
    return text.isEmpty() ? QObject::tr("Image decoding failed.") : text;
}

gint64 archiveSourceRead(VipsSourceCustom *, void *buffer, gint64 length, gpointer userData)
{
    auto *reader = static_cast<ArchiveEntryStreamReader *>(userData);
    if (reader == nullptr) {
        return -1;
    }

    return reader->read(static_cast<char *>(buffer), length);
}

gint64 archiveSourceSeek(VipsSourceCustom *, gint64, int, gpointer)
{
    return -1;
}

QString archiveDecodeErrorMessage(const ArchiveEntryStreamReader *reader, const char *fallback)
{
    if (reader != nullptr && !reader->errorMessage().isEmpty()) {
        return reader->errorMessage();
    }

    return vipsErrorMessage(fallback);
}

struct ExifFieldSpec {
    QString field;
    QString label;
};

QVector<ExifFieldSpec> usefulExifFields()
{
    return {
        { QStringLiteral("exif-ifd0-Model"), QStringLiteral("Camera") },
        { QStringLiteral("exif-ifd0-Make"), QStringLiteral("Make") },
        { QStringLiteral("exif-ifd2-LensModel"), QStringLiteral("Lens") },
        { QStringLiteral("exif-ifd2-DateTimeOriginal"), QStringLiteral("Date taken") },
        { QStringLiteral("exif-ifd0-DateTime"), QStringLiteral("Date") },
        { QStringLiteral("exif-ifd2-OffsetTimeOriginal"), QStringLiteral("Time offset") },
        { QStringLiteral("exif-ifd2-OffsetTime"), QStringLiteral("Time offset") },
        { QStringLiteral("exif-ifd0-Software"), QStringLiteral("Software") },
        { QStringLiteral("exif-ifd2-ExposureTime"), QStringLiteral("Exposure") },
        { QStringLiteral("exif-ifd2-FNumber"), QStringLiteral("Aperture") },
        { QStringLiteral("exif-ifd2-ISOSpeedRatings"), QStringLiteral("ISO") },
        { QStringLiteral("exif-ifd2-PhotographicSensitivity"), QStringLiteral("ISO") },
        { QStringLiteral("exif-ifd2-FocalLength"), QStringLiteral("Focal length") },
        { QStringLiteral("exif-ifd2-ExposureBiasValue"), QStringLiteral("Exposure bias") },
    };
}

QVector<ImageMetadataField> extractExifFields(const vips::VImage &image)
{
    QVector<ImageMetadataField> metadata;
    for (const ExifFieldSpec &spec : usefulExifFields()) {
        const QByteArray fieldName = spec.field.toUtf8();
        QString value;
        try {
            value = QString::fromUtf8(image.get_string(fieldName.constData())).trimmed();
        } catch (const vips::VError &) {
            continue;
        }

        if (value.isEmpty()) {
            continue;
        }

        bool duplicate = false;
        for (const ImageMetadataField &existing : metadata) {
            if (existing.name == spec.label) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            metadata.append(ImageMetadataField { spec.label, value });
        }
    }

    return metadata;
}

} // namespace

QString cacheKeyFor(const ImageEntry &entry, const DecodeRequest &request)
{
    return entry.identityKey() + QLatin1Char('|') + request.cacheKeySuffix();
}

DecodedImage VipsImageDecoder::decode(
    const ImageEntry &entry,
    const DecodeRequest &request,
    const QString &archivePassword) const
{
    DecodedImage result;
    std::unique_ptr<ArchiveEntryStreamReader> archiveStream;

    try {
        vips::VImage image;
        if (entry.source == ImageEntrySource::Archive) {
            archiveStream = std::make_unique<ArchiveEntryStreamReader>(entry, archivePassword);
            if (!archiveStream->open()) {
                result.errorMessage = archiveStream->errorMessage();
                result.errorReason = DecodeErrorReason::Other;
                return result;
            }

            VipsSourceCustom *source = vips_source_custom_new();
            g_signal_connect(source, "read", G_CALLBACK(archiveSourceRead), archiveStream.get());
            g_signal_connect(source, "seek", G_CALLBACK(archiveSourceSeek), nullptr);
            image = vips::VImage::new_from_source(
                vips::VSource(VIPS_SOURCE(source), vips::STEAL),
                "",
                vips::VImage::option()->set("access", "sequential"));
        } else {
            const QByteArray path = entry.absolutePath.toUtf8();
            image = vips::VImage::new_from_file(
                path.constData(),
                vips::VImage::option()->set("access", "sequential"));
        }

        result.metadataFields = extractExifFields(image);

        if (request.exifAutorotate) {
            image = image.autorot();
        }
        result.sourceSize = QSize(image.width(), image.height());

        image = applyViewRotation(image, request.rotationDegrees);

        const QSize rotatedSize(image.width(), image.height());
        const double devicePixelRatio = sanitizedDevicePixelRatio(request.devicePixelRatio);
        const QSize physicalViewportSize = scaledSizeFor(request.viewportSize, devicePixelRatio);
        const double physicalScale = request.fitToWindow
            ? fitScaleFor(rotatedSize, physicalViewportSize)
            : std::max(0.01, request.scaleFactor);
        const double logicalScale = physicalScale / devicePixelRatio;
        const QSize fullDisplaySize = scaledSizeFor(rotatedSize, logicalScale);
        QRect sourceRegion(QPoint(0, 0), rotatedSize);
        QPoint displayRegionOffset;
        const bool useRegionRendering = request.regionRendering && !request.fitToWindow;

        if (useRegionRendering) {
            const QRect displayRegion = regionDisplayRect(fullDisplaySize, request.viewportSize, request.panOffset);
            sourceRegion = sourceRegionForDisplayRegion(displayRegion, logicalScale, rotatedSize);
            if (sourceRegion.isEmpty()) {
                result.errorMessage = QObject::tr("Image region is empty.");
                result.errorReason = DecodeErrorReason::Other;
                return result;
            }
            displayRegionOffset = QPoint(
                static_cast<int>(std::floor(sourceRegion.left() * logicalScale)),
                static_cast<int>(std::floor(sourceRegion.top() * logicalScale)));
        }

        if (exceedsRenderMemoryBudget(
                sourceRegion.size(),
                image.bands(),
                physicalScale,
                request.maxDecodedBytes)) {
            result.errorMessage = QObject::tr("Image is too large to render at the requested scale.");
            result.errorReason = DecodeErrorReason::RenderTooLarge;
            return result;
        }

        if (useRegionRendering) {
            image = image.extract_area(
                sourceRegion.left(),
                sourceRegion.top(),
                sourceRegion.width(),
                sourceRegion.height());
        }

        if (std::abs(physicalScale - 1.0) > 0.001) {
            image = image.resize(physicalScale, vips::VImage::option()->set("kernel", scalingKernel(request.scalingQuality)));
        }

        image = normalizeBands(image);
        if (image.format() != VIPS_FORMAT_UCHAR) {
            image = image.cast(VIPS_FORMAT_UCHAR);
        }

        const int bands = image.bands();
        if (bands != 3 && bands != 4) {
            result.errorMessage = QObject::tr("Decoded image has an unsupported channel layout.");
            result.errorReason = DecodeErrorReason::Other;
            return result;
        }

        size_t memorySize = 0;
        void *memory = image.write_to_memory(&memorySize);
        if (memory == nullptr || memorySize == 0) {
            result.errorMessage = QObject::tr("Decoded image memory allocation failed.");
            result.errorReason = DecodeErrorReason::Other;
            return result;
        }

        const int bytesPerLine = image.width() * bands;
        const QImage::Format format = bands == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
        QImage view(
            reinterpret_cast<uchar *>(memory),
            image.width(),
            image.height(),
            bytesPerLine,
            format,
            freeVipsMemory,
            memory);
        if (view.isNull()) {
            g_free(memory);
            result.errorMessage = QObject::tr("Decoded image could not be wrapped for display.");
            result.errorReason = DecodeErrorReason::Other;
            return result;
        }

        view.setDevicePixelRatio(devicePixelRatio);
        result.image = view;

        result.displaySize = result.image.size();
        result.fullDisplaySize = fullDisplaySize.isEmpty() ? result.displaySize : fullDisplaySize;
        result.displayRegionOffset = displayRegionOffset;
        result.sourceRegion = sourceRegion;
        result.effectiveScale = physicalScale;
        result.devicePixelRatio = devicePixelRatio;
        result.regionBased = useRegionRendering;
    } catch (const vips::VError &error) {
        result.errorMessage = archiveDecodeErrorMessage(archiveStream.get(), error.what());
        result.errorReason = DecodeErrorReason::Other;
    } catch (const std::exception &error) {
        result.errorMessage = QString::fromUtf8(error.what());
        result.errorReason = DecodeErrorReason::Other;
    }

    return result;
}

} // namespace insidious::core
