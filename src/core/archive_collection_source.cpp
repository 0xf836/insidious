#include "core/archive_collection_source.h"

#include "core/image_formats.h"
#include "core/natural_sort.h"

#include <QDateTime>
#include <QFileInfo>
#include <QObject>

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>

namespace insidious::core {
namespace {

QString archiveError(archive *archiveReader, const QString &fallback)
{
    const char *message = archive_error_string(archiveReader);
    const QString text = QString::fromUtf8(message == nullptr ? "" : message).trimmed();
    return text.isEmpty() ? fallback : text;
}

QString entryPath(archive_entry *entry)
{
    const char *path = archive_entry_pathname_utf8(entry);
    if (path != nullptr) {
        return QString::fromUtf8(path);
    }

    path = archive_entry_pathname(entry);
    return QString::fromLocal8Bit(path == nullptr ? "" : path);
}

bool openArchive(archive *archiveReader, const QString &archivePath, QString *errorMessage)
{
    archive_read_support_filter_all(archiveReader);
    archive_read_support_format_all(archiveReader);

    const QByteArray path = QFileInfo(archivePath).absoluteFilePath().toUtf8();
    const int status = archive_read_open_filename(archiveReader, path.constData(), 10240);
    if (status != ARCHIVE_OK) {
        if (errorMessage != nullptr) {
            *errorMessage = archiveError(archiveReader, QObject::tr("Could not open archive."));
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

bool ArchiveEntryReadResult::ok() const
{
    return errorMessage.isEmpty();
}

ScanResult scanArchive(const QString &archivePath)
{
    ScanResult result;

    const QFileInfo archiveInfo(archivePath);
    if (!archiveInfo.exists()) {
        result.errorMessage = QObject::tr("Archive does not exist.");
        return result;
    }
    if (!archiveInfo.isFile()) {
        result.errorMessage = QObject::tr("Path is not a regular file.");
        return result;
    }
    if (!archiveInfo.isReadable()) {
        result.errorMessage = QObject::tr("Archive is not readable.");
        return result;
    }

    archive *archiveReader = archive_read_new();
    QString openError;
    if (!openArchive(archiveReader, archiveInfo.absoluteFilePath(), &openError)) {
        archive_read_free(archiveReader);
        result.errorMessage = openError;
        return result;
    }

    archive_entry *entry = nullptr;
    while (archive_read_next_header(archiveReader, &entry) == ARCHIVE_OK) {
        const QString path = entryPath(entry);
        if (isRegularFile(entry) && isSupportedImageExtension(QFileInfo(path).suffix())) {
            ImageEntry imageEntry;
            imageEntry.source = ImageEntrySource::Archive;
            imageEntry.displayName = path;
            imageEntry.archivePath = archiveInfo.absoluteFilePath();
            imageEntry.archiveEntryPath = path;
            imageEntry.absolutePath = QStringLiteral("%1::%2").arg(imageEntry.archivePath, imageEntry.archiveEntryPath);
            imageEntry.fileSize = archive_entry_size_is_set(entry) ? archive_entry_size(entry) : 0;
            const time_t modifiedTime = archive_entry_mtime(entry);
            if (modifiedTime > 0) {
                imageEntry.modifiedTime = QDateTime::fromSecsSinceEpoch(modifiedTime);
            }
            imageEntry.formatHint = formatHintFromSuffix(QFileInfo(path).suffix());
            result.entries.append(imageEntry);
        }

        archive_read_data_skip(archiveReader);
    }

    if (archive_errno(archiveReader) != ARCHIVE_OK && archive_errno(archiveReader) != ARCHIVE_EOF) {
        result.errorMessage = archiveError(archiveReader, QObject::tr("Could not read archive."));
    }

    archive_read_close(archiveReader);
    archive_read_free(archiveReader);

    if (!result.errorMessage.isEmpty()) {
        return result;
    }

    std::stable_sort(result.entries.begin(), result.entries.end(), [](const ImageEntry &left, const ImageEntry &right) {
        return naturalLess(left.displayName, right.displayName);
    });

    if (result.entries.isEmpty()) {
        result.errorMessage = QObject::tr("Archive contains no supported images.");
        return result;
    }

    result.selectedIndex = 0;
    return result;
}

ArchiveEntryReadResult readArchiveEntry(const ImageEntry &entry, qint64 maxBytes)
{
    ArchiveEntryReadResult result;
    if (entry.source != ImageEntrySource::Archive || entry.archivePath.isEmpty() || entry.archiveEntryPath.isEmpty()) {
        result.errorMessage = QObject::tr("Invalid archive entry.");
        return result;
    }

    archive *archiveReader = archive_read_new();
    QString openError;
    if (!openArchive(archiveReader, entry.archivePath, &openError)) {
        archive_read_free(archiveReader);
        result.errorMessage = openError;
        return result;
    }

    archive_entry *archiveEntry = nullptr;
    bool found = false;
    while (archive_read_next_header(archiveReader, &archiveEntry) == ARCHIVE_OK) {
        if (entryPath(archiveEntry) != entry.archiveEntryPath) {
            archive_read_data_skip(archiveReader);
            continue;
        }

        found = true;
        if (!isRegularFile(archiveEntry)) {
            result.errorMessage = QObject::tr("Archive entry is not a regular file.");
            break;
        }

        if (archive_entry_size_is_set(archiveEntry) && archive_entry_size(archiveEntry) > maxBytes) {
            result.errorMessage = QObject::tr("Archive entry is too large to read.");
            break;
        }

        std::array<char, 64 * 1024> buffer;
        for (;;) {
            const la_ssize_t bytesRead = archive_read_data(archiveReader, buffer.data(), buffer.size());
            if (bytesRead == 0) {
                break;
            }
            if (bytesRead < 0) {
                result.errorMessage = archiveError(archiveReader, QObject::tr("Could not read archive entry."));
                break;
            }
            if (result.data.size() + bytesRead > maxBytes) {
                result.data.clear();
                result.errorMessage = QObject::tr("Archive entry is too large to read.");
                break;
            }
            result.data.append(buffer.data(), static_cast<qsizetype>(bytesRead));
        }
        break;
    }

    if (!found && result.errorMessage.isEmpty()) {
        result.errorMessage = QObject::tr("Archive entry was not found.");
    }

    archive_read_close(archiveReader);
    archive_read_free(archiveReader);
    return result;
}

} // namespace insidious::core
