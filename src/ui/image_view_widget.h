#pragma once

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QWidget>

namespace insidious::ui {

class ImageViewWidget : public QWidget {
    Q_OBJECT

public:
    explicit ImageViewWidget(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void setMessage(const QString &message);
    void setPanOffset(QPointF offset);
    [[nodiscard]] QPointF panOffset() const;
    void resetPan();

signals:
    void navigatePreviousRequested();
    void navigateNextRequested();
    void zoomInRequested();
    void zoomOutRequested();
    void toggleZoomRequested();
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

    QImage m_image;
    QString m_message;
    QPointF m_panOffset;
    QPoint m_lastMousePosition;
    bool m_panning = false;
};

} // namespace insidious::ui
