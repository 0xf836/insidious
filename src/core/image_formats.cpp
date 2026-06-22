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

} // namespace insidious::core
