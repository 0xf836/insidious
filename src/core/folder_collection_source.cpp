#include "core/folder_collection_source.h"

#include "core/image_formats.h"
#include "core/natural_sort.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <algorithm>

namespace insidious::core {

bool ScanResult::ok() const
{
    return errorMessage.isEmpty();
}

ImageEntry imageEntryFromFileInfo(const QFileInfo &fileInfo)
{
    ImageEntry entry;
    entry.displayName = fileInfo.fileName();
    entry.absolutePath = fileInfo.absoluteFilePath();
    entry.fileSize = fileInfo.size();
    entry.modifiedTime = fileInfo.lastModified();
    entry.formatHint = formatHintFromSuffix(fileInfo.suffix());
    return entry;
}

ScanResult scanFolder(const QString &folderPath, const QString &selectedPath)
{
    ScanResult result;

    const QFileInfo folderInfo(folderPath);
    if (!folderInfo.exists()) {
        result.errorMessage = QObject::tr("Folder does not exist.");
        return result;
    }
    if (!folderInfo.isDir()) {
        result.errorMessage = QObject::tr("Path is not a folder.");
        return result;
    }
    if (!folderInfo.isReadable()) {
        result.errorMessage = QObject::tr("Folder is not readable.");
        return result;
    }

    const QDir dir(folderInfo.absoluteFilePath());
    const QFileInfoList files = dir.entryInfoList(
        QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDir::NoSort);

    for (const QFileInfo &fileInfo : files) {
        if (fileInfo.isReadable() && isSupportedImageFile(fileInfo)) {
            result.entries.append(imageEntryFromFileInfo(fileInfo));
        }
    }

    std::stable_sort(result.entries.begin(), result.entries.end(), [](const ImageEntry &left, const ImageEntry &right) {
        return naturalLess(left.displayName, right.displayName);
    });

    if (result.entries.isEmpty()) {
        result.errorMessage = QObject::tr("Folder contains no supported images.");
        return result;
    }

    result.selectedIndex = 0;
    if (!selectedPath.isEmpty()) {
        const QString normalizedSelected = QFileInfo(selectedPath).absoluteFilePath();
        for (qsizetype i = 0; i < result.entries.size(); ++i) {
            if (result.entries.at(i).absolutePath == normalizedSelected) {
                result.selectedIndex = static_cast<int>(i);
                break;
            }
        }
    }

    return result;
}

ScanResult singleImageCollection(const QString &filePath)
{
    ScanResult result;
    const QFileInfo fileInfo(filePath);

    if (!fileInfo.exists()) {
        result.errorMessage = QObject::tr("File does not exist.");
        return result;
    }
    if (!fileInfo.isFile()) {
        result.errorMessage = QObject::tr("Path is not a regular file.");
        return result;
    }
    if (!fileInfo.isReadable()) {
        result.errorMessage = QObject::tr("File is not readable.");
        return result;
    }
    if (!isSupportedImageFile(fileInfo)) {
        result.errorMessage = QObject::tr("Unsupported image format.");
        return result;
    }

    result.entries.append(imageEntryFromFileInfo(fileInfo));
    result.selectedIndex = 0;
    return result;
}

} // namespace insidious::core
