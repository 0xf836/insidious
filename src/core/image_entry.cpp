#include "core/image_entry.h"

namespace insidious::core {

bool ImageEntry::isValid() const
{
    return !absolutePath.isEmpty();
}

QString ImageEntry::identityKey() const
{
    if (source == ImageEntrySource::Archive) {
        return QStringLiteral("archive|%1|%2|%3|%4|%5")
            .arg(archivePath,
                 absolutePath,
                 QString::number(fileSize),
                 QString::number(modifiedTime.toMSecsSinceEpoch()),
                 archiveEntryPath);
    }

    return QStringLiteral("file|%1|%2|%3")
        .arg(absolutePath,
             QString::number(fileSize),
             QString::number(modifiedTime.toMSecsSinceEpoch()));
}

} // namespace insidious::core
