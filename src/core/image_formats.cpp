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
        QStringLiteral("webp"),
        QStringLiteral("bmp"),
        QStringLiteral("dib"),
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
    return QStringLiteral("application/octet-stream");
}

QStringList supportedImageNameFilters()
{
    return {
        QStringLiteral("Images (*.jpg *.jpeg *.png *.webp *.bmp *.dib)"),
        QStringLiteral("JPEG (*.jpg *.jpeg)"),
        QStringLiteral("PNG (*.png)"),
        QStringLiteral("WebP (*.webp)"),
        QStringLiteral("BMP (*.bmp *.dib)"),
        QStringLiteral("All files (*)"),
    };
}

QStringList supportedOpenNameFilters()
{
    return {
        QStringLiteral("Images and archives (*.jpg *.jpeg *.png *.webp *.bmp *.dib *.zip *.7z *.rar)"),
        QStringLiteral("Images (*.jpg *.jpeg *.png *.webp *.bmp *.dib)"),
        QStringLiteral("Archives (*.zip *.7z *.rar)"),
        QStringLiteral("JPEG (*.jpg *.jpeg)"),
        QStringLiteral("PNG (*.png)"),
        QStringLiteral("WebP (*.webp)"),
        QStringLiteral("BMP (*.bmp *.dib)"),
        QStringLiteral("All files (*)"),
    };
}

} // namespace insidious::core
