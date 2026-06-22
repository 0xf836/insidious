#pragma once

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QWidget>

namespace insidious::ui {

class ImageViewWidget : public QWidget {
    Q_OBJECT

public:
    explicit ImageViewWidget(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void setMessage(const QString &message);
    void setPanningEnabled(bool enabled);
    void setPanOffset(QPointF offset);
    [[nodiscard]] QPointF panOffset() const;
    [[nodiscard]] QRectF imageRect() const;
    void resetPan();

signals:
    void navigatePreviousRequested();
    void navigateNextRequested();
    void zoomInRequested();
    void zoomOutRequested();
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

    QImage m_image;
    QString m_message;
    QPointF m_panOffset;
    QPoint m_lastMousePosition;
    QPoint m_leftPressPosition;
    QPoint m_zoomSelectionStart;
    QPoint m_zoomSelectionEnd;
    bool m_panningEnabled = true;
    bool m_panning = false;
    bool m_leftDragMoved = false;
    bool m_selectingZoom = false;
};

} // namespace insidious::ui
