#pragma once

#include "core/archive_collection_source.h"
#include "core/image_entry.h"

#include <QString>

struct archive;

namespace insidious::core {

class ArchiveEntryStreamReader {
public:
    ArchiveEntryStreamReader(ImageEntry entry, QString password = {});
    ~ArchiveEntryStreamReader();

    ArchiveEntryStreamReader(const ArchiveEntryStreamReader &) = delete;
    ArchiveEntryStreamReader &operator=(const ArchiveEntryStreamReader &) = delete;

    [[nodiscard]] bool open();
    [[nodiscard]] qint64 read(char *buffer, qint64 maxBytes);

    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] ArchiveEntryReadErrorReason errorReason() const;

private:
    void setReadError(const QString &fallback);

    ImageEntry m_entry;
    QString m_password;
    archive *m_archive = nullptr;
    QString m_errorMessage;
    ArchiveEntryReadErrorReason m_errorReason = ArchiveEntryReadErrorReason::None;
    bool m_open = false;
    bool m_eof = false;
};

} // namespace insidious::core
