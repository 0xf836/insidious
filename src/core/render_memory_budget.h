#pragma once

#include <QSize>
#include <QtGlobal>

namespace insidious::core {

struct RenderMemoryEstimate {
    QSize outputSize;
    int bytesPerPixel = 4;
    qint64 bytes = 0;
    bool overflowed = false;
};

[[nodiscard]] RenderMemoryEstimate estimateRenderMemory(QSize inputSize, int bands, double scale, int bytesPerSample = 1);
[[nodiscard]] bool exceedsRenderMemoryBudget(QSize inputSize, int bands, double scale, qint64 maxBytes, int bytesPerSample = 1);

} // namespace insidious::core
