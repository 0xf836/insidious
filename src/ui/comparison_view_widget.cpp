#include "ui/comparison_view_widget.h"

#include "core/vips_image_decoder.h"

#include <QApplication>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedLayout>
#include <QStyle>
#include <QVariant>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace insidious::ui {
namespace {

constexpr int TileMinimumWidth = 24;
constexpr int TileMargin = 10;
constexpr int TileTitleHeight = 28;
constexpr int TileSpacing = 10;
constexpr qint64 ComparisonCacheBudgetBytes = 384LL * 1024LL * 1024LL;

QString itemTitle(const ComparisonSessionItem &item)
{
    return item.entry.displayName.isEmpty() ? item.entry.sourceRelativePath : item.entry.displayName;
}

double sanitizedDevicePixelRatio(double dpr)
{
    if (!std::isfinite(dpr) || dpr <= 0.0) {
        return 1.0;
    }
    return std::clamp(dpr, 0.25, 8.0);
}

QString comparisonModeName(ComparisonMode mode)
{
    switch (mode) {
    case ComparisonMode::Row:
        return QObject::tr("Row");
    case ComparisonMode::Grid:
        return QObject::tr("Grid");
    case ComparisonMode::Carousel:
        return QObject::tr("Carousel");
    case ComparisonMode::HoverSwitch:
        return QObject::tr("Hover Switch");
    }
    return QObject::tr("Row");
}

QString comparisonAlignmentPresetName(ComparisonAlignmentPreset preset)
{
    switch (preset) {
    case ComparisonAlignmentPreset::FitCenter:
        return QObject::tr("Fit Center");
    case ComparisonAlignmentPreset::TopLeft:
        return QObject::tr("Top Left");
    case ComparisonAlignmentPreset::TopCenter:
        return QObject::tr("Top Center");
    case ComparisonAlignmentPreset::CenterLeft:
        return QObject::tr("Center Left");
    case ComparisonAlignmentPreset::BottomLeft:
        return QObject::tr("Bottom Left");
    case ComparisonAlignmentPreset::BottomCenter:
        return QObject::tr("Bottom Center");
    case ComparisonAlignmentPreset::SameSourceScale:
        return QObject::tr("Same Source Scale");
    case ComparisonAlignmentPreset::CoverCenter:
        return QObject::tr("Cover Center");
    }
    return QObject::tr("Fit Center");
}

QPointF alignmentAnchorFactors(ComparisonAlignmentPreset preset)
{
    switch (preset) {
    case ComparisonAlignmentPreset::TopLeft:
        return QPointF(0.0, 0.0);
    case ComparisonAlignmentPreset::TopCenter:
        return QPointF(0.5, 0.0);
    case ComparisonAlignmentPreset::CenterLeft:
        return QPointF(0.0, 0.5);
    case ComparisonAlignmentPreset::BottomLeft:
        return QPointF(0.0, 1.0);
    case ComparisonAlignmentPreset::BottomCenter:
        return QPointF(0.5, 1.0);
    case ComparisonAlignmentPreset::FitCenter:
    case ComparisonAlignmentPreset::SameSourceScale:
    case ComparisonAlignmentPreset::CoverCenter:
        return QPointF(0.5, 0.5);
    }
    return QPointF(0.5, 0.5);
}

QSizeF decodedLogicalSize(const core::DecodedImage &decoded)
{
    if (!decoded.ok()) {
        return QSizeF();
    }
    const QSizeF size = decoded.image.deviceIndependentSize();
    return QSizeF(std::ceil(size.width()), std::ceil(size.height()));
}

double decodedSourceLogicalScale(const core::DecodedImage &decoded)
{
    const QSizeF logicalSize = decodedLogicalSize(decoded);
    if (!decoded.ok() || logicalSize.isEmpty() || decoded.sourceSize.isEmpty()) {
        return 0.0;
    }
    return std::min(
        logicalSize.width() / static_cast<double>(decoded.sourceSize.width()),
        logicalSize.height() / static_cast<double>(decoded.sourceSize.height()));
}

QSizeF baseDrawSize(
    const core::DecodedImage &decoded,
    const QRectF &imageArea,
    ComparisonAlignmentPreset preset,
    double sharedSourceScale)
{
    QSizeF decodedSize = decodedLogicalSize(decoded);
    if (decodedSize.isEmpty()) {
        return QSizeF();
    }

    if (preset == ComparisonAlignmentPreset::SameSourceScale
        && sharedSourceScale > 0.0
        && !decoded.sourceSize.isEmpty()) {
        return QSizeF(
            static_cast<double>(decoded.sourceSize.width()) * sharedSourceScale,
            static_cast<double>(decoded.sourceSize.height()) * sharedSourceScale);
    } else if (preset == ComparisonAlignmentPreset::CoverCenter) {
        QSizeF size = decoded.sourceSize.isEmpty()
            ? decodedSize
            : QSizeF(decoded.sourceSize);
        const double coverScale = std::max(
            imageArea.width() / size.width(),
            imageArea.height() / size.height());
        if (std::isfinite(coverScale) && coverScale > 0.0) {
            return size * coverScale;
        }
        return decodedSize;
    } else if (!decoded.sourceSize.isEmpty()) {
        QSizeF size(decoded.sourceSize);
        const double fitScale = std::min(
            imageArea.width() / size.width(),
            imageArea.height() / size.height());
        if (std::isfinite(fitScale) && fitScale > 0.0) {
            return size * fitScale;
        }
        return decodedSize;
    }
    return decodedSize;
}

QRectF alignedImageRect(
    const QRectF &imageArea,
    const core::DecodedImage &decoded,
    ComparisonAlignmentPreset preset,
    double sharedSourceScale,
    double zoomFactor,
    QPointF panOffset)
{
    const QSizeF baseSize = baseDrawSize(decoded, imageArea, preset, sharedSourceScale);
    if (imageArea.isEmpty() || baseSize.isEmpty()) {
        return QRectF();
    }

    const QSizeF drawSize(baseSize.width() * zoomFactor, baseSize.height() * zoomFactor);
    const QPointF anchor = alignmentAnchorFactors(preset);
    return QRectF(
        imageArea.x() + (imageArea.width() - drawSize.width()) * anchor.x() + panOffset.x(),
        imageArea.y() + (imageArea.height() - drawSize.height()) * anchor.y() + panOffset.y(),
        drawSize.width(),
        drawSize.height());
}

QSize coverDecodeTargetSize(QSize sourceSize, QSize baseTargetSize)
{
    if (sourceSize.isEmpty() || baseTargetSize.isEmpty()) {
        return baseTargetSize;
    }
    const double coverScale = std::max(
        static_cast<double>(baseTargetSize.width()) / static_cast<double>(sourceSize.width()),
        static_cast<double>(baseTargetSize.height()) / static_cast<double>(sourceSize.height()));
    if (!std::isfinite(coverScale) || coverScale <= 0.0) {
        return baseTargetSize;
    }
    return QSize(
        std::max(1, static_cast<int>(std::ceil(static_cast<double>(sourceSize.width()) * coverScale))),
        std::max(1, static_cast<int>(std::ceil(static_cast<double>(sourceSize.height()) * coverScale))));
}

} // namespace

ComparisonTileWidget::ComparisonTileWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(TileMinimumWidth, 160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void ComparisonTileWidget::setIndex(int index)
{
    m_index = index;
}

void ComparisonTileWidget::setTitle(const QString &title)
{
    m_title = title;
    update();
}

void ComparisonTileWidget::setMessage(const QString &message)
{
    m_message = message;
    update();
}

void ComparisonTileWidget::setDecodedImage(const core::DecodedImage &decoded)
{
    const bool preserveView = m_decoded.ok() && decoded.ok();
    QPointF normalizedCenter(0.5, 0.5);
    if (preserveView) {
        const QRectF area = rect().adjusted(TileMargin, TileTitleHeight, -TileMargin, -TileMargin);
        const QRectF oldRect = imageRect();
        if (!area.isEmpty() && !oldRect.isEmpty() && m_zoomFactor > 0.0) {
            const QPointF imagePoint = area.center() - oldRect.topLeft();
            normalizedCenter = QPointF(
                imagePoint.x() / oldRect.width(),
                imagePoint.y() / oldRect.height());
        }
    }

    m_decoded = decoded;
    m_message.clear();
    if (!preserveView) {
        resetView();
        return;
    }

    const QRectF area = rect().adjusted(TileMargin, TileTitleHeight, -TileMargin, -TileMargin);
    if (!area.isEmpty()) {
        m_panOffset = QPointF();
        const QRectF defaultRect = imageRect();
        if (defaultRect.isEmpty()) {
            update();
            return;
        }
        const QPointF desiredTopLeft = area.center() - QPointF(
            normalizedCenter.x() * defaultRect.width(),
            normalizedCenter.y() * defaultRect.height());
        m_panOffset = desiredTopLeft - defaultRect.topLeft();
    }
    update();
}

void ComparisonTileWidget::setAlignmentPreset(ComparisonAlignmentPreset preset)
{
    if (m_alignmentPreset == preset) {
        return;
    }
    m_alignmentPreset = preset;
    update();
}

void ComparisonTileWidget::setSharedSourceScale(double scale)
{
    if (qFuzzyCompare(m_sharedSourceScale, scale)) {
        return;
    }
    m_sharedSourceScale = scale;
    update();
}

void ComparisonTileWidget::clearImage()
{
    m_decoded = {};
    resetView();
    update();
}

void ComparisonTileWidget::resetView()
{
    m_zoomFactor = 1.0;
    m_panOffset = QPointF();
    m_panning = false;
    m_reorderDragging = false;
    update();
}

double ComparisonTileWidget::sourceLogicalScale() const
{
    return decodedSourceLogicalScale(m_decoded);
}

void ComparisonTileWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));

    const QRect border = rect().adjusted(0, 0, -1, -1);
    painter.setPen(QPen(palette().color(hasFocus() ? QPalette::Highlight : QPalette::Mid), 1));
    painter.drawRect(border);

    const QRect titleRect(TileMargin, 0, width() - TileMargin * 2, TileTitleHeight);
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(
        titleRect,
        Qt::AlignVCenter | Qt::AlignLeft,
        painter.fontMetrics().elidedText(m_title, Qt::ElideRight, titleRect.width()));

    const QRect imageArea = rect().adjusted(TileMargin, TileTitleHeight, -TileMargin, -TileMargin);
    if (!m_decoded.ok()) {
        painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        painter.drawText(imageArea, Qt::AlignCenter | Qt::TextWordWrap, m_message);
        return;
    }

    painter.save();
    painter.setClipRect(imageArea);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(imageRect(), m_decoded.image);
    painter.restore();
}

void ComparisonTileWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_decoded.ok()) {
        event->ignore();
        return;
    }

    const double multiplier = event->angleDelta().y() > 0 ? 1.25 : 0.8;
    m_zoomFactor = std::clamp(m_zoomFactor * multiplier, 1.0, 16.0);
    if (qFuzzyCompare(m_zoomFactor, 1.0)) {
        m_panOffset = QPointF();
    }
    update();
    event->accept();
}

void ComparisonTileWidget::mousePressEvent(QMouseEvent *event)
{
    emit focused(m_index);
    setFocus(Qt::MouseFocusReason);
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_pressPosition = event->pos();
    m_lastMousePosition = event->pos();
    if (m_decoded.ok() && m_zoomFactor > 1.0) {
        m_panning = true;
    } else {
        m_reorderDragging = true;
    }
    event->accept();
}

void ComparisonTileWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastMousePosition;
        m_lastMousePosition = event->pos();
        m_panOffset += QPointF(delta);
        update();
        event->accept();
        return;
    }

    if (m_reorderDragging && (event->pos() - m_pressPosition).manhattanLength() > QApplication::startDragDistance()) {
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void ComparisonTileWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_reorderDragging) {
        unsetCursor();
        m_reorderDragging = false;
        if ((event->pos() - m_pressPosition).manhattanLength() > QApplication::startDragDistance()) {
            emit reorderDropRequested(m_index, event->globalPosition().toPoint());
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void ComparisonTileWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_panning = false;
        m_reorderDragging = false;
        unsetCursor();
        emit toggleFullscreenRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

QSizeF ComparisonTileWidget::imageLogicalSize() const
{
    return decodedLogicalSize(m_decoded);
}

QRectF ComparisonTileWidget::imageRect() const
{
    const QRectF imageArea = rect().adjusted(TileMargin, TileTitleHeight, -TileMargin, -TileMargin);
    return alignedImageRect(
        imageArea,
        m_decoded,
        m_alignmentPreset,
        m_sharedSourceScale,
        m_zoomFactor,
        m_panOffset);
}

ComparisonHoverSwitchWidget::ComparisonHoverSwitchWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(TileMinimumWidth, 160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void ComparisonHoverSwitchWidget::setTitles(const QString &firstTitle, const QString &secondTitle)
{
    m_titles[0] = firstTitle;
    m_titles[1] = secondTitle;
    update();
}

void ComparisonHoverSwitchWidget::setMessage(int index, const QString &message)
{
    if (index < 0 || index > 1) {
        return;
    }
    m_messages[index] = message;
    update();
}

void ComparisonHoverSwitchWidget::setDecodedImage(int index, const core::DecodedImage &decoded)
{
    if (index < 0 || index > 1) {
        return;
    }

    const bool preserveView = m_decoded[index].ok() && decoded.ok();
    m_decoded[index] = decoded;
    m_messages[index].clear();
    if (!preserveView && !m_decoded[1 - index].ok()) {
        resetView();
        return;
    }
    if (m_cursorInside && !m_panning) {
        updateHoverState(m_lastHoverPosition);
    }
    update();
}

void ComparisonHoverSwitchWidget::setAlignmentPreset(ComparisonAlignmentPreset preset)
{
    if (m_alignmentPreset == preset) {
        return;
    }
    m_alignmentPreset = preset;
    if (m_cursorInside && !m_panning) {
        updateHoverState(m_lastHoverPosition);
    }
    update();
}

void ComparisonHoverSwitchWidget::setSharedSourceScale(double scale)
{
    if (qFuzzyCompare(m_sharedSourceScale, scale)) {
        return;
    }
    m_sharedSourceScale = scale;
    if (m_cursorInside && !m_panning) {
        updateHoverState(m_lastHoverPosition);
    }
    update();
}

void ComparisonHoverSwitchWidget::clearImages()
{
    m_decoded[0] = {};
    m_decoded[1] = {};
    m_messages[0].clear();
    m_messages[1].clear();
    resetView();
}

void ComparisonHoverSwitchWidget::resetView()
{
    m_zoomFactor = 1.0;
    m_panOffset = QPointF();
    m_panning = false;
    if (m_cursorInside) {
        updateHoverState(m_lastHoverPosition);
    } else {
        m_hovered = false;
    }
    update();
}

QSize ComparisonHoverSwitchWidget::targetImageSize() const
{
    const QSize area = rect()
            .adjusted(TileMargin, TileTitleHeight, -TileMargin, -TileMargin)
            .size();
    return QSize(std::max(1, area.width()), std::max(1, area.height()));
}

double ComparisonHoverSwitchWidget::sourceLogicalScale(int index) const
{
    if (index < 0 || index > 1) {
        return 0.0;
    }
    return decodedSourceLogicalScale(m_decoded[index]);
}

void ComparisonHoverSwitchWidget::enterEvent(QEnterEvent *event)
{
    m_cursorInside = true;
    m_lastHoverPosition = event->position();
    updateHoverState(m_lastHoverPosition);
    QWidget::enterEvent(event);
}

void ComparisonHoverSwitchWidget::leaveEvent(QEvent *event)
{
    m_cursorInside = false;
    m_hovered = false;
    m_panning = false;
    update();
    QWidget::leaveEvent(event);
}

void ComparisonHoverSwitchWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));

    const QRect border = rect().adjusted(0, 0, -1, -1);
    painter.setPen(QPen(palette().color(hasFocus() ? QPalette::Highlight : QPalette::Mid), 1));
    painter.drawRect(border);

    const int index = activeIndex();
    const QRect titleRect(TileMargin, 0, width() - TileMargin * 2, TileTitleHeight);
    const QString prefix = index == 0 ? tr("Base") : tr("Hover");
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(
        titleRect,
        Qt::AlignVCenter | Qt::AlignLeft,
        painter.fontMetrics().elidedText(
            tr("%1: %2").arg(prefix, m_titles[index]),
            Qt::ElideRight,
            titleRect.width()));

    const QRect imageArea = rect().adjusted(TileMargin, TileTitleHeight, -TileMargin, -TileMargin);
    if (!m_decoded[index].ok()) {
        painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        painter.drawText(imageArea, Qt::AlignCenter | Qt::TextWordWrap, m_messages[index]);
        return;
    }

    painter.save();
    painter.setClipRect(imageArea);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(imageRect(index), m_decoded[index].image);
    painter.restore();
}

void ComparisonHoverSwitchWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_cursorInside && !m_panning) {
        updateHoverState(m_lastHoverPosition);
    }
}

void ComparisonHoverSwitchWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_decoded[activeIndex()].ok()) {
        event->ignore();
        return;
    }

    const double multiplier = event->angleDelta().y() > 0 ? 1.25 : 0.8;
    m_zoomFactor = std::clamp(m_zoomFactor * multiplier, 1.0, 16.0);
    if (qFuzzyCompare(m_zoomFactor, 1.0)) {
        m_panOffset = QPointF();
    }
    if (m_cursorInside && !m_panning) {
        updateHoverState(m_lastHoverPosition);
    }
    update();
    event->accept();
}

void ComparisonHoverSwitchWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_lastMousePosition = event->pos();
    m_lastHoverPosition = event->position();
    m_cursorInside = true;
    if (!m_panning) {
        updateHoverState(m_lastHoverPosition);
    }
    if (m_decoded[activeIndex()].ok() && m_zoomFactor > 1.0) {
        m_panning = true;
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ComparisonHoverSwitchWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_lastHoverPosition = event->position();
    m_cursorInside = true;
    if (!m_panning) {
        updateHoverState(m_lastHoverPosition);
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint delta = event->pos() - m_lastMousePosition;
    m_lastMousePosition = event->pos();
    m_panOffset += QPointF(delta);
    update();
    event->accept();
}

void ComparisonHoverSwitchWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        m_lastHoverPosition = event->position();
        m_cursorInside = true;
        updateHoverState(m_lastHoverPosition);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ComparisonHoverSwitchWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_panning = false;
        emit toggleFullscreenRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

int ComparisonHoverSwitchWidget::activeIndex() const
{
    return m_hovered ? 1 : 0;
}

QRectF ComparisonHoverSwitchWidget::imageAreaRect() const
{
    return QRectF(rect().adjusted(TileMargin, TileTitleHeight, -TileMargin, -TileMargin));
}

QRectF ComparisonHoverSwitchWidget::hoverSwitchRect() const
{
    const QRectF baseRect = imageRect(0);
    if (baseRect.isEmpty()) {
        return QRectF();
    }
    return baseRect.intersected(imageAreaRect());
}

void ComparisonHoverSwitchWidget::updateHoverState(QPointF position)
{
    const bool hovered = m_cursorInside && hoverSwitchRect().contains(position);
    if (m_hovered == hovered) {
        return;
    }
    m_hovered = hovered;
    update();
}

QSizeF ComparisonHoverSwitchWidget::imageLogicalSize(int index) const
{
    if (index < 0 || index > 1 || !m_decoded[index].ok()) {
        return QSizeF();
    }
    return decodedLogicalSize(m_decoded[index]);
}

QRectF ComparisonHoverSwitchWidget::imageRect(int index) const
{
    const QRectF imageArea = imageAreaRect();
    if (index < 0 || index > 1) {
        return QRectF();
    }
    return alignedImageRect(
        imageArea,
        m_decoded[index],
        m_alignmentPreset,
        m_sharedSourceScale,
        m_zoomFactor,
        m_panOffset);
}

ComparisonCarouselWidget::ComparisonCarouselWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(TileMinimumWidth, 160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void ComparisonCarouselWidget::setIndexInfo(int index, int count)
{
    m_index = std::max(0, index);
    m_count = std::max(0, count);
    update();
}

void ComparisonCarouselWidget::setTitle(const QString &title)
{
    m_title = title;
    update();
}

void ComparisonCarouselWidget::setMessage(const QString &message)
{
    m_message = message;
    m_decoded = {};
    update();
}

void ComparisonCarouselWidget::setPendingMessage(const QString &message)
{
    m_message = message;
    update();
}

void ComparisonCarouselWidget::setDecodedImage(const core::DecodedImage &decoded)
{
    m_decoded = decoded;
    m_message.clear();
    update();
}

void ComparisonCarouselWidget::setAlignmentPreset(ComparisonAlignmentPreset preset)
{
    if (m_alignmentPreset == preset) {
        return;
    }
    m_alignmentPreset = preset;
    update();
}

void ComparisonCarouselWidget::setSharedSourceScale(double scale)
{
    if (qFuzzyCompare(m_sharedSourceScale, scale)) {
        return;
    }
    m_sharedSourceScale = scale;
    update();
}

void ComparisonCarouselWidget::clearImage()
{
    m_decoded = {};
    m_message.clear();
    resetView();
}

void ComparisonCarouselWidget::resetView()
{
    m_zoomFactor = 1.0;
    m_panOffset = QPointF();
    m_panning = false;
    update();
}

QSize ComparisonCarouselWidget::targetImageSize() const
{
    const QSize area = rect()
            .adjusted(TileMargin, TileTitleHeight, -TileMargin, -TileMargin)
            .size();
    return QSize(std::max(1, area.width()), std::max(1, area.height()));
}

double ComparisonCarouselWidget::sourceLogicalScale() const
{
    return decodedSourceLogicalScale(m_decoded);
}

void ComparisonCarouselWidget::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_PageUp:
        emit previousRequested();
        event->accept();
        return;
    case Qt::Key_Right:
    case Qt::Key_PageDown:
        emit nextRequested();
        event->accept();
        return;
    case Qt::Key_Home:
        emit firstRequested();
        event->accept();
        return;
    case Qt::Key_End:
        emit lastRequested();
        event->accept();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void ComparisonCarouselWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));

    const QRect border = rect().adjusted(0, 0, -1, -1);
    painter.setPen(QPen(palette().color(hasFocus() ? QPalette::Highlight : QPalette::Mid), 1));
    painter.drawRect(border);

    const QRect titleRect(TileMargin, 0, width() - TileMargin * 2, TileTitleHeight);
    const QString indexText = m_count > 0
            ? tr("%1/%2: %3").arg(m_index + 1).arg(m_count).arg(m_title)
            : m_title;
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(
        titleRect,
        Qt::AlignVCenter | Qt::AlignLeft,
        painter.fontMetrics().elidedText(indexText, Qt::ElideRight, titleRect.width()));

    const QRectF area = imageAreaRect();
    if (!m_decoded.ok()) {
        painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        painter.drawText(area.toRect(), Qt::AlignCenter | Qt::TextWordWrap, m_message);
        return;
    }

    painter.save();
    painter.setClipRect(area);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(imageRect(), m_decoded.image);
    painter.restore();
}

void ComparisonCarouselWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
}

void ComparisonCarouselWidget::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        if (!m_decoded.ok()) {
            event->ignore();
            return;
        }
        const double multiplier = event->angleDelta().y() > 0 ? 1.25 : 0.8;
        m_zoomFactor = std::clamp(m_zoomFactor * multiplier, 1.0, 16.0);
        if (qFuzzyCompare(m_zoomFactor, 1.0)) {
            m_panOffset = QPointF();
        }
        update();
        event->accept();
        return;
    }

    if (event->angleDelta().y() > 0) {
        emit previousRequested();
    } else if (event->angleDelta().y() < 0) {
        emit nextRequested();
    }
    event->accept();
}

void ComparisonCarouselWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_lastMousePosition = event->pos();
    if (m_decoded.ok() && m_zoomFactor > 1.0) {
        m_panning = true;
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ComparisonCarouselWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_panning) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint delta = event->pos() - m_lastMousePosition;
    m_lastMousePosition = event->pos();
    m_panOffset += QPointF(delta);
    update();
    event->accept();
}

void ComparisonCarouselWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ComparisonCarouselWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_panning = false;
        emit toggleFullscreenRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

QRectF ComparisonCarouselWidget::imageAreaRect() const
{
    return QRectF(rect().adjusted(TileMargin, TileTitleHeight, -TileMargin, -TileMargin));
}

QRectF ComparisonCarouselWidget::imageRect() const
{
    return alignedImageRect(
        imageAreaRect(),
        m_decoded,
        m_alignmentPreset,
        m_sharedSourceScale,
        m_zoomFactor,
        m_panOffset);
}

ComparisonViewWidget::ComparisonViewWidget(QWidget *parent)
    : QWidget(parent)
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(8, 8, 8, 8);
    m_mainLayout->setSpacing(8);

    m_headerWidget = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    m_headerLabel = new QLabel(tr("No comparison session"), m_headerWidget);
    headerLayout->addWidget(m_headerLabel, 1);

    m_modeComboBox = new QComboBox(m_headerWidget);
    m_modeComboBox->addItem(comparisonModeName(ComparisonMode::Row), static_cast<int>(ComparisonMode::Row));
    m_modeComboBox->addItem(comparisonModeName(ComparisonMode::Grid), static_cast<int>(ComparisonMode::Grid));
    m_modeComboBox->addItem(comparisonModeName(ComparisonMode::Carousel), static_cast<int>(ComparisonMode::Carousel));
    m_modeComboBox->addItem(comparisonModeName(ComparisonMode::HoverSwitch), static_cast<int>(ComparisonMode::HoverSwitch));
    m_modeComboBox->setEnabled(false);
    connect(m_modeComboBox, &QComboBox::currentIndexChanged, this, [this](int index) {
        const ComparisonMode requestedMode = static_cast<ComparisonMode>(m_modeComboBox->itemData(index).toInt());
        if (!setMode(requestedMode)) {
            const QSignalBlocker blocker(m_modeComboBox);
            m_modeComboBox->setCurrentIndex(m_modeComboBox->findData(static_cast<int>(m_mode)));
        }
    });
    headerLayout->addWidget(m_modeComboBox);

    m_carouselPreviousButton = new QPushButton(tr("Previous"), m_headerWidget);
    m_carouselPreviousButton->setVisible(false);
    connect(m_carouselPreviousButton, &QPushButton::clicked, this, [this]() {
        moveCarouselIndex(-1);
    });
    headerLayout->addWidget(m_carouselPreviousButton);

    m_carouselNextButton = new QPushButton(tr("Next"), m_headerWidget);
    m_carouselNextButton->setVisible(false);
    connect(m_carouselNextButton, &QPushButton::clicked, this, [this]() {
        moveCarouselIndex(1);
    });
    headerLayout->addWidget(m_carouselNextButton);

    m_alignmentComboBox = new QComboBox(m_headerWidget);
    m_alignmentComboBox->addItem(comparisonAlignmentPresetName(ComparisonAlignmentPreset::FitCenter), static_cast<int>(ComparisonAlignmentPreset::FitCenter));
    m_alignmentComboBox->addItem(comparisonAlignmentPresetName(ComparisonAlignmentPreset::TopLeft), static_cast<int>(ComparisonAlignmentPreset::TopLeft));
    m_alignmentComboBox->addItem(comparisonAlignmentPresetName(ComparisonAlignmentPreset::TopCenter), static_cast<int>(ComparisonAlignmentPreset::TopCenter));
    m_alignmentComboBox->addItem(comparisonAlignmentPresetName(ComparisonAlignmentPreset::CenterLeft), static_cast<int>(ComparisonAlignmentPreset::CenterLeft));
    m_alignmentComboBox->addItem(comparisonAlignmentPresetName(ComparisonAlignmentPreset::BottomLeft), static_cast<int>(ComparisonAlignmentPreset::BottomLeft));
    m_alignmentComboBox->addItem(comparisonAlignmentPresetName(ComparisonAlignmentPreset::BottomCenter), static_cast<int>(ComparisonAlignmentPreset::BottomCenter));
    m_alignmentComboBox->addItem(comparisonAlignmentPresetName(ComparisonAlignmentPreset::SameSourceScale), static_cast<int>(ComparisonAlignmentPreset::SameSourceScale));
    m_alignmentComboBox->addItem(comparisonAlignmentPresetName(ComparisonAlignmentPreset::CoverCenter), static_cast<int>(ComparisonAlignmentPreset::CoverCenter));
    m_alignmentComboBox->setEnabled(false);
    connect(m_alignmentComboBox, &QComboBox::currentIndexChanged, this, [this](int index) {
        setAlignmentPreset(static_cast<ComparisonAlignmentPreset>(m_alignmentComboBox->itemData(index).toInt()));
    });
    headerLayout->addWidget(m_alignmentComboBox);

    auto *closeButton = new QPushButton(tr("Close Compare"), m_headerWidget);
    connect(closeButton, &QPushButton::clicked, this, [this]() {
        clearSession();
        emit closeRequested();
    });
    headerLayout->addWidget(closeButton);
    m_mainLayout->addWidget(m_headerWidget);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_contentWidget = new QWidget(m_scrollArea);
    m_contentWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_contentLayout = new QStackedLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);

    m_rowWidget = new QWidget(m_contentWidget);
    m_rowWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_rowLayout = new QHBoxLayout(m_rowWidget);
    m_rowLayout->setContentsMargins(0, 0, 0, 0);
    m_rowLayout->setSpacing(TileSpacing);
    m_contentLayout->addWidget(m_rowWidget);

    m_gridWidget = new QWidget(m_contentWidget);
    m_gridWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_gridLayout = new QGridLayout(m_gridWidget);
    m_gridLayout->setContentsMargins(0, 0, 0, 0);
    m_gridLayout->setSpacing(TileSpacing);
    m_contentLayout->addWidget(m_gridWidget);

    m_hoverWidget = new ComparisonHoverSwitchWidget(m_contentWidget);
    connect(m_hoverWidget, &ComparisonHoverSwitchWidget::toggleFullscreenRequested, this, &ComparisonViewWidget::toggleFullscreenRequested);
    m_contentLayout->addWidget(m_hoverWidget);

    m_carouselWidget = new ComparisonCarouselWidget(m_contentWidget);
    connect(m_carouselWidget, &ComparisonCarouselWidget::previousRequested, this, [this]() {
        moveCarouselIndex(-1);
    });
    connect(m_carouselWidget, &ComparisonCarouselWidget::nextRequested, this, [this]() {
        moveCarouselIndex(1);
    });
    connect(m_carouselWidget, &ComparisonCarouselWidget::firstRequested, this, [this]() {
        setCarouselIndex(0);
    });
    connect(m_carouselWidget, &ComparisonCarouselWidget::lastRequested, this, [this]() {
        setCarouselIndex(m_items.size() - 1);
    });
    connect(m_carouselWidget, &ComparisonCarouselWidget::toggleFullscreenRequested, this, &ComparisonViewWidget::toggleFullscreenRequested);
    m_contentLayout->addWidget(m_carouselWidget);
    m_contentLayout->setCurrentWidget(m_rowWidget);

    m_scrollArea->setWidget(m_contentWidget);
    m_mainLayout->addWidget(m_scrollArea, 1);

    m_threadPool.setMaxThreadCount(2);
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(120);
    connect(&m_reloadTimer, &QTimer::timeout, this, &ComparisonViewWidget::reloadVisibleSession);
}

void ComparisonViewWidget::setSession(
    QVector<ComparisonSessionItem> items,
    const ComparisonRenderSettings &settings,
    ComparisonMode mode)
{
    clearSession();
    if (!supportsItemCount(mode, items.size())) {
        mode = ComparisonMode::Row;
    }
    m_items = std::move(items);
    m_settings = settings;
    m_mode = mode;
    m_alignmentPreset = ComparisonAlignmentPreset::FitCenter;
    m_carouselIndex = 0;
    ++m_generation;
    rebuildPresentation();
    scheduleReload();
    emit statusMessageChanged(tr("Compare: %1 images in %2").arg(m_items.size()).arg(comparisonModeName(m_mode)));
}

int ComparisonViewWidget::appendItems(QVector<ComparisonSessionItem> items)
{
    if (items.isEmpty()) {
        return 0;
    }

    int added = 0;
    if (m_items.isEmpty()) {
        m_settings = ComparisonRenderSettings {};
    }

    for (const ComparisonSessionItem &item : std::as_const(items)) {
        const QString identityKey = item.entry.identityKey();
        if (identityKey.isEmpty() || hasItem(identityKey)) {
            continue;
        }
        const int index = m_items.size();
        m_items.append(item);
        ComparisonTileWidget *tile = createTile(index);
        m_tiles.append(tile);
        ++added;
    }

    if (added > 0) {
        rebuildPresentation();
        refreshTileIndexes();
        scheduleReload();
        emit statusMessageChanged(tr("Compare: %1 images in %2").arg(m_items.size()).arg(comparisonModeName(m_mode)));
    }
    return added;
}

void ComparisonViewWidget::clearSession()
{
    ++m_generation;
    for (auto it = m_decodeWatchers.begin(); it != m_decodeWatchers.end(); ++it) {
        QFutureWatcher<DecodeResult> *watcher = it.key();
        disconnect(watcher, nullptr, this, nullptr);
        watcher->cancel();
        watcher->deleteLater();
    }
    m_decodeWatchers.clear();
    m_cache.clear();
    m_knownSourceSizes.clear();
    m_cacheBytes = 0;
    deleteTiles();
    m_items.clear();
    m_tiles.clear();
    m_mode = ComparisonMode::Row;
    m_alignmentPreset = ComparisonAlignmentPreset::FitCenter;
    m_carouselIndex = 0;
    m_headerLabel->setText(tr("No comparison session"));
    if (m_modeComboBox != nullptr) {
        const QSignalBlocker blocker(m_modeComboBox);
        m_modeComboBox->setCurrentIndex(m_modeComboBox->findData(static_cast<int>(m_mode)));
        m_modeComboBox->setEnabled(false);
        m_modeComboBox->setItemData(
            m_modeComboBox->findData(static_cast<int>(ComparisonMode::Carousel)),
            0,
            Qt::UserRole - 1);
        m_modeComboBox->setItemData(
            m_modeComboBox->findData(static_cast<int>(ComparisonMode::HoverSwitch)),
            0,
            Qt::UserRole - 1);
    }
    if (m_alignmentComboBox != nullptr) {
        const QSignalBlocker blocker(m_alignmentComboBox);
        m_alignmentComboBox->setCurrentIndex(m_alignmentComboBox->findData(static_cast<int>(m_alignmentPreset)));
        m_alignmentComboBox->setEnabled(false);
    }
    if (m_contentLayout != nullptr && m_rowWidget != nullptr) {
        m_contentLayout->setCurrentWidget(m_rowWidget);
    }
    if (m_hoverWidget != nullptr) {
        m_hoverWidget->clearImages();
    }
    if (m_carouselWidget != nullptr) {
        m_carouselWidget->clearImage();
    }
    updateCarouselControls();
    emit statusMessageChanged(QString());
}

bool ComparisonViewWidget::setMode(ComparisonMode mode)
{
    if (!supportsItemCount(mode, m_items.size())) {
        emit statusMessageChanged(mode == ComparisonMode::HoverSwitch
                ? tr("Compare: Hover Switch supports exactly 2 images")
                : tr("Compare: mode is not available for this session"));
        return false;
    }
    if (m_mode == mode) {
        return true;
    }

    m_mode = mode;
    if (m_mode == ComparisonMode::Carousel) {
        m_carouselIndex = wrappedCarouselIndex(m_carouselIndex);
    }
    ++m_generation;
    rebuildPresentation();
    scheduleReload();
    emit statusMessageChanged(tr("Compare: %1 images in %2").arg(m_items.size()).arg(comparisonModeName(m_mode)));
    return true;
}

void ComparisonViewWidget::setAlignmentPreset(ComparisonAlignmentPreset preset)
{
    if (m_alignmentPreset == preset) {
        return;
    }

    m_alignmentPreset = preset;
    if (m_alignmentComboBox != nullptr) {
        const QSignalBlocker blocker(m_alignmentComboBox);
        m_alignmentComboBox->setCurrentIndex(m_alignmentComboBox->findData(static_cast<int>(m_alignmentPreset)));
    }
    for (ComparisonTileWidget *tile : std::as_const(m_tiles)) {
        if (tile == nullptr) {
            continue;
        }
        tile->setAlignmentPreset(m_alignmentPreset);
        tile->resetView();
    }
    if (m_hoverWidget != nullptr) {
        m_hoverWidget->setAlignmentPreset(m_alignmentPreset);
        m_hoverWidget->resetView();
    }
    if (m_carouselWidget != nullptr) {
        m_carouselWidget->setAlignmentPreset(m_alignmentPreset);
        m_carouselWidget->resetView();
    }
    refreshAlignmentState();
    if (m_alignmentPreset == ComparisonAlignmentPreset::CoverCenter) {
        scheduleReload();
    }
}

bool ComparisonViewWidget::hasSession() const
{
    return !m_items.isEmpty();
}

int ComparisonViewWidget::itemCount() const
{
    return m_items.size();
}

bool ComparisonViewWidget::hasItem(const QString &identityKey) const
{
    if (identityKey.isEmpty()) {
        return false;
    }
    for (const ComparisonSessionItem &item : m_items) {
        if (item.entry.identityKey() == identityKey) {
            return true;
        }
    }
    return false;
}

ComparisonMode ComparisonViewWidget::mode() const
{
    return m_mode;
}

ComparisonAlignmentPreset ComparisonViewWidget::alignmentPreset() const
{
    return m_alignmentPreset;
}

bool ComparisonViewWidget::canAppendItems(int uniqueItemCount) const
{
    if (uniqueItemCount <= 0) {
        return true;
    }
    return supportsItemCount(m_mode, m_items.size() + uniqueItemCount);
}

bool ComparisonViewWidget::supportsItemCount(ComparisonMode mode, int count)
{
    switch (mode) {
    case ComparisonMode::Row:
        return count >= 1;
    case ComparisonMode::Grid:
        return count >= 1;
    case ComparisonMode::Carousel:
        return count >= 1;
    case ComparisonMode::HoverSwitch:
        return count == 2;
    }
    return false;
}

void ComparisonViewWidget::setFullscreenPresentation(bool fullscreen)
{
    if (m_headerWidget != nullptr) {
        m_headerWidget->setVisible(!fullscreen);
    }
    if (m_mainLayout != nullptr) {
        m_mainLayout->setContentsMargins(fullscreen ? QMargins() : QMargins(8, 8, 8, 8));
        m_mainLayout->setSpacing(fullscreen ? 0 : 8);
    }
    scheduleReload();
}

void ComparisonViewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (hasSession()) {
        if (m_mode == ComparisonMode::Grid) {
            layoutTiles();
        }
        scheduleReload();
    }
}

void ComparisonViewWidget::rebuildTiles()
{
    deleteTiles();

    for (int index = 0; index < m_items.size(); ++index) {
        ComparisonTileWidget *tile = createTile(index);
        m_tiles.append(tile);
    }
    layoutTiles();
}

void ComparisonViewWidget::rebuildPresentation()
{
    if (m_modeComboBox != nullptr) {
        const QSignalBlocker blocker(m_modeComboBox);
        m_modeComboBox->setEnabled(hasSession());
        m_modeComboBox->setCurrentIndex(m_modeComboBox->findData(static_cast<int>(m_mode)));
        const bool rowEnabled = supportsItemCount(ComparisonMode::Row, m_items.size());
        const bool gridEnabled = supportsItemCount(ComparisonMode::Grid, m_items.size());
        const bool carouselEnabled = supportsItemCount(ComparisonMode::Carousel, m_items.size());
        const bool hoverSwitchEnabled = supportsItemCount(ComparisonMode::HoverSwitch, m_items.size());
        m_modeComboBox->setItemData(
            m_modeComboBox->findData(static_cast<int>(ComparisonMode::Row)),
            rowEnabled ? QVariant() : QVariant(0),
            Qt::UserRole - 1);
        m_modeComboBox->setItemData(
            m_modeComboBox->findData(static_cast<int>(ComparisonMode::Grid)),
            gridEnabled ? QVariant() : QVariant(0),
            Qt::UserRole - 1);
        m_modeComboBox->setItemData(
            m_modeComboBox->findData(static_cast<int>(ComparisonMode::Carousel)),
            carouselEnabled ? QVariant() : QVariant(0),
            Qt::UserRole - 1);
        m_modeComboBox->setItemData(
            m_modeComboBox->findData(static_cast<int>(ComparisonMode::HoverSwitch)),
            hoverSwitchEnabled ? QVariant() : QVariant(0),
            Qt::UserRole - 1);
    }
    if (m_alignmentComboBox != nullptr) {
        const QSignalBlocker blocker(m_alignmentComboBox);
        m_alignmentComboBox->setEnabled(hasSession());
        m_alignmentComboBox->setCurrentIndex(m_alignmentComboBox->findData(static_cast<int>(m_alignmentPreset)));
    }

    m_headerLabel->setText(hasSession()
            ? tr("Compare: %1 (%2 images)").arg(comparisonModeName(m_mode)).arg(m_items.size())
            : tr("No comparison session"));
    updateCarouselControls();

    if (m_mode == ComparisonMode::Row || m_mode == ComparisonMode::Grid) {
        if (m_tiles.size() != m_items.size()) {
            rebuildTiles();
        } else {
            layoutTiles();
        }
        return;
    }

    deleteTiles();

    if (m_mode == ComparisonMode::Carousel) {
        m_carouselIndex = wrappedCarouselIndex(m_carouselIndex);
        if (m_carouselWidget != nullptr) {
            m_carouselWidget->setAlignmentPreset(m_alignmentPreset);
            m_carouselWidget->setIndexInfo(m_carouselIndex, m_items.size());
            m_carouselWidget->setTitle(itemTitle(m_items.at(m_carouselIndex)));
            m_carouselWidget->setPendingMessage(tr("Loading..."));
            m_carouselWidget->setFocus(Qt::OtherFocusReason);
        }
        if (m_contentLayout != nullptr && m_carouselWidget != nullptr) {
            m_contentLayout->setCurrentWidget(m_carouselWidget);
        }
        return;
    }

    if (m_hoverWidget != nullptr && m_items.size() == 2) {
        m_hoverWidget->clearImages();
        m_hoverWidget->setAlignmentPreset(m_alignmentPreset);
        m_hoverWidget->setTitles(itemTitle(m_items.at(0)), itemTitle(m_items.at(1)));
        m_hoverWidget->setMessage(0, tr("Loading..."));
        m_hoverWidget->setMessage(1, tr("Loading..."));
    }
    if (m_contentLayout != nullptr && m_hoverWidget != nullptr) {
        m_contentLayout->setCurrentWidget(m_hoverWidget);
    }
}

void ComparisonViewWidget::layoutTiles()
{
    clearTileLayouts();

    if (m_mode == ComparisonMode::Grid) {
        if (m_contentLayout != nullptr && m_gridWidget != nullptr) {
            m_contentLayout->setCurrentWidget(m_gridWidget);
        }
        const int columns = gridColumnCount(
            m_scrollArea != nullptr && m_scrollArea->viewport() != nullptr
                ? m_scrollArea->viewport()->size()
                : QSize(1, 1),
            m_tiles.size());
        for (int index = 0; index < m_tiles.size(); ++index) {
            m_gridLayout->addWidget(m_tiles.at(index), index / columns, index % columns);
        }
        return;
    }

    if (m_contentLayout != nullptr && m_rowWidget != nullptr) {
        m_contentLayout->setCurrentWidget(m_rowWidget);
    }
    for (ComparisonTileWidget *tile : std::as_const(m_tiles)) {
        m_rowLayout->addWidget(tile, 1);
    }
}

void ComparisonViewWidget::clearTileLayouts()
{
    while (m_rowLayout != nullptr && m_rowLayout->count() > 0) {
        delete m_rowLayout->takeAt(0);
    }
    while (m_gridLayout != nullptr && m_gridLayout->count() > 0) {
        delete m_gridLayout->takeAt(0);
    }
}

void ComparisonViewWidget::deleteTiles()
{
    clearTileLayouts();
    qDeleteAll(m_tiles);
    m_tiles.clear();
}

ComparisonTileWidget *ComparisonViewWidget::createTile(int index)
{
    auto *tile = new ComparisonTileWidget(m_contentWidget);
    tile->setIndex(index);
    tile->setTitle(itemTitle(m_items.at(index)));
    tile->setMessage(tr("Loading..."));
    tile->setAlignmentPreset(m_alignmentPreset);
    connect(tile, &ComparisonTileWidget::reorderDropRequested, this, &ComparisonViewWidget::reorderTile);
    connect(tile, &ComparisonTileWidget::toggleFullscreenRequested, this, &ComparisonViewWidget::toggleFullscreenRequested);
    return tile;
}

void ComparisonViewWidget::scheduleReload()
{
    m_reloadTimer.start();
}

void ComparisonViewWidget::reloadVisibleSession()
{
    if (m_items.isEmpty()) {
        return;
    }

    ++m_generation;
    if (m_mode == ComparisonMode::HoverSwitch) {
        if (m_items.size() != 2) {
            return;
        }
        const QSize targetSize = targetHoverImageSize();
        startDecode(0, adjustedDecodeTargetSize(0, targetSize));
        startDecode(1, adjustedDecodeTargetSize(1, targetSize));
        return;
    }
    if (m_mode == ComparisonMode::Carousel) {
        m_carouselIndex = wrappedCarouselIndex(m_carouselIndex);
        const QSize targetSize = targetCarouselImageSize();
        startDecode(m_carouselIndex, adjustedDecodeTargetSize(m_carouselIndex, targetSize));
        if (m_items.size() > 1) {
            startDecode(wrappedCarouselIndex(m_carouselIndex + 1), adjustedDecodeTargetSize(wrappedCarouselIndex(m_carouselIndex + 1), targetSize));
        }
        if (m_items.size() > 2) {
            startDecode(wrappedCarouselIndex(m_carouselIndex - 1), adjustedDecodeTargetSize(wrappedCarouselIndex(m_carouselIndex - 1), targetSize));
        }
        return;
    }

    const QSize targetSize = m_mode == ComparisonMode::Grid
            ? targetGridImageSize()
            : targetTileImageSize();
    for (int index = 0; index < m_items.size(); ++index) {
        startDecode(index, adjustedDecodeTargetSize(index, targetSize));
    }
}

void ComparisonViewWidget::startDecode(int index, const QSize &targetSize)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }
    if ((m_mode == ComparisonMode::Row || m_mode == ComparisonMode::Grid) && index >= m_tiles.size()) {
        return;
    }
    if (m_mode == ComparisonMode::HoverSwitch && (m_hoverWidget == nullptr || index > 1)) {
        return;
    }
    if (m_mode == ComparisonMode::Carousel && m_carouselWidget == nullptr) {
        return;
    }

    const core::DecodeRequest request = decodeRequestForSize(targetSize);
    const ComparisonSessionItem item = m_items.at(index);
    const QString cacheKey = core::cacheKeyFor(item.entry, request);
    core::DecodedImage cached;
    if (tryGetCached(cacheKey, &cached)) {
        const QString identityKey = item.entry.identityKey();
        if (!identityKey.isEmpty() && !cached.sourceSize.isEmpty() && !m_knownSourceSizes.contains(identityKey)) {
            m_knownSourceSizes.insert(identityKey, cached.sourceSize);
        }
        if (m_mode == ComparisonMode::HoverSwitch) {
            m_hoverWidget->setDecodedImage(index, cached);
        } else if (m_mode == ComparisonMode::Carousel) {
            if (index == m_carouselIndex) {
                m_carouselWidget->setDecodedImage(cached);
            }
        } else {
            m_tiles.at(index)->setDecodedImage(cached);
        }
        refreshAlignmentState();
        if (m_alignmentPreset == ComparisonAlignmentPreset::CoverCenter
            && adjustedDecodeTargetSize(index, targetSize) != targetSize) {
            scheduleReload();
        }
        return;
    }

    if (m_mode == ComparisonMode::HoverSwitch) {
        m_hoverWidget->setMessage(index, tr("Loading..."));
    } else if (m_mode == ComparisonMode::Carousel) {
        if (index == m_carouselIndex) {
            m_carouselWidget->setPendingMessage(tr("Loading..."));
        }
    } else {
        m_tiles.at(index)->setMessage(tr("Loading..."));
    }
    const quint64 generation = m_generation;
    auto *watcher = new QFutureWatcher<DecodeResult>(this);
    m_decodeWatchers.insert(watcher, index);
    connect(watcher, &QFutureWatcher<DecodeResult>::finished, this, [this, watcher]() {
        handleDecodeFinished(watcher);
    });
    watcher->setFuture(QtConcurrent::run(&m_threadPool, [generation, index, cacheKey, item, request]() {
        core::VipsImageDecoder decoder;
        DecodeResult result;
        result.generation = generation;
        result.index = index;
        result.cacheKey = cacheKey;
        result.decoded = decoder.decode(item.entry, request, item.archivePassword);
        return result;
    }));
}

void ComparisonViewWidget::handleDecodeFinished(QFutureWatcher<DecodeResult> *watcher)
{
    if (watcher == nullptr) {
        return;
    }

    m_decodeWatchers.remove(watcher);
    const QFuture<DecodeResult> future = watcher->future();
    watcher->deleteLater();
    if (future.isCanceled() || future.resultCount() == 0 || !future.isResultReadyAt(0)) {
        return;
    }

    const DecodeResult result = future.result();
    if (result.generation != m_generation || result.index < 0 || result.index >= m_items.size()) {
        return;
    }
    if ((m_mode == ComparisonMode::Row || m_mode == ComparisonMode::Grid) && result.index >= m_tiles.size()) {
        return;
    }
    if (m_mode == ComparisonMode::HoverSwitch && (m_hoverWidget == nullptr || result.index > 1)) {
        return;
    }
    if (m_mode == ComparisonMode::Carousel && m_carouselWidget == nullptr) {
        return;
    }

    if (result.decoded.ok()) {
        bool sourceSizeDiscovered = false;
        if (result.index >= 0 && result.index < m_items.size() && !result.decoded.sourceSize.isEmpty()) {
            const QString identityKey = m_items.at(result.index).entry.identityKey();
            if (!identityKey.isEmpty() && !m_knownSourceSizes.contains(identityKey)) {
                m_knownSourceSizes.insert(identityKey, result.decoded.sourceSize);
                sourceSizeDiscovered = true;
            }
        }
        insertCached(result.cacheKey, result.decoded);
        if (m_mode == ComparisonMode::HoverSwitch) {
            m_hoverWidget->setDecodedImage(result.index, result.decoded);
        } else if (m_mode == ComparisonMode::Carousel) {
            if (result.index == m_carouselIndex) {
                m_carouselWidget->setDecodedImage(result.decoded);
            }
        } else {
            m_tiles.at(result.index)->setDecodedImage(result.decoded);
        }
        refreshAlignmentState();
        if (sourceSizeDiscovered && m_alignmentPreset == ComparisonAlignmentPreset::CoverCenter) {
            scheduleReload();
        }
    } else {
        const QString message = result.decoded.errorMessage.isEmpty()
                ? tr("Could not decode image.")
                : result.decoded.errorMessage;
        if (m_mode == ComparisonMode::HoverSwitch) {
            m_hoverWidget->setMessage(result.index, message);
        } else if (m_mode == ComparisonMode::Carousel) {
            if (result.index == m_carouselIndex) {
                m_carouselWidget->setMessage(message);
            }
        } else {
            m_tiles.at(result.index)->setMessage(message);
        }
    }
}

void ComparisonViewWidget::updateCarouselControls()
{
    const bool visible = hasSession() && m_mode == ComparisonMode::Carousel;
    if (m_carouselPreviousButton != nullptr) {
        m_carouselPreviousButton->setVisible(visible);
        m_carouselPreviousButton->setEnabled(m_items.size() > 1);
    }
    if (m_carouselNextButton != nullptr) {
        m_carouselNextButton->setVisible(visible);
        m_carouselNextButton->setEnabled(m_items.size() > 1);
    }
}

void ComparisonViewWidget::setCarouselIndex(int index)
{
    if (m_items.isEmpty()) {
        return;
    }

    m_carouselIndex = wrappedCarouselIndex(index);
    if (m_carouselWidget != nullptr) {
        m_carouselWidget->setIndexInfo(m_carouselIndex, m_items.size());
        m_carouselWidget->setTitle(itemTitle(m_items.at(m_carouselIndex)));
        m_carouselWidget->setPendingMessage(tr("Loading..."));
        m_carouselWidget->setFocus(Qt::OtherFocusReason);
    }
    ++m_generation;
    scheduleReload();
    emit statusMessageChanged(tr("Compare: Carousel %1/%2").arg(m_carouselIndex + 1).arg(m_items.size()));
}

void ComparisonViewWidget::moveCarouselIndex(int offset)
{
    if (m_items.isEmpty()) {
        return;
    }
    setCarouselIndex(m_carouselIndex + offset);
}

int ComparisonViewWidget::wrappedCarouselIndex(int index) const
{
    if (m_items.isEmpty()) {
        return 0;
    }
    const int count = m_items.size();
    return ((index % count) + count) % count;
}

core::DecodeRequest ComparisonViewWidget::decodeRequestForSize(const QSize &targetSize) const
{
    core::DecodeRequest request;
    request.viewportSize = targetSize.expandedTo(QSize(1, 1));
    request.devicePixelRatio = sanitizedDevicePixelRatio(m_settings.devicePixelRatio);
    request.fitToWindow = true;
    request.regionRendering = false;
    request.exifAutorotate = m_settings.exifAutorotate;
    request.scalingQuality = m_settings.scalingQuality;
    request.colorManagementEnabled = m_settings.colorManagementEnabled;
    request.maxDecodedBytes = 384LL * 1024LL * 1024LL;
    return request;
}

QSize ComparisonViewWidget::targetTileImageSize() const
{
    if (m_scrollArea == nullptr || m_scrollArea->viewport() == nullptr) {
        return QSize(240, 360);
    }

    const QSize viewportSize = m_scrollArea->viewport()->size();
    const int count = std::max(1, static_cast<int>(m_items.size()));
    const int totalSpacing = TileSpacing * std::max(0, count - 1);
    const int tileWidth = std::max(
        1,
        (viewportSize.width() - totalSpacing) / count);
    return QSize(
        std::max(1, tileWidth - TileMargin * 2),
        std::max(1, viewportSize.height() - TileTitleHeight - TileMargin * 2));
}

QSize ComparisonViewWidget::targetGridImageSize() const
{
    if (m_scrollArea == nullptr || m_scrollArea->viewport() == nullptr) {
        return QSize(240, 360);
    }

    const QSize viewportSize = m_scrollArea->viewport()->size();
    const int count = std::max(1, static_cast<int>(m_items.size()));
    const int columns = gridColumnCount(viewportSize, count);
    const int rows = std::max(1, (count + columns - 1) / columns);
    const int tileWidth = std::max(
        1,
        (viewportSize.width() - TileSpacing * std::max(0, columns - 1)) / columns);
    const int tileHeight = std::max(
        1,
        (viewportSize.height() - TileSpacing * std::max(0, rows - 1)) / rows);
    return QSize(
        std::max(1, tileWidth - TileMargin * 2),
        std::max(1, tileHeight - TileTitleHeight - TileMargin * 2));
}

QSize ComparisonViewWidget::targetHoverImageSize() const
{
    if (m_hoverWidget == nullptr) {
        return QSize(240, 360);
    }
    return m_hoverWidget->targetImageSize();
}

QSize ComparisonViewWidget::targetCarouselImageSize() const
{
    if (m_carouselWidget == nullptr) {
        return QSize(240, 360);
    }
    return m_carouselWidget->targetImageSize();
}

int ComparisonViewWidget::gridColumnCount(const QSize &viewportSize, int count) const
{
    count = std::max(1, count);
    int bestColumns = 1;
    double bestScore = -1.0;

    for (int columns = 1; columns <= count; ++columns) {
        const int rows = (count + columns - 1) / columns;
        if (count > 2 && (columns == 1 || rows == 1)) {
            continue;
        }

        const int tileWidth = (viewportSize.width() - TileSpacing * std::max(0, columns - 1)) / columns;
        const int tileHeight = (viewportSize.height() - TileSpacing * std::max(0, rows - 1)) / rows;
        if (tileWidth <= 0 || tileHeight <= 0) {
            continue;
        }

        const double occupancy = static_cast<double>(count) / static_cast<double>(rows * columns);
        const double aspect = static_cast<double>(tileWidth) / static_cast<double>(tileHeight);
        const double aspectScore = std::min(aspect, 1.0 / aspect);
        const double score = static_cast<double>(tileWidth) * static_cast<double>(tileHeight) * occupancy * aspectScore;
        if (score > bestScore) {
            bestScore = score;
            bestColumns = columns;
        }
    }

    if (bestScore < 0.0) {
        bestColumns = std::clamp(static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count)))), 1, count);
    }
    return bestColumns;
}

bool ComparisonViewWidget::tryGetCached(const QString &cacheKey, core::DecodedImage *decoded)
{
    auto it = m_cache.find(cacheKey);
    if (it == m_cache.end()) {
        return false;
    }

    it->lastUsed = ++m_cacheClock;
    if (decoded != nullptr) {
        *decoded = it->decoded;
    }
    return true;
}

void ComparisonViewWidget::insertCached(const QString &cacheKey, const core::DecodedImage &decoded)
{
    if (cacheKey.isEmpty() || !decoded.ok()) {
        return;
    }

    auto existing = m_cache.find(cacheKey);
    if (existing != m_cache.end()) {
        m_cacheBytes -= existing->bytes;
        m_cache.erase(existing);
    }

    CacheItem item;
    item.decoded = decoded;
    item.bytes = decoded.image.sizeInBytes();
    item.lastUsed = ++m_cacheClock;
    m_cache.insert(cacheKey, item);
    m_cacheBytes += item.bytes;
    evictCacheIfNeeded();
}

void ComparisonViewWidget::evictCacheIfNeeded()
{
    while (m_cacheBytes > ComparisonCacheBudgetBytes && !m_cache.isEmpty()) {
        auto oldest = m_cache.begin();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
            if (it->lastUsed < oldest->lastUsed) {
                oldest = it;
            }
        }
        m_cacheBytes -= oldest->bytes;
        m_cache.erase(oldest);
    }
}

void ComparisonViewWidget::reorderTile(int sourceIndex, QPoint globalPosition)
{
    if (sourceIndex < 0 || sourceIndex >= m_items.size()) {
        return;
    }

    int targetIndex = sourceIndex;
    for (int index = 0; index < m_tiles.size(); ++index) {
        ComparisonTileWidget *tile = m_tiles.at(index);
        if (tile != nullptr && tile->rect().contains(tile->mapFromGlobal(globalPosition))) {
            targetIndex = index;
            break;
        }
    }

    if (targetIndex == sourceIndex) {
        return;
    }

    m_items.move(sourceIndex, targetIndex);
    m_tiles.move(sourceIndex, targetIndex);
    layoutTiles();
    refreshTileIndexes();
    scheduleReload();
}

void ComparisonViewWidget::refreshTileIndexes()
{
    for (int index = 0; index < m_tiles.size(); ++index) {
        m_tiles.at(index)->setIndex(index);
    }
}

void ComparisonViewWidget::refreshAlignmentState()
{
    double sharedSourceScale = 0.0;
    if (m_alignmentPreset == ComparisonAlignmentPreset::SameSourceScale) {
        auto includeScale = [&sharedSourceScale](double scale) {
            if (!std::isfinite(scale) || scale <= 0.0) {
                return;
            }
            if (sharedSourceScale <= 0.0 || scale < sharedSourceScale) {
                sharedSourceScale = scale;
            }
        };

        if (m_mode == ComparisonMode::HoverSwitch && m_hoverWidget != nullptr) {
            includeScale(m_hoverWidget->sourceLogicalScale(0));
            includeScale(m_hoverWidget->sourceLogicalScale(1));
        } else if (m_mode == ComparisonMode::Carousel && m_carouselWidget != nullptr) {
            includeScale(m_carouselWidget->sourceLogicalScale());
        } else {
            for (const ComparisonTileWidget *tile : std::as_const(m_tiles)) {
                if (tile != nullptr) {
                    includeScale(tile->sourceLogicalScale());
                }
            }
        }
    }

    for (ComparisonTileWidget *tile : std::as_const(m_tiles)) {
        if (tile == nullptr) {
            continue;
        }
        tile->setAlignmentPreset(m_alignmentPreset);
        tile->setSharedSourceScale(sharedSourceScale);
    }
    if (m_hoverWidget != nullptr) {
        m_hoverWidget->setAlignmentPreset(m_alignmentPreset);
        m_hoverWidget->setSharedSourceScale(sharedSourceScale);
    }
    if (m_carouselWidget != nullptr) {
        m_carouselWidget->setAlignmentPreset(m_alignmentPreset);
        m_carouselWidget->setSharedSourceScale(sharedSourceScale);
    }
}

QSize ComparisonViewWidget::adjustedDecodeTargetSize(int index, const QSize &baseTargetSize) const
{
    if (m_alignmentPreset != ComparisonAlignmentPreset::CoverCenter
        || index < 0
        || index >= m_items.size()) {
        return baseTargetSize;
    }

    const QString identityKey = m_items.at(index).entry.identityKey();
    const auto it = m_knownSourceSizes.constFind(identityKey);
    if (identityKey.isEmpty() || it == m_knownSourceSizes.constEnd()) {
        return baseTargetSize;
    }
    return coverDecodeTargetSize(*it, baseTargetSize);
}

} // namespace insidious::ui
