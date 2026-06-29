#pragma once

#include "core/image_entry.h"

#include <QPointF>
#include <QVector>

namespace insidious::core {

inline constexpr double MinimumZoomFactor = 0.05;
inline constexpr double MaximumZoomFactor = 16.0;

enum class ZoomMode {
    FitToWindow,
    ActualSize,
    Custom,
};

class ViewerController {
public:
    void setCollection(QVector<ImageEntry> entries, int selectedIndex);
    void clearCollection();

    [[nodiscard]] bool hasCollection() const;
    [[nodiscard]] int count() const;
    [[nodiscard]] int currentIndex() const;
    [[nodiscard]] const ImageEntry *currentEntry() const;
    [[nodiscard]] const QVector<ImageEntry> &entries() const;

    bool goNext();
    bool goPrevious();
    bool goFirst();
    bool goLast();
    bool setCurrentIndex(int index);

    void setFitToWindow();
    void setActualSize();
    void setCustomZoom(double zoomFactor);
    void zoomIn();
    void zoomOut();
    void resetPan();
    void setPanOffset(QPointF offset);
    void resetRotation();
    void rotateLeft();
    void rotateRight();

    [[nodiscard]] ZoomMode zoomMode() const;
    [[nodiscard]] double zoomFactor() const;
    [[nodiscard]] QPointF panOffset() const;
    [[nodiscard]] int rotationDegrees() const;

private:
    QVector<ImageEntry> m_entries;
    int m_currentIndex = -1;
    ZoomMode m_zoomMode = ZoomMode::FitToWindow;
    double m_zoomFactor = 1.0;
    QPointF m_panOffset;
    int m_rotationDegrees = 0;
};

} // namespace insidious::core
