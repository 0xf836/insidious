#pragma once

#include "core/folder_collection_source.h"
#include "core/image_entry.h"

#include <QByteArray>
#include <QString>

namespace insidious::core {

struct ArchiveEntryReadResult {
    QByteArray data;
    QString errorMessage;

    [[nodiscard]] bool ok() const;
};

[[nodiscard]] ScanResult scanArchive(const QString &archivePath);
[[nodiscard]] ArchiveEntryReadResult readArchiveEntry(const ImageEntry &entry, qint64 maxBytes = 256LL * 1024LL * 1024LL);

} // namespace insidious::core
