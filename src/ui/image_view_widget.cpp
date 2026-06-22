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
}

void ImageViewWidget::setImage(const QImage &image)
{
    m_image = image;
    m_message.clear();
    update();
}

void ImageViewWidget::setMessage(const QString &message)
{
    m_image = QImage();
    m_message = message;
    update();
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

void ImageViewWidget::setPanOffset(QPointF offset)
{
    m_panOffset = offset;
    update();
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

        return;
    }

    if (!m_message.isEmpty()) {
        painter.setPen(QColor(220, 220, 220));
        painter.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, m_message);
    }
}

void ImageViewWidget::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        if (event->angleDelta().y() > 0) {
            emit zoomInRequested();
        } else if (event->angleDelta().y() < 0) {
            emit zoomOutRequested();
        }
        event->accept();
        return;
    }

    if (event->angleDelta().y() > 0) {
        emit navigatePreviousRequested();
    } else if (event->angleDelta().y() < 0) {
        emit navigateNextRequested();
    }
    event->accept();
}

void ImageViewWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_image.isNull() && m_panningEnabled) {
        m_panning = true;
        m_leftDragMoved = false;
        m_leftPressPosition = event->pos();
        m_lastMousePosition = event->pos();
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && !m_image.isNull()) {
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
    if (event->button() == Qt::LeftButton && m_panning) {
        const bool wasDrag = m_leftDragMoved;
        m_panning = false;
        m_leftDragMoved = false;
        unsetCursor();
        if (!wasDrag) {
            emit imageClicked(event->pos());
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && !m_image.isNull()) {
        const int distance = (event->pos() - m_leftPressPosition).manhattanLength();
        if (distance < QApplication::startDragDistance()) {
            emit imageClicked(event->pos());
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
        m_panning = false;
        m_leftDragMoved = false;
        m_selectingZoom = false;
        unsetCursor();
        update();
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

} // namespace insidious::ui
