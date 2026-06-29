#include "ui/status_line_formatter.h"

#include <QRegularExpression>
#include <QDateTime>
#include <QLocale>
#include <QObject>
#include <QSize>
#include <QStringList>

#include <cmath>

namespace insidious::ui {
namespace {

QString escaped(const QString &value)
{
    return value.toHtmlEscaped();
}

QString mutedLabel(const QString &value)
{
    return QStringLiteral("<span style=\"color:#777777;\">%1</span>").arg(escaped(value));
}

QString fieldWithLabel(const QString &label, const QString &value)
{
    return QStringLiteral("%1&nbsp;%2")
        .arg(mutedLabel(label + QLatin1Char(':')),
             escaped(value.isEmpty() ? QObject::tr("N/A") : value));
}

QString fieldSeparator()
{
    return QStringLiteral("&nbsp;&nbsp;<span style=\"color:#9a9a9a;\">|</span>&nbsp;&nbsp;");
}

QString formattedFileSize(qint64 bytes)
{
    return QLocale::system().formattedDataSize(bytes);
}

QString formattedCompactDateTime(const QDateTime &dateTime)
{
    if (!dateTime.isValid()) {
        return {};
    }

    const QDate localDate = dateTime.toLocalTime().date();
    const QTime localTime = dateTime.toLocalTime().time();
    return QStringLiteral("%1.%2.%3 %4:%5:%6")
        .arg(localDate.year())
        .arg(localDate.month())
        .arg(localDate.day())
        .arg(localTime.hour(), 2, 10, QLatin1Char('0'))
        .arg(localTime.minute(), 2, 10, QLatin1Char('0'))
        .arg(localTime.second(), 2, 10, QLatin1Char('0'));
}

QString formattedDateTime(const QDateTime &dateTime)
{
    return formattedCompactDateTime(dateTime);
}

QString formattedDimensions(const core::DecodedImage *decoded)
{
    if (decoded == nullptr || !decoded->ok() || decoded->sourceSize.isEmpty()) {
        return QObject::tr("N/A");
    }

    const QSize sourceSize = decoded->sourceSize;
    const double megapixels = static_cast<double>(sourceSize.width()) * static_cast<double>(sourceSize.height()) / 1'000'000.0;
    return QObject::tr("%1 x %2 = %3 MP")
        .arg(sourceSize.width())
        .arg(sourceSize.height())
        .arg(QLocale::system().toString(megapixels, 'f', 1));
}

QString exifDate(const core::DecodedImage *decoded)
{
    if (decoded == nullptr || !decoded->ok()) {
        return {};
    }

    for (const core::ImageMetadataField &field : decoded->metadataFields) {
        if (field.name == QLatin1String("Date taken") && !field.value.isEmpty()) {
            return field.value;
        }
    }
    for (const core::ImageMetadataField &field : decoded->metadataFields) {
        if (field.name == QLatin1String("Date") && !field.value.isEmpty()) {
            return field.value;
        }
    }
    return {};
}

QString trimmedExifDate(const QString &value)
{
    static const QRegularExpression exifDatePattern(
        QStringLiteral(R"((\d{4})[:.](\d{1,2})[:.](\d{1,2})\s(\d{2}):(\d{2}):(\d{2}))"));
    const QRegularExpressionMatch match = exifDatePattern.match(value);
    if (match.hasMatch()) {
        return QStringLiteral("%1.%2.%3 %4:%5:%6")
            .arg(match.captured(1))
            .arg(match.captured(2).toInt())
            .arg(match.captured(3).toInt())
            .arg(match.captured(4))
            .arg(match.captured(5))
            .arg(match.captured(6));
    }
    return value;
}

QString displayPath(const core::ImageEntry &entry)
{
    if (!entry.sourceRelativePath.isEmpty()) {
        return entry.sourceRelativePath;
    }
    if (!entry.displayName.isEmpty()) {
        return entry.displayName;
    }
    return entry.absolutePath;
}

QString formattedPercent(double scale)
{
    if (!std::isfinite(scale) || scale <= 0.0) {
        return QObject::tr("N/A");
    }

    const double percent = scale * 100.0;
    const double rounded = std::round(percent);
    if (std::abs(percent - rounded) < 0.05) {
        return QObject::tr("%1%").arg(QLocale::system().toString(rounded, 'f', 0));
    }
    return QObject::tr("%1%").arg(QLocale::system().toString(percent, 'f', 1));
}

QString zoomText(const core::DecodedImage *decoded, core::ZoomMode zoomMode, double zoomFactor)
{
    if (zoomMode == core::ZoomMode::FitToWindow) {
        if (decoded == nullptr || !decoded->ok()) {
            return QObject::tr("N/A");
        }
        return formattedPercent(decoded->effectiveScale);
    }
    return formattedPercent(zoomFactor);
}

QString indexText(int currentIndex, int totalCount)
{
    if (currentIndex < 0 || totalCount <= 0) {
        return QObject::tr("N/A");
    }
    return QObject::tr("%1 / %2").arg(currentIndex + 1).arg(totalCount);
}

} // namespace

QString formatStatusLine(const StatusLineContext &context)
{
    if (!context.message.isEmpty()) {
        return escaped(context.message);
    }

    if (context.entry == nullptr) {
        return escaped(QObject::tr("No image loaded."));
    }

    const core::ImageEntry &entry = *context.entry;
    const QStringList fields {
        fieldWithLabel(QObject::tr("image"), formattedDimensions(context.decoded)),
        fieldWithLabel(QObject::tr("size"), formattedFileSize(entry.fileSize)),
        fieldWithLabel(QObject::tr("modified"), formattedDateTime(entry.modifiedTime)),
        fieldWithLabel(QObject::tr("taken"), trimmedExifDate(exifDate(context.decoded))),
        fieldWithLabel(QObject::tr("name"), displayPath(entry)),
        fieldWithLabel(QObject::tr("zoom"), zoomText(context.decoded, context.zoomMode, context.zoomFactor)),
        fieldWithLabel(QObject::tr("pos"), indexText(context.currentIndex, context.totalCount)),
    };

    return fields.join(fieldSeparator());
}

} // namespace insidious::ui
