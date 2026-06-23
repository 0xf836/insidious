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

    if (entry.source == ImageEntrySource::Archive) {
        const QFileInfo archiveInfo(entry.archivePath);
        if (!archiveInfo.exists()) {
            result.errorMessage = QObject::tr("Archive no longer exists.");
            return result;
        }
        if (!archiveInfo.isFile()) {
            result.errorMessage = QObject::tr("Archive path is not a regular file.");
            return result;
        }
        if (!archiveInfo.isReadable()) {
            result.errorMessage = QObject::tr("Archive is not readable.");
            return result;
        }
        if (entry.archiveEntryPath.isEmpty()) {
            result.errorMessage = QObject::tr("Archive entry is missing.");
            return result;
        }
        return result;
    }

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
