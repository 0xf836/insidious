#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QVector>

namespace insidious::core {

struct DecodeRequest {
    QSize viewportSize;
    double scaleFactor = 1.0;
    int rotationDegrees = 0;
    bool fitToWindow = true;
    bool exifAutorotate = true;
    qint64 maxDecodedBytes = 384LL * 1024LL * 1024LL;

    [[nodiscard]] QString cacheKeySuffix() const
    {
        return QStringLiteral("%1x%2|scale=%3|rot=%4|fit=%5|autorot=%6|max=%7")
            .arg(QString::number(viewportSize.width()),
                 QString::number(viewportSize.height()),
                 QString::number(scaleFactor, 'f', 4),
                 QString::number(rotationDegrees),
                 fitToWindow ? QStringLiteral("1") : QStringLiteral("0"),
                 exifAutorotate ? QStringLiteral("1") : QStringLiteral("0"),
                 QString::number(maxDecodedBytes));
    }
};

struct ImageMetadataField {
    QString name;
    QString value;
};

enum class DecodeErrorReason {
    None,
    RenderTooLarge,
    Other,
};

struct DecodedImage {
    QImage image;
    QSize sourceSize;
    QSize displaySize;
    QVector<ImageMetadataField> metadataFields;
    double effectiveScale = 1.0;
    QString errorMessage;
    DecodeErrorReason errorReason = DecodeErrorReason::None;

    [[nodiscard]] bool ok() const { return errorMessage.isEmpty() && !image.isNull(); }
};

} // namespace insidious::core
