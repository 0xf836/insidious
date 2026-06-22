#include "core/image_entry.h"

namespace insidious::core {

bool ImageEntry::isValid() const
{
    return !absolutePath.isEmpty();
}

QString ImageEntry::identityKey() const
{
    return QStringLiteral("%1|%2|%3")
        .arg(absolutePath,
             QString::number(fileSize),
             QString::number(modifiedTime.toMSecsSinceEpoch()));
}

} // namespace insidious::core
