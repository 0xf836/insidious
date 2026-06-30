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

double fitScaleFor(QSize imageSize, QSize viewportSize)
{
    if (imageSize.isEmpty() || viewportSize.isEmpty()) {
        return 1.0;
    }

    const double horizontal = static_cast<double>(viewportSize.width()) / static_cast<double>(imageSize.width());
    const double vertical = static_cast<double>(viewportSize.height()) / static_cast<double>(imageSize.height());
    return std::max(0.01, std::min(horizontal, vertical));
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
        const double scale = request.fitToWindow
            ? fitScaleFor(rotatedSize, request.viewportSize)
            : std::max(0.01, request.scaleFactor);

        if (exceedsRenderMemoryBudget(
                QSize(image.width(), image.height()),
                image.bands(),
                scale,
                request.maxDecodedBytes)) {
            result.errorMessage = QObject::tr("Image is too large to render at the requested scale.");
            result.errorReason = DecodeErrorReason::RenderTooLarge;
            return result;
        }

        if (std::abs(scale - 1.0) > 0.001) {
            image = image.resize(scale, vips::VImage::option()->set("kernel", VIPS_KERNEL_LANCZOS3));
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

        result.image = view;

        result.displaySize = result.image.size();
        result.effectiveScale = scale;
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
