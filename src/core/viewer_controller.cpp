#include "core/viewer_controller.h"

#include <algorithm>

namespace insidious::core {
namespace {

int normalizeRotation(int degrees)
{
    degrees %= 360;
    if (degrees < 0) {
        degrees += 360;
    }
    return degrees;
}

} // namespace

void ViewerController::setCollection(QVector<ImageEntry> entries, int selectedIndex)
{
    m_entries = std::move(entries);
    if (m_entries.isEmpty()) {
        m_currentIndex = -1;
        return;
    }
    m_currentIndex = std::clamp(selectedIndex, 0, static_cast<int>(m_entries.size()) - 1);
    resetPan();
}

void ViewerController::clearCollection()
{
    m_entries.clear();
    m_currentIndex = -1;
    resetPan();
}

bool ViewerController::hasCollection() const
{
    return currentEntry() != nullptr;
}

int ViewerController::count() const
{
    return m_entries.size();
}

int ViewerController::currentIndex() const
{
    return m_currentIndex;
}

const ImageEntry *ViewerController::currentEntry() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_entries.size()) {
        return nullptr;
    }
    return &m_entries.at(m_currentIndex);
}

const QVector<ImageEntry> &ViewerController::entries() const
{
    return m_entries;
}

bool ViewerController::goNext()
{
    return setCurrentIndex(m_currentIndex + 1);
}

bool ViewerController::goPrevious()
{
    return setCurrentIndex(m_currentIndex - 1);
}

bool ViewerController::goFirst()
{
    return setCurrentIndex(0);
}

bool ViewerController::goLast()
{
    return setCurrentIndex(m_entries.size() - 1);
}

bool ViewerController::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_entries.size() || index == m_currentIndex) {
        return false;
    }
    m_currentIndex = index;
    resetPan();
    return true;
}

void ViewerController::setFitToWindow()
{
    m_zoomMode = ZoomMode::FitToWindow;
    m_zoomFactor = 1.0;
    resetPan();
}

void ViewerController::setActualSize()
{
    m_zoomMode = ZoomMode::ActualSize;
    m_zoomFactor = 1.0;
    resetPan();
}

void ViewerController::setCustomZoom(double zoomFactor)
{
    m_zoomMode = ZoomMode::Custom;
    m_zoomFactor = std::clamp(zoomFactor, MinimumZoomFactor, MaximumZoomFactor);
}

void ViewerController::zoomIn()
{
    m_zoomMode = ZoomMode::Custom;
    m_zoomFactor = std::min(m_zoomFactor * 1.25, MaximumZoomFactor);
}

void ViewerController::zoomOut()
{
    m_zoomMode = ZoomMode::Custom;
    m_zoomFactor = std::max(m_zoomFactor / 1.25, MinimumZoomFactor);
}

void ViewerController::resetPan()
{
    m_panOffset = QPointF();
}

void ViewerController::setPanOffset(QPointF offset)
{
    m_panOffset = offset;
}

void ViewerController::rotateLeft()
{
    m_rotationDegrees = normalizeRotation(m_rotationDegrees - 90);
    resetPan();
}

void ViewerController::rotateRight()
{
    m_rotationDegrees = normalizeRotation(m_rotationDegrees + 90);
    resetPan();
}

ZoomMode ViewerController::zoomMode() const
{
    return m_zoomMode;
}

double ViewerController::zoomFactor() const
{
    return m_zoomFactor;
}

QPointF ViewerController::panOffset() const
{
    return m_panOffset;
}

int ViewerController::rotationDegrees() const
{
    return m_rotationDegrees;
}

} // namespace insidious::core
