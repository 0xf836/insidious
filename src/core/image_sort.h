#pragma once

#include "core/image_entry.h"

#include <QVector>

namespace insidious::core {

enum class ImageSortKey {
    Name,
    Type,
    Size,
    Date,
};

enum class SortDirection {
    Ascending,
    Descending,
};

struct ImageSortMode {
    ImageSortKey key = ImageSortKey::Name;
    SortDirection direction = SortDirection::Ascending;
};

[[nodiscard]] ImageSortMode defaultImageSortMode();
void sortImageEntries(QVector<ImageEntry> *entries, ImageSortMode mode = defaultImageSortMode());

} // namespace insidious::core
