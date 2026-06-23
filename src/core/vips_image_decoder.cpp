#include "core/vips_image_decoder.h"

#include <QByteArray>
#include <QObject>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>
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

bool wouldExceedBudget(int width, int height, int bands, double scale, qint64 maxDecodedBytes)
{
    const double scaledWidth = std::ceil(static_cast<double>(width) * scale);
    const double scaledHeight = std::ceil(static_cast<double>(height) * scale);
    const double bytes = scaledWidth * scaledHeight * static_cast<double>(std::max(4, bands));
    return bytes > static_cast<double>(maxDecodedBytes);
}

QString vipsErrorMessage(const char *message)
{
    const QString text = QString::fromUtf8(message == nullptr ? "" : message).trimmed();
    return text.isEmpty() ? QObject::tr("Image decoding failed.") : text;
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

DecodedImage VipsImageDecoder::decode(const ImageEntry &entry, const DecodeRequest &request) const
{
    DecodedImage result;

    try {
        const QByteArray path = entry.absolutePath.toUtf8();
        vips::VImage image = vips::VImage::new_from_file(
            path.constData(),
            vips::VImage::option()->set("access", "sequential"));

        result.metadataFields = extractExifFields(image);

        image = image.autorot();
        result.sourceSize = QSize(image.width(), image.height());

        image = applyViewRotation(image, request.rotationDegrees);

        const QSize rotatedSize(image.width(), image.height());
        const double scale = request.fitToWindow
            ? fitScaleFor(rotatedSize, request.viewportSize)
            : std::max(0.01, request.scaleFactor);

        if (wouldExceedBudget(image.width(), image.height(), image.bands(), scale, request.maxDecodedBytes)) {
            result.errorMessage = QObject::tr("Image is too large to render at the requested scale.");
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
            return result;
        }

        size_t memorySize = 0;
        void *memory = image.write_to_memory(&memorySize);
        const int bytesPerLine = image.width() * bands;
        const QImage::Format format = bands == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
        const QImage view(reinterpret_cast<const uchar *>(memory), image.width(), image.height(), bytesPerLine, format);
        result.image = view.copy();
        g_free(memory);

        result.displaySize = result.image.size();
        result.effectiveScale = scale;
    } catch (const vips::VError &error) {
        result.errorMessage = vipsErrorMessage(error.what());
    } catch (const std::exception &error) {
        result.errorMessage = QString::fromUtf8(error.what());
    }

    return result;
}

} // namespace insidious::core
