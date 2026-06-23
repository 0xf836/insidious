#pragma once

#include "core/folder_collection_source.h"
#include "core/image_entry.h"

#include <QByteArray>
#include <QString>

namespace insidious::core {

enum class ArchiveEntryReadErrorReason {
    None,
    Generic,
    PasswordRequired,
    InvalidPassword,
};

struct ArchiveEntryReadResult {
    QByteArray data;
    QString errorMessage;
    ArchiveEntryReadErrorReason errorReason = ArchiveEntryReadErrorReason::None;

    [[nodiscard]] bool ok() const;
};

[[nodiscard]] ScanResult scanArchive(const QString &archivePath, const QString &password = {});
[[nodiscard]] CollectionTreeResult scanArchiveSourceTree(const QString &archivePath, const QString &password = {});
[[nodiscard]] ArchiveEntryReadResult readArchiveEntry(
    const ImageEntry &entry,
    const QString &password = {},
    qint64 maxBytes = 256LL * 1024LL * 1024LL);

} // namespace insidious::core
