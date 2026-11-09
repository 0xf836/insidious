#include "core/archive_collection_source.h"

#include "core/archive_read_error.h"
#include "core/image_formats.h"
#include "core/image_sort.h"
#include "core/natural_sort.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QObject>
#include <QtGlobal>

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>

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

QString cleanArchiveRelativePath(QString path)
{
    path.replace(QChar('\\'), QChar('/'));
    while (path.startsWith(QChar('/'))) {
        path.remove(0, 1);
    }
    while (path.endsWith(QChar('/'))) {
        path.chop(1);
    }
    return path == QLatin1String(".") ? QString() : path;
}

QString archiveFolderPath(const QString &entryPath)
{
    const QString folder = QFileInfo(entryPath).path();
    return folder == QLatin1String(".") ? QString() : cleanArchiveRelativePath(folder);
}

QString archiveFolderDisplayName(const QString &relativePath, const QString &rootName)
{
    if (relativePath.isEmpty()) {
        return rootName;
    }
    return relativePath.section(QChar('/'), -1);
}

std::shared_ptr<CollectionFolderNode> ensureArchiveFolder(
    QHash<QString, std::shared_ptr<CollectionFolderNode>> *folders,
    const QString &relativePath,
    const QString &rootName)
{
    const QString normalized = cleanArchiveRelativePath(relativePath);
    if (folders->contains(normalized)) {
        return folders->value(normalized);
    }

    const QString parentPath = archiveFolderPath(normalized);
    std::shared_ptr<CollectionFolderNode> parent = ensureArchiveFolder(folders, parentPath, rootName);
    auto folder = std::make_shared<CollectionFolderNode>();
    folder->relativePath = normalized;
    folder->displayName = archiveFolderDisplayName(normalized, rootName);
    folder->loaded = true;
    folder->canLoadChildren = false;
    folder->parent = parent.get();
    parent->children.append(folder);
    folders->insert(normalized, folder);
    return folder;
}

void sortArchiveFolderTree(const std::shared_ptr<CollectionFolderNode> &folder)
{
    std::stable_sort(folder->children.begin(), folder->children.end(), [](const auto &left, const auto &right) {
        return naturalLess(left->displayName, right->displayName);
    });
    sortImageEntries(&folder->images);
    for (const auto &child : folder->children) {
        sortArchiveFolderTree(child);
    }
}

const CollectionFolderNode *firstFolderWithImages(const std::shared_ptr<CollectionFolderNode> &folder)
{
    if (!folder->images.isEmpty()) {
        return folder.get();
    }
    for (const auto &child : folder->children) {
        if (const CollectionFolderNode *candidate = firstFolderWithImages(child)) {
            return candidate;
        }
    }
    return nullptr;
}

void setScanArchiveReadError(ScanResult *result, archive *archiveReader, const QString &password)
{
    const auto error = archiveReadError(archiveReader, QObject::tr("Could not read archive."), password);
    result->errorMessage = error.message;
    result->errorReason = archiveScanErrorReason(error.reason);
}

bool validateEncryptedEntryPassword(ScanResult *result, archive *archiveReader, const QString &password)
{
    std::array<char, 64 * 1024> buffer;
    for (;;) {
        const la_ssize_t bytesRead = archive_read_data(archiveReader, buffer.data(), buffer.size());
        if (bytesRead == 0) {
            return true;
        }
        if (bytesRead < 0) {
            setScanArchiveReadError(result, archiveReader, password);
            return false;
        }
    }
}

void setArchiveEntryReadError(ArchiveEntryReadResult *result, archive *archiveReader, const QString &password)
{
    const auto error = archiveReadError(archiveReader, QObject::tr("Could not read archive entry."), password);
    result->data.clear();
    result->errorMessage = error.message;
    result->errorReason = error.reason;
}

} // namespace

bool ArchiveEntryReadResult::ok() const
{
    return errorMessage.isEmpty();
}

ScanResult scanArchive(const QString &archivePath, const QString &password)
{
    ScanResult result;

    const QFileInfo archiveInfo(archivePath);
    if (!archiveInfo.exists()) {
        result.errorMessage = QObject::tr("Archive does not exist.");
        result.errorReason = ScanErrorReason::Generic;
        return result;
    }
    if (!archiveInfo.isFile()) {
        result.errorMessage = QObject::tr("Path is not a regular file.");
        result.errorReason = ScanErrorReason::Generic;
        return result;
    }
    if (!archiveInfo.isReadable()) {
        result.errorMessage = QObject::tr("Archive is not readable.");
        result.errorReason = ScanErrorReason::Generic;
        return result;
    }

    archive *archiveReader = archive_read_new();
    ArchiveReadError openError;
    if (!openArchive(archiveReader, archiveInfo.absoluteFilePath(), password, &openError)) {
        archive_read_free(archiveReader);
        result.errorMessage = openError.message;
        result.errorReason = archiveScanErrorReason(openError.reason);
        return result;
    }

    archive_entry *entry = nullptr;
    bool passwordValidated = false;
    for (;;) {
        const int headerStatus = archive_read_next_header(archiveReader, &entry);
        if (headerStatus == ARCHIVE_EOF) {
            break;
        }
        if (headerStatus != ARCHIVE_OK) {
            setScanArchiveReadError(&result, archiveReader, password);
            break;
        }

        const QString path = entryPath(entry);
        if (isRegularFile(entry) && isSupportedImageExtension(QFileInfo(path).suffix())) {
            // RAR4 can report unsupported encryption even with a successful header read.
            const auto headerError = archiveReadError(archiveReader, {}, password);
            if (archive_entry_is_encrypted(entry) && headerError.reason == ArchiveEntryReadErrorReason::UnsupportedEncryption) {
                result.errorMessage = headerError.message;
                result.errorReason = ScanErrorReason::UnsupportedEncryption;
                break;
            }

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

            if (archive_entry_is_encrypted(entry) && !passwordValidated) {
                if (!validateEncryptedEntryPassword(&result, archiveReader, password)) {
                    break;
                }
                passwordValidated = true;
                continue;
            }
        }

        const int skipStatus = archive_read_data_skip(archiveReader);
        if (skipStatus != ARCHIVE_OK && skipStatus != ARCHIVE_EOF) {
            setScanArchiveReadError(&result, archiveReader, password);
            break;
        }
    }

    archive_read_close(archiveReader);
    archive_read_free(archiveReader);

    if (!result.errorMessage.isEmpty()) {
        return result;
    }

    sortImageEntries(&result.entries);

    if (result.entries.isEmpty()) {
        result.errorMessage = QObject::tr("Archive contains no supported images.");
        result.errorReason = ScanErrorReason::Generic;
        return result;
    }

    result.selectedIndex = 0;
    return result;
}

CollectionTreeResult scanArchiveSourceTree(const QString &archivePath, const QString &password)
{
    CollectionTreeResult result;

    const QFileInfo archiveInfo(archivePath);
    if (!archiveInfo.exists()) {
        result.errorMessage = QObject::tr("Archive does not exist.");
        result.errorReason = ScanErrorReason::Generic;
        return result;
    }
    if (!archiveInfo.isFile()) {
        result.errorMessage = QObject::tr("Path is not a regular file.");
        result.errorReason = ScanErrorReason::Generic;
        return result;
    }
    if (!archiveInfo.isReadable()) {
        result.errorMessage = QObject::tr("Archive is not readable.");
        result.errorReason = ScanErrorReason::Generic;
        return result;
    }

    archive *archiveReader = archive_read_new();
    ArchiveReadError openError;
    if (!openArchive(archiveReader, archiveInfo.absoluteFilePath(), password, &openError)) {
        archive_read_free(archiveReader);
        result.errorMessage = openError.message;
        result.errorReason = archiveScanErrorReason(openError.reason);
        return result;
    }

    result.rootPath = archiveInfo.absoluteFilePath();
    const QString rootName = archiveInfo.fileName();
    auto root = std::make_shared<CollectionFolderNode>();
    root->relativePath = QString();
    root->displayName = rootName;
    root->loaded = true;
    root->canLoadChildren = false;

    QHash<QString, std::shared_ptr<CollectionFolderNode>> folders;
    folders.insert(QString(), root);

    archive_entry *entry = nullptr;
    bool passwordValidated = false;
    for (;;) {
        const int headerStatus = archive_read_next_header(archiveReader, &entry);
        if (headerStatus == ARCHIVE_EOF) {
            break;
        }
        if (headerStatus != ARCHIVE_OK) {
            const auto error = archiveReadError(archiveReader, QObject::tr("Could not read archive."), password);
            result.errorMessage = error.message;
            result.errorReason = archiveScanErrorReason(error.reason);
            break;
        }

        const QString path = cleanArchiveRelativePath(entryPath(entry));
        if (isRegularFile(entry) && isSupportedImageExtension(QFileInfo(path).suffix())) {
            // RAR4 can report unsupported encryption even with a successful header read.
            const auto headerError = archiveReadError(archiveReader, {}, password);
            if (archive_entry_is_encrypted(entry) && headerError.reason == ArchiveEntryReadErrorReason::UnsupportedEncryption) {
                result.errorMessage = headerError.message;
                result.errorReason = ScanErrorReason::UnsupportedEncryption;
                break;
            }

            const QString folderPath = archiveFolderPath(path);
            auto folder = ensureArchiveFolder(&folders, folderPath, rootName);

            ImageEntry imageEntry;
            imageEntry.source = ImageEntrySource::Archive;
            imageEntry.displayName = QFileInfo(path).fileName();
            imageEntry.sourceRelativePath = path;
            imageEntry.sourceFolderPath = folderPath;
            imageEntry.archivePath = archiveInfo.absoluteFilePath();
            imageEntry.archiveEntryPath = path;
            imageEntry.absolutePath = QStringLiteral("%1::%2").arg(imageEntry.archivePath, imageEntry.archiveEntryPath);
            imageEntry.fileSize = archive_entry_size_is_set(entry) ? archive_entry_size(entry) : 0;
            const time_t modifiedTime = archive_entry_mtime(entry);
            if (modifiedTime > 0) {
                imageEntry.modifiedTime = QDateTime::fromSecsSinceEpoch(modifiedTime);
            }
            imageEntry.formatHint = formatHintFromSuffix(QFileInfo(path).suffix());
            folder->images.append(imageEntry);

            if (archive_entry_is_encrypted(entry) && !passwordValidated) {
                ScanResult validationResult;
                if (!validateEncryptedEntryPassword(&validationResult, archiveReader, password)) {
                    result.errorMessage = validationResult.errorMessage;
                    result.errorReason = validationResult.errorReason;
                    break;
                }
                passwordValidated = true;
                continue;
            }
        }

        const int skipStatus = archive_read_data_skip(archiveReader);
        if (skipStatus != ARCHIVE_OK && skipStatus != ARCHIVE_EOF) {
            const auto error = archiveReadError(archiveReader, QObject::tr("Could not read archive."), password);
            result.errorMessage = error.message;
            result.errorReason = archiveScanErrorReason(error.reason);
            break;
        }
    }

    archive_read_close(archiveReader);
    archive_read_free(archiveReader);

    if (!result.errorMessage.isEmpty()) {
        return result;
    }

    sortArchiveFolderTree(root);
    const CollectionFolderNode *selectedFolder = firstFolderWithImages(root);
    if (selectedFolder == nullptr) {
        result.errorMessage = QObject::tr("Archive contains no supported images.");
        result.errorReason = ScanErrorReason::Generic;
        return result;
    }

    result.root = root;
    result.selectedFolderPath = selectedFolder->relativePath;
    result.selectedImageIndex = 0;
    return result;
}

ArchiveEntryReadResult readArchiveEntry(const ImageEntry &entry, const QString &password, qint64 maxBytes)
{
    ArchiveEntryReadResult result;
    if (entry.source != ImageEntrySource::Archive || entry.archivePath.isEmpty() || entry.archiveEntryPath.isEmpty()) {
        result.errorMessage = QObject::tr("Invalid archive entry.");
        result.errorReason = ArchiveEntryReadErrorReason::Generic;
        return result;
    }

    archive *archiveReader = archive_read_new();
    ArchiveReadError openError;
    if (!openArchive(archiveReader, entry.archivePath, password, &openError)) {
        archive_read_free(archiveReader);
        result.errorMessage = openError.message;
        result.errorReason = openError.reason;
        return result;
    }

    archive_entry *archiveEntry = nullptr;
    bool found = false;
    for (;;) {
        const int headerStatus = archive_read_next_header(archiveReader, &archiveEntry);
        if (headerStatus == ARCHIVE_EOF) {
            break;
        }
        if (headerStatus != ARCHIVE_OK) {
            setArchiveEntryReadError(&result, archiveReader, password);
            break;
        }

        if (entryPath(archiveEntry) != entry.archiveEntryPath) {
            const int skipStatus = archive_read_data_skip(archiveReader);
            if (skipStatus != ARCHIVE_OK && skipStatus != ARCHIVE_EOF) {
                setArchiveEntryReadError(&result, archiveReader, password);
                break;
            }
            continue;
        }

        found = true;
        if (!isRegularFile(archiveEntry)) {
            result.errorMessage = QObject::tr("Archive entry is not a regular file.");
            result.errorReason = ArchiveEntryReadErrorReason::Generic;
            break;
        }

        const auto headerError = archiveReadError(archiveReader, {}, password);
        if (archive_entry_is_encrypted(archiveEntry) && headerError.reason == ArchiveEntryReadErrorReason::UnsupportedEncryption) {
            result.errorMessage = headerError.message;
            result.errorReason = headerError.reason;
            break;
        }

        if (archive_entry_size_is_set(archiveEntry) && archive_entry_size(archiveEntry) > maxBytes) {
            result.errorMessage = QObject::tr("Archive entry is too large to read.");
            result.errorReason = ArchiveEntryReadErrorReason::Generic;
            break;
        }

        std::array<char, 64 * 1024> buffer;
        for (;;) {
            const la_ssize_t bytesRead = archive_read_data(archiveReader, buffer.data(), buffer.size());
            if (bytesRead == 0) {
                break;
            }
            if (bytesRead < 0) {
                setArchiveEntryReadError(&result, archiveReader, password);
                break;
            }
            if (result.data.size() + bytesRead > maxBytes) {
                result.data.clear();
                result.errorMessage = QObject::tr("Archive entry is too large to read.");
                result.errorReason = ArchiveEntryReadErrorReason::Generic;
                break;
            }
            result.data.append(buffer.data(), static_cast<qsizetype>(bytesRead));
        }
        break;
    }

    if (!found && result.errorMessage.isEmpty()) {
        result.errorMessage = QObject::tr("Archive entry was not found.");
        result.errorReason = ArchiveEntryReadErrorReason::Generic;
    }

    archive_read_close(archiveReader);
    archive_read_free(archiveReader);
    return result;
}

} // namespace insidious::core
