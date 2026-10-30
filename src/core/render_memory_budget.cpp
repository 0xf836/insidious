#include "core/render_memory_budget.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace insidious::core {
namespace {

constexpr qint64 MaximumBudgetableBytes = std::numeric_limits<qint64>::max();

RenderMemoryEstimate overflowEstimate(int bytesPerPixel)
{
    RenderMemoryEstimate estimate;
    estimate.bytesPerPixel = bytesPerPixel;
    estimate.bytes = MaximumBudgetableBytes;
    estimate.overflowed = true;
    return estimate;
}

} // namespace

RenderMemoryEstimate estimateRenderMemory(QSize inputSize, int bands, double scale, int bytesPerSample)
{
    if (bytesPerSample <= 0 || bands <= 0
        || bands > std::numeric_limits<int>::max() / bytesPerSample) {
        return overflowEstimate(4);
    }
    const int bytesPerPixel = std::max(4, bands * bytesPerSample);
    if (inputSize.isEmpty() || bands <= 0 || !std::isfinite(scale) || scale <= 0.0) {
        return overflowEstimate(bytesPerPixel);
    }

    const long double scaledWidth = std::ceil(static_cast<long double>(inputSize.width()) * scale);
    const long double scaledHeight = std::ceil(static_cast<long double>(inputSize.height()) * scale);
    const long double intMax = static_cast<long double>(std::numeric_limits<int>::max());
    if (!std::isfinite(scaledWidth)
        || !std::isfinite(scaledHeight)
        || scaledWidth > intMax
        || scaledHeight > intMax) {
        return overflowEstimate(bytesPerPixel);
    }

    const long double bytes = scaledWidth * scaledHeight * static_cast<long double>(bytesPerPixel);
    const long double byteMax = static_cast<long double>(MaximumBudgetableBytes);
    if (!std::isfinite(bytes) || bytes > byteMax) {
        return overflowEstimate(bytesPerPixel);
    }

    RenderMemoryEstimate estimate;
    estimate.outputSize = QSize(static_cast<int>(scaledWidth), static_cast<int>(scaledHeight));
    estimate.bytesPerPixel = bytesPerPixel;
    estimate.bytes = static_cast<qint64>(bytes);
    return estimate;
}

bool exceedsRenderMemoryBudget(QSize inputSize, int bands, double scale, qint64 maxBytes, int bytesPerSample)
{
    if (maxBytes < 0) {
        return true;
    }

    const RenderMemoryEstimate estimate = estimateRenderMemory(inputSize, bands, scale, bytesPerSample);
    return estimate.overflowed || estimate.bytes > maxBytes;
}

} // namespace insidious::core
