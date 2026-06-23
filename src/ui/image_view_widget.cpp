#include "ui/image_view_widget.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

namespace insidious::ui {

ImageViewWidget::ImageViewWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    setMouseTracking(true);

    m_clickTimer.setSingleShot(true);
    m_clickTimer.setInterval(QApplication::doubleClickInterval());
    connect(&m_clickTimer, &QTimer::timeout, this, [this]() {
        if (!m_clickPending) {
            return;
        }
        m_clickPending = false;
        emit imageClicked(m_pendingClickPosition);
    });
}

void ImageViewWidget::setImage(const QImage &image)
{
    m_image = image;
    m_message.clear();
    m_overlayMessage.clear();
    update();
}

void ImageViewWidget::setMessage(const QString &message)
{
    m_image = QImage();
    m_message = message;
    m_overlayMessage.clear();
    stopImageInteraction();
    cancelPendingImageClick();
    update();
}

void ImageViewWidget::setOverlayMessage(const QString &message)
{
    m_overlayMessage = message;
    update();
}

void ImageViewWidget::setImageInteractionEnabled(bool enabled)
{
    if (m_imageInteractionEnabled == enabled) {
        return;
    }

    m_imageInteractionEnabled = enabled;
    if (!m_imageInteractionEnabled) {
        stopImageInteraction();
        cancelPendingImageClick();
        update();
    }
}

void ImageViewWidget::setPanningEnabled(bool enabled)
{
    if (m_panningEnabled == enabled) {
        return;
    }

    m_panningEnabled = enabled;
    if (!m_panningEnabled) {
        m_panning = false;
        m_leftDragMoved = false;
        unsetCursor();
    }
}

void ImageViewWidget::setClickZoomBehavior(core::ClickZoomBehavior behavior)
{
    if (m_clickZoomBehavior == behavior) {
        return;
    }

    m_clickZoomBehavior = behavior;
    stopImageInteraction();
    cancelPendingImageClick();
    update();
}

void ImageViewWidget::setPanOffset(QPointF offset)
{
    m_panOffset = offset;
    update();
}

bool ImageViewWidget::hasImage() const
{
    return !m_image.isNull();
}

QPointF ImageViewWidget::panOffset() const
{
    return m_panOffset;
}

QRectF ImageViewWidget::imageRect() const
{
    if (m_image.isNull()) {
        return QRectF();
    }
    return QRectF(imageTopLeft(), QSizeF(m_image.size()));
}

void ImageViewWidget::resetPan()
{
    setPanOffset(QPointF());
}

void ImageViewWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), QColor(24, 24, 24));

    if (!m_image.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(imageTopLeft(), m_image);

        if (m_selectingZoom) {
            const QRect selection = zoomSelectionRect().intersected(rect());
            if (!selection.isEmpty()) {
                painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
                painter.fillRect(selection, QColor(80, 150, 255, 48));
                painter.setPen(QPen(QColor(180, 220, 255), 1));
                painter.drawRect(selection.adjusted(0, 0, -1, -1));
            }
        }

        if (!m_overlayMessage.isEmpty()) {
            painter.setRenderHint(QPainter::Antialiasing, true);
            const QFontMetrics metrics(painter.font());
            const QSize textSize = metrics.size(Qt::TextSingleLine, m_overlayMessage);
            const int horizontalPadding = 18;
            const int verticalPadding = 8;
            const QSize boxSize(textSize.width() + horizontalPadding * 2, textSize.height() + verticalPadding * 2);
            const QRect box(
                (width() - boxSize.width()) / 2,
                18,
                boxSize.width(),
                boxSize.height());

            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 120));
            painter.drawRoundedRect(box, 6, 6);
            painter.setPen(QColor(245, 245, 245, 220));
            painter.drawText(box, Qt::AlignCenter, m_overlayMessage);
        }

        return;
    }

    if (!m_message.isEmpty()) {
        painter.setPen(QColor(220, 220, 220));
        painter.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, m_message);
    }
}

void ImageViewWidget::wheelEvent(QWheelEvent *event)
{
    emit wheelActionRequested(event->modifiers(), event->angleDelta().y(), event->position().toPoint());
    event->accept();
}

void ImageViewWidget::mousePressEvent(QMouseEvent *event)
{
    if (!m_imageInteractionEnabled && (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)) {
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && !m_image.isNull() && m_panningEnabled) {
        if (m_clickZoomBehavior == core::ClickZoomBehavior::HoldToZoomPan) {
            m_holdingZoom = true;
            m_lastMousePosition = event->pos();
            setCursor(Qt::ClosedHandCursor);
            emit holdZoomStarted(event->pos());
            event->accept();
            return;
        }

        m_panning = true;
        m_leftDragMoved = false;
        m_leftPressPosition = event->pos();
        m_lastMousePosition = event->pos();
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && !m_image.isNull()) {
        if (m_clickZoomBehavior == core::ClickZoomBehavior::HoldToZoomPan) {
            m_holdingZoom = true;
            m_lastMousePosition = event->pos();
            setCursor(Qt::ClosedHandCursor);
            emit holdZoomStarted(event->pos());
            event->accept();
            return;
        }

        m_leftPressPosition = event->pos();
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton && !m_image.isNull()) {
        m_selectingZoom = true;
        m_zoomSelectionStart = event->pos();
        m_zoomSelectionEnd = event->pos();
        setCursor(Qt::CrossCursor);
        update();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void ImageViewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_holdingZoom) {
        const QPoint delta = event->pos() - m_lastMousePosition;
        m_lastMousePosition = event->pos();
        if (!delta.isNull()) {
            emit holdZoomMoved(delta);
        }
        event->accept();
        return;
    }

    if (m_panning) {
        if (!m_leftDragMoved) {
            const int distance = (event->pos() - m_leftPressPosition).manhattanLength();
            if (distance < QApplication::startDragDistance()) {
                event->accept();
                return;
            }
            m_leftDragMoved = true;
            setCursor(Qt::ClosedHandCursor);
        }

        const QPoint delta = event->pos() - m_lastMousePosition;
        m_lastMousePosition = event->pos();
        m_panOffset += delta;
        emit panChanged(m_panOffset);
        update();
        event->accept();
        return;
    }

    if (m_selectingZoom) {
        m_zoomSelectionEnd = event->pos();
        update();
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void ImageViewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_imageInteractionEnabled && (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)) {
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_panning) {
        const bool wasDrag = m_leftDragMoved;
        m_panning = false;
        m_leftDragMoved = false;
        unsetCursor();
        if (!wasDrag) {
            scheduleImageClick(event->pos());
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_holdingZoom) {
        m_holdingZoom = false;
        unsetCursor();
        emit holdZoomEnded();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && !m_image.isNull()) {
        const int distance = (event->pos() - m_leftPressPosition).manhattanLength();
        if (distance < QApplication::startDragDistance()) {
            scheduleImageClick(event->pos());
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton && m_selectingZoom) {
        m_zoomSelectionEnd = event->pos();
        const QRect selection = zoomSelectionRect().intersected(rect());
        m_selectingZoom = false;
        unsetCursor();
        update();
        if (selection.width() >= 8 && selection.height() >= 8) {
            emit zoomToRectRequested(selection);
        }
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void ImageViewWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        cancelPendingImageClick();
        const bool wasHoldingZoom = m_holdingZoom;
        m_panning = false;
        m_leftDragMoved = false;
        m_selectingZoom = false;
        m_holdingZoom = false;
        unsetCursor();
        update();
        if (wasHoldingZoom) {
            emit holdZoomEnded();
        }
        emit toggleFullscreenRequested();
        event->accept();
        return;
    }

    QWidget::mouseDoubleClickEvent(event);
}

QPointF ImageViewWidget::imageTopLeft() const
{
    const QPointF centered((width() - m_image.width()) / 2.0, (height() - m_image.height()) / 2.0);
    return centered + m_panOffset;
}

QRect ImageViewWidget::zoomSelectionRect() const
{
    return QRect(m_zoomSelectionStart, m_zoomSelectionEnd).normalized();
}

void ImageViewWidget::scheduleImageClick(QPoint position)
{
    m_pendingClickPosition = position;
    m_clickPending = true;
    m_clickTimer.start();
}

void ImageViewWidget::cancelPendingImageClick()
{
    m_clickPending = false;
    m_clickTimer.stop();
}

void ImageViewWidget::stopImageInteraction()
{
    m_panning = false;
    m_leftDragMoved = false;
    m_selectingZoom = false;
    m_holdingZoom = false;
    unsetCursor();
}

} // namespace insidious::ui
