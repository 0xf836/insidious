#pragma once

#include "core/image_entry.h"

#include <QString>

namespace insidious::core {

struct EntryValidationResult {
    QString errorMessage;

    [[nodiscard]] bool ok() const;
};

[[nodiscard]] EntryValidationResult validateImageEntryLoadable(const ImageEntry &entry);

} // namespace insidious::core
