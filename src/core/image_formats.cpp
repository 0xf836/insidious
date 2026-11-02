#include "core/image_formats.h"

#include <QFileInfo>
#include <QSet>

namespace insidious::core {
namespace {

const QSet<QString> &supportedExtensions()
{
    static const QSet<QString> extensions = {
        QStringLiteral("jpg"),
        QStringLiteral("jpeg"),
        QStringLiteral("png"),
        QStringLiteral("gif"),
        QStringLiteral("tif"),
        QStringLiteral("tiff"),
        QStringLiteral("webp"),
        QStringLiteral("bmp"),
        QStringLiteral("dib"),
        QStringLiteral("avif"),
        QStringLiteral("jxl"),
        QStringLiteral("jp2"),
        QStringLiteral("j2k"),
        QStringLiteral("heic"),
        QStringLiteral("heif"),
    };
    return extensions;
}

const QSet<QString> &supportedArchiveExtensions()
{
    static const QSet<QString> extensions = {
        QStringLiteral("zip"),
        QStringLiteral("7z"),
        QStringLiteral("rar"),
    };
    return extensions;
}

} // namespace

bool isSupportedImageExtension(QString suffix)
{
    suffix = suffix.toCaseFolded();
    if (suffix.startsWith(QLatin1Char('.'))) {
        suffix.remove(0, 1);
    }
    return supportedExtensions().contains(suffix);
}

bool isSupportedImageFile(const QFileInfo &fileInfo)
{
    return fileInfo.isFile() && isSupportedImageExtension(fileInfo.suffix());
}

bool isSupportedArchiveExtension(QString suffix)
{
    suffix = suffix.toCaseFolded();
    if (suffix.startsWith(QLatin1Char('.'))) {
        suffix.remove(0, 1);
    }
    return supportedArchiveExtensions().contains(suffix);
}

bool isSupportedArchiveFile(const QFileInfo &fileInfo)
{
    return fileInfo.isFile() && isSupportedArchiveExtension(fileInfo.suffix());
}

QString formatHintFromSuffix(QString suffix)
{
    suffix = suffix.toCaseFolded();
    if (suffix.startsWith(QLatin1Char('.'))) {
        suffix.remove(0, 1);
    }
    if (suffix == QLatin1String("jpg")) {
        return QStringLiteral("JPEG");
    }
    return suffix.toUpper();
}

QString imageMimeTypeFromPath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toCaseFolded();
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")) {
        return QStringLiteral("image/jpeg");
    }
    if (suffix == QLatin1String("png")) {
        return QStringLiteral("image/png");
    }
    if (suffix == QLatin1String("webp")) {
        return QStringLiteral("image/webp");
    }
    if (suffix == QLatin1String("bmp") || suffix == QLatin1String("dib")) {
        return QStringLiteral("image/bmp");
    }
    if (suffix == QLatin1String("avif")) {
        return QStringLiteral("image/avif");
    }
    if (suffix == QLatin1String("jxl")) {
        return QStringLiteral("image/jxl");
    }
    if (suffix == QLatin1String("jp2")) {
        return QStringLiteral("image/jp2");
    }
    if (suffix == QLatin1String("j2k")) {
        return QStringLiteral("image/x-jp2-codestream");
    }
    if (suffix == QLatin1String("heic")) {
        return QStringLiteral("image/heic");
    }
    if (suffix == QLatin1String("heif")) {
        return QStringLiteral("image/heif");
    }
    if (suffix == QLatin1String("gif")) {
        return QStringLiteral("image/gif");
    }
    if (suffix == QLatin1String("tif") || suffix == QLatin1String("tiff")) {
        return QStringLiteral("image/tiff");
    }
    return QStringLiteral("application/octet-stream");
}

QStringList supportedImageNameFilters()
{
    return {
        QStringLiteral("Images (*.jpg *.jpeg *.png *.gif *.tif *.tiff *.webp *.bmp *.dib *.avif *.jxl *.jp2 *.j2k *.heic *.heif)"),
        QStringLiteral("JPEG (*.jpg *.jpeg)"),
        QStringLiteral("PNG (*.png)"),
        QStringLiteral("GIF (*.gif)"),
        QStringLiteral("TIFF (*.tif *.tiff)"),
        QStringLiteral("WebP (*.webp)"),
        QStringLiteral("BMP (*.bmp *.dib)"),
        QStringLiteral("AVIF (*.avif)"),
        QStringLiteral("JPEG XL (*.jxl)"),
        QStringLiteral("JPEG 2000 (*.jp2 *.j2k)"),
        QStringLiteral("HEIC/HEIF (*.heic *.heif)"),
        QStringLiteral("All files (*)"),
    };
}

QStringList supportedOpenNameFilters()
{
    return {
        QStringLiteral("Images and archives (*.jpg *.jpeg *.png *.gif *.tif *.tiff *.webp *.bmp *.dib *.avif *.jxl *.jp2 *.j2k *.heic *.heif *.zip *.7z *.rar)"),
        QStringLiteral("Images (*.jpg *.jpeg *.png *.gif *.tif *.tiff *.webp *.bmp *.dib *.avif *.jxl *.jp2 *.j2k *.heic *.heif)"),
        QStringLiteral("Archives (*.zip *.7z *.rar)"),
        QStringLiteral("JPEG (*.jpg *.jpeg)"),
        QStringLiteral("PNG (*.png)"),
        QStringLiteral("GIF (*.gif)"),
        QStringLiteral("TIFF (*.tif *.tiff)"),
        QStringLiteral("WebP (*.webp)"),
        QStringLiteral("BMP (*.bmp *.dib)"),
        QStringLiteral("AVIF (*.avif)"),
        QStringLiteral("JPEG XL (*.jxl)"),
        QStringLiteral("JPEG 2000 (*.jp2 *.j2k)"),
        QStringLiteral("HEIC/HEIF (*.heic *.heif)"),
        QStringLiteral("All files (*)"),
    };
}

} // namespace insidious::core
