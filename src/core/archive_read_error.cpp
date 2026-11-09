#include "core/archive_read_error.h"

#include <QObject>

#include <archive.h>

namespace insidious::core {

ArchiveReadError archiveReadError(archive *reader, const QString &fallback, const QString &password)
{
    const char *raw = archive_error_string(reader);
    ArchiveReadError error;
    error.message = QString::fromUtf8(raw == nullptr ? "" : raw).trimmed();
    if (error.message.isEmpty()) {
        error.message = fallback;
        return error;
    }

    // shortcut: libarchive exposes encryption failures as text, use public error codes when available.
    const QString text = error.message.toCaseFolded();
    const bool encryption = text.contains(QStringLiteral("encrypt")) || text.contains(QStringLiteral("decrypt"));
    if (encryption && (text.contains(QStringLiteral("not supported"))
            || text.contains(QStringLiteral("not currently supported"))
            || text.contains(QStringLiteral("unsupported")) || text.contains(QStringLiteral("unavailable")))) {
        error.reason = ArchiveEntryReadErrorReason::UnsupportedEncryption;
        error.message = QObject::tr("This archive's encryption is not supported by the installed archive library.");
    } else if (text.contains(QStringLiteral("incorrect passphrase"))
        || text.contains(QStringLiteral("incorrect password"))
        || text.startsWith(QStringLiteral("passphrase required"))
        || text.startsWith(QStringLiteral("password required"))) {
        error.reason = password.isEmpty()
            ? ArchiveEntryReadErrorReason::PasswordRequired : ArchiveEntryReadErrorReason::InvalidPassword;
    }
    return error;
}

ScanErrorReason archiveScanErrorReason(ArchiveEntryReadErrorReason reason)
{
    switch (reason) {
    case ArchiveEntryReadErrorReason::None:
        return ScanErrorReason::None;
    case ArchiveEntryReadErrorReason::Generic:
        return ScanErrorReason::Generic;
    case ArchiveEntryReadErrorReason::PasswordRequired:
        return ScanErrorReason::PasswordRequired;
    case ArchiveEntryReadErrorReason::InvalidPassword:
        return ScanErrorReason::InvalidPassword;
    case ArchiveEntryReadErrorReason::UnsupportedEncryption:
        return ScanErrorReason::UnsupportedEncryption;
    }
    return ScanErrorReason::Generic;
}

} // namespace insidious::core
