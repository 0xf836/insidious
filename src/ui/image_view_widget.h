#pragma once

#include "core/app_settings.h"

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
    void setImage(const QImage &image, QSize fullImageSize, QPoint imageRegionOffset);
    void setMessage(const QString &message);
    void setOverlayMessage(const QString &message);
    void setImageInteractionEnabled(bool enabled);
    void setPanningEnabled(bool enabled);
    void setFitHandPanEnabled(bool enabled);
    void setClickZoomBehavior(core::ClickZoomBehavior behavior);
    void setClickZoomCursorEnabled(bool enabled);
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
    void wheelActionRequested(Qt::KeyboardModifiers modifiers, int angleDeltaY, QPoint position);
    void mouseShortcutRequested(Qt::KeyboardModifiers modifiers, Qt::MouseButton button, bool *handled);
    void imageClicked(QPoint position);
    void holdZoomStarted(QPoint position);
    void holdZoomMoved(QPoint delta);
    void holdZoomEnded();
    void fitHandPanStarted(QPoint position);
    void fitHandPanMoved(QPoint delta);
    void fitHandPanEnded();
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
    [[nodiscard]] bool isFitHandPanTrigger(Qt::MouseButton button, Qt::KeyboardModifiers modifiers) const;
    void scheduleImageClick(QPoint position);
    void cancelPendingImageClick();
    void stopImageInteraction();
    void updateIdleCursor();
    [[nodiscard]] bool shouldShowClickZoomCursor() const;

    QImage m_image;
    QSize m_fullImageSize;
    QPoint m_imageRegionOffset;
    QString m_message;
    QString m_overlayMessage;
    QPointF m_panOffset;
    QPoint m_lastMousePosition;
    QPoint m_leftPressPosition;
    QPoint m_pendingClickPosition;
    QPoint m_zoomSelectionStart;
    QPoint m_zoomSelectionEnd;
    QTimer m_clickTimer;
    Qt::MouseButton m_fitHandPanButton = Qt::NoButton;
    bool m_imageInteractionEnabled = true;
    bool m_panningEnabled = true;
    bool m_fitHandPanEnabled = false;
    bool m_panning = false;
    bool m_fitHandPanning = false;
    bool m_leftDragMoved = false;
    bool m_clickPending = false;
    bool m_selectingZoom = false;
    bool m_holdingZoom = false;
    bool m_clickZoomCursorEnabled = false;
    core::ClickZoomBehavior m_clickZoomBehavior = core::ClickZoomBehavior::ToggleOnClick;
};

} // namespace insidious::ui
