#include "core/archive_entry_stream.h"

#include "core/archive_read_error.h"

#include <QFileInfo>
#include <QObject>

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <utility>

namespace insidious::core {
namespace {

QString entryPath(archive_entry *entry)
{
    const char *path = archive_entry_pathname_utf8(entry);
    if (path != nullptr) {
        return QString::fromUtf8(path);
    }

    path = archive_entry_pathname(entry);
    return QString::fromLocal8Bit(path == nullptr ? "" : path);
}

bool openArchive(archive *archiveReader, const QString &archivePath, const QString &password, ArchiveReadError *error)
{
    archive_read_support_filter_all(archiveReader);
    archive_read_support_format_all(archiveReader);

    if (!password.isEmpty()) {
        const QByteArray passwordBytes = password.toUtf8();
        if (archive_read_add_passphrase(archiveReader, passwordBytes.constData()) != ARCHIVE_OK) {
            if (error != nullptr) {
                *error = archiveReadError(archiveReader, QObject::tr("Could not set archive password."), password);
            }
            return false;
        }
    }

    const QByteArray path = QFileInfo(archivePath).absoluteFilePath().toUtf8();
    const int status = archive_read_open_filename(archiveReader, path.constData(), 10240);
    if (status != ARCHIVE_OK) {
        if (error != nullptr) {
            *error = archiveReadError(archiveReader, QObject::tr("Could not open archive."), password);
        }
        return false;
    }

    return true;
}

bool isRegularFile(archive_entry *entry)
{
    return archive_entry_filetype(entry) == AE_IFREG;
}

} // namespace

ArchiveEntryStreamReader::ArchiveEntryStreamReader(ImageEntry entry, QString password)
    : m_entry(std::move(entry))
    , m_password(std::move(password))
{
}

ArchiveEntryStreamReader::~ArchiveEntryStreamReader()
{
    if (m_archive == nullptr) {
        return;
    }

    archive_read_close(m_archive);
    archive_read_free(m_archive);
}

bool ArchiveEntryStreamReader::open()
{
    if (m_entry.source != ImageEntrySource::Archive || m_entry.archivePath.isEmpty() || m_entry.archiveEntryPath.isEmpty()) {
        m_errorMessage = QObject::tr("Invalid archive entry.");
        m_errorReason = ArchiveEntryReadErrorReason::Generic;
        return false;
    }

    m_archive = archive_read_new();
    ArchiveReadError openError;
    if (!openArchive(m_archive, m_entry.archivePath, m_password, &openError)) {
        archive_read_free(m_archive);
        m_archive = nullptr;
        m_errorMessage = openError.message;
        m_errorReason = openError.reason;
        return false;
    }

    archive_entry *archiveEntry = nullptr;
    for (;;) {
        const int headerStatus = archive_read_next_header(m_archive, &archiveEntry);
        if (headerStatus == ARCHIVE_EOF) {
            m_errorMessage = QObject::tr("Archive entry was not found.");
            m_errorReason = ArchiveEntryReadErrorReason::Generic;
            return false;
        }
        if (headerStatus != ARCHIVE_OK) {
            setReadError(QObject::tr("Could not read archive entry."));
            return false;
        }

        if (entryPath(archiveEntry) != m_entry.archiveEntryPath) {
            const int skipStatus = archive_read_data_skip(m_archive);
            if (skipStatus != ARCHIVE_OK && skipStatus != ARCHIVE_EOF) {
                setReadError(QObject::tr("Could not read archive entry."));
                return false;
            }
            continue;
        }

        if (!isRegularFile(archiveEntry)) {
            m_errorMessage = QObject::tr("Archive entry is not a regular file.");
            m_errorReason = ArchiveEntryReadErrorReason::Generic;
            return false;
        }

        const auto headerError = archiveReadError(m_archive, {}, m_password);
        if (archive_entry_is_encrypted(archiveEntry) && headerError.reason == ArchiveEntryReadErrorReason::UnsupportedEncryption) {
            m_errorMessage = headerError.message;
            m_errorReason = headerError.reason;
            return false;
        }

        m_open = true;
        return true;
    }
}

qint64 ArchiveEntryStreamReader::read(char *buffer, qint64 maxBytes)
{
    if (!m_open || m_archive == nullptr || m_eof) {
        return 0;
    }
    if (buffer == nullptr || maxBytes <= 0) {
        return 0;
    }

    constexpr qint64 MaxChunkSize = 1024LL * 1024LL;
    const size_t bytesToRead = static_cast<size_t>(std::min(maxBytes, MaxChunkSize));
    const la_ssize_t bytesRead = archive_read_data(m_archive, buffer, bytesToRead);
    if (bytesRead == 0) {
        m_eof = true;
        return 0;
    }
    if (bytesRead < 0) {
        setReadError(QObject::tr("Could not read archive entry."));
        return -1;
    }

    return bytesRead;
}

QString ArchiveEntryStreamReader::errorMessage() const
{
    return m_errorMessage;
}

ArchiveEntryReadErrorReason ArchiveEntryStreamReader::errorReason() const
{
    return m_errorReason;
}

void ArchiveEntryStreamReader::setReadError(const QString &fallback)
{
    const auto error = archiveReadError(m_archive, fallback, m_password);
    m_errorMessage = error.message;
    m_errorReason = error.reason;
}

} // namespace insidious::core
