#include "ui/image_view_widget.h"

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

void ImageViewWidget::setPanOffset(QPointF offset)
{
    m_panOffset = offset;
    update();
}

QPointF ImageViewWidget::panOffset() const
{
    return m_panOffset;
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
    if (event->button() == Qt::RightButton) {
        m_panning = true;
        m_lastMousePosition = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void ImageViewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastMousePosition;
        m_lastMousePosition = event->pos();
        m_panOffset += delta;
        emit panChanged(m_panOffset);
        update();
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void ImageViewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton && m_panning) {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        emit toggleZoomRequested();
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void ImageViewWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
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

} // namespace insidious::ui
