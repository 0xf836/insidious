#pragma once

#include "core/decoded_image.h"
#include "core/image_entry.h"
#include "core/viewer_controller.h"

#include <QString>

namespace insidious::ui {

struct StatusLineContext {
    const core::ImageEntry *entry = nullptr;
    const core::DecodedImage *decoded = nullptr;
    core::ZoomMode zoomMode = core::ZoomMode::FitToWindow;
    double zoomFactor = 1.0;
    int currentIndex = -1;
    int totalCount = 0;
    QString message;
};

[[nodiscard]] QString formatStatusLine(const StatusLineContext &context);

} // namespace insidious::ui
