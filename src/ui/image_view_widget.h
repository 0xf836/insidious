#pragma once

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QTimer>
#include <QWidget>
#include <Qt>

namespace insidious::ui {

class ImageViewWidget : public QWidget {
    Q_OBJECT

public:
    explicit ImageViewWidget(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void setMessage(const QString &message);
    void setOverlayMessage(const QString &message);
    void setImageInteractionEnabled(bool enabled);
    void setPanningEnabled(bool enabled);
    void setPanOffset(QPointF offset);
    [[nodiscard]] bool hasImage() const;
    [[nodiscard]] QPointF panOffset() const;
    [[nodiscard]] QRectF imageRect() const;
    void resetPan();

signals:
    void navigatePreviousRequested();
    void navigateNextRequested();
    void zoomInRequested();
    void zoomOutRequested();
    void wheelActionRequested(Qt::KeyboardModifiers modifiers, int angleDeltaY);
    void imageClicked(QPoint position);
    void zoomToRectRequested(QRect rect);
    void toggleFullscreenRequested();
    void panChanged(QPointF offset);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    [[nodiscard]] QPointF imageTopLeft() const;
    [[nodiscard]] QRect zoomSelectionRect() const;
    void scheduleImageClick(QPoint position);
    void cancelPendingImageClick();
    void stopImageInteraction();

    QImage m_image;
    QString m_message;
    QString m_overlayMessage;
    QPointF m_panOffset;
    QPoint m_lastMousePosition;
    QPoint m_leftPressPosition;
    QPoint m_pendingClickPosition;
    QPoint m_zoomSelectionStart;
    QPoint m_zoomSelectionEnd;
    QTimer m_clickTimer;
    bool m_imageInteractionEnabled = true;
    bool m_panningEnabled = true;
    bool m_panning = false;
    bool m_leftDragMoved = false;
    bool m_clickPending = false;
    bool m_selectingZoom = false;
};

} // namespace insidious::ui
