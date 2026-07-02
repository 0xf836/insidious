#pragma once

#include "core/app_settings.h"

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

namespace insidious::core {

struct DecodeRequest {
    QSize viewportSize;
    QPointF panOffset;
    double scaleFactor = 1.0;
    int rotationDegrees = 0;
    bool fitToWindow = true;
    bool regionRendering = false;
    bool exifAutorotate = true;
    ScalingQuality scalingQuality = ScalingQuality::HighLanczos3;
    qint64 maxDecodedBytes = 384LL * 1024LL * 1024LL;

    [[nodiscard]] QString cacheKeySuffix() const
    {
        const QPointF cachePanOffset = regionRendering ? panOffset : QPointF();
        return QStringLiteral("%1x%2|pan=%3,%4|scale=%5|rot=%6|fit=%7|region=%8|autorot=%9|quality=%10|max=%11")
            .arg(QString::number(viewportSize.width()),
                 QString::number(viewportSize.height()),
                 QString::number(cachePanOffset.x(), 'f', 2),
                 QString::number(cachePanOffset.y(), 'f', 2),
                 QString::number(scaleFactor, 'f', 4),
                 QString::number(rotationDegrees),
                 fitToWindow ? QStringLiteral("1") : QStringLiteral("0"),
                 regionRendering ? QStringLiteral("1") : QStringLiteral("0"),
                 exifAutorotate ? QStringLiteral("1") : QStringLiteral("0"),
                 scalingQualityToSettingsString(scalingQuality),
                 QString::number(maxDecodedBytes));
    }
};

[[nodiscard]] inline bool shouldPrefetchDecodeRequest(const DecodeRequest &request)
{
    return request.fitToWindow
        && request.viewportSize.width() > 0
        && request.viewportSize.height() > 0;
}

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
    QSize fullDisplaySize;
    QPoint displayRegionOffset;
    QRect sourceRegion;
    QVector<ImageMetadataField> metadataFields;
    double effectiveScale = 1.0;
    bool regionBased = false;
    QString errorMessage;
    DecodeErrorReason errorReason = DecodeErrorReason::None;

    [[nodiscard]] bool ok() const { return errorMessage.isEmpty() && !image.isNull(); }
};

} // namespace insidious::core
