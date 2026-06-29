#include "core/image_sort.h"

#include "core/natural_sort.h"

#include <algorithm>

namespace insidious::core {
namespace {

int compareText(const QString &left, const QString &right)
{
    const int folded = QString::compare(left.toCaseFolded(), right.toCaseFolded());
    if (folded != 0) {
        return folded < 0 ? -1 : 1;
    }
    const int exact = QString::compare(left, right, Qt::CaseSensitive);
    if (exact != 0) {
        return exact < 0 ? -1 : 1;
    }
    return 0;
}

int compareByKey(const ImageEntry &left, const ImageEntry &right, ImageSortKey key)
{
    switch (key) {
    case ImageSortKey::Name:
        return naturalCompare(left.displayName, right.displayName);
    case ImageSortKey::Type: {
        const int result = compareText(left.formatHint, right.formatHint);
        return result != 0 ? result : naturalCompare(left.displayName, right.displayName);
    }
    case ImageSortKey::Size:
        if (left.fileSize != right.fileSize) {
            return left.fileSize < right.fileSize ? -1 : 1;
        }
        return naturalCompare(left.displayName, right.displayName);
    case ImageSortKey::Date: {
        const bool leftValid = left.modifiedTime.isValid();
        const bool rightValid = right.modifiedTime.isValid();
        if (leftValid != rightValid) {
            return leftValid ? -1 : 1;
        }
        if (leftValid && left.modifiedTime != right.modifiedTime) {
            return left.modifiedTime < right.modifiedTime ? -1 : 1;
        }
        return naturalCompare(left.displayName, right.displayName);
    }
    }
    return naturalCompare(left.displayName, right.displayName);
}

} // namespace

ImageSortMode defaultImageSortMode()
{
    return {};
}

void sortImageEntries(QVector<ImageEntry> *entries, ImageSortMode mode)
{
    if (entries == nullptr) {
        return;
    }

    std::stable_sort(entries->begin(), entries->end(), [mode](const ImageEntry &left, const ImageEntry &right) {
        if (mode.key == ImageSortKey::Date) {
            const bool leftValid = left.modifiedTime.isValid();
            const bool rightValid = right.modifiedTime.isValid();
            if (leftValid != rightValid) {
                return leftValid;
            }
        }

        const int result = compareByKey(left, right, mode.key);
        return mode.direction == SortDirection::Ascending ? result < 0 : result > 0;
    });
}

} // namespace insidious::core
