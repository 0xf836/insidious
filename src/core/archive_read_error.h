#pragma once

#include "core/archive_collection_source.h"

struct archive;

namespace insidious::core {

struct ArchiveReadError {
    QString message;
    ArchiveEntryReadErrorReason reason = ArchiveEntryReadErrorReason::Generic;
};

[[nodiscard]] ArchiveReadError archiveReadError(archive *reader, const QString &fallback, const QString &password);
[[nodiscard]] ScanErrorReason archiveScanErrorReason(ArchiveEntryReadErrorReason reason);

} // namespace insidious::core
