#include "core/image_entry_validation.h"

#include <QFileInfo>
#include <QObject>

namespace insidious::core {

bool EntryValidationResult::ok() const
{
    return errorMessage.isEmpty();
}

EntryValidationResult validateImageEntryLoadable(const ImageEntry &entry)
{
    EntryValidationResult result;

    const QFileInfo fileInfo(entry.absolutePath);
    if (!fileInfo.exists()) {
        result.errorMessage = QObject::tr("File no longer exists.");
        return result;
    }
    if (!fileInfo.isFile()) {
        result.errorMessage = QObject::tr("Path is not a regular file.");
        return result;
    }
    if (!fileInfo.isReadable()) {
        result.errorMessage = QObject::tr("File is not readable.");
        return result;
    }

    return result;
}

} // namespace insidious::core
