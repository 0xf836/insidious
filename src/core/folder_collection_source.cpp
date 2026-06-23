#include "core/folder_collection_source.h"

#include "core/image_formats.h"
#include "core/natural_sort.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <algorithm>

namespace insidious::core {
namespace {

QString cleanRelativePath(QString path)
{
    path.replace(QChar('\\'), QChar('/'));
    while (path.startsWith(QLatin1String("./"))) {
        path.remove(0, 2);
    }
    while (path.startsWith(QChar('/'))) {
        path.remove(0, 1);
    }
    while (path.endsWith(QChar('/'))) {
        path.chop(1);
    }
    return path == QLatin1String(".") ? QString() : path;
}

QString absolutePathForRelativeFolder(const QString &rootPath, const QString &folderRelativePath)
{
    if (folderRelativePath.isEmpty()) {
        return QFileInfo(rootPath).absoluteFilePath();
    }
    return QDir(QFileInfo(rootPath).absoluteFilePath()).filePath(folderRelativePath);
}

QString relativePathFromRoot(const QString &rootPath, const QString &absolutePath)
{
    const QString relative = QDir(QFileInfo(rootPath).absoluteFilePath()).relativeFilePath(QFileInfo(absolutePath).absoluteFilePath());
    return cleanRelativePath(relative);
}

void sortFolderNodes(QVector<std::shared_ptr<CollectionFolderNode>> *nodes)
{
    std::stable_sort(nodes->begin(), nodes->end(), [](const auto &left, const auto &right) {
        return naturalLess(left->displayName, right->displayName);
    });
}

void sortImageEntries(QVector<ImageEntry> *entries)
{
    std::stable_sort(entries->begin(), entries->end(), [](const ImageEntry &left, const ImageEntry &right) {
        return naturalLess(left.displayName, right.displayName);
    });
}

} // namespace

bool ScanResult::ok() const
{
    return errorMessage.isEmpty();
}

bool CollectionTreeResult::ok() const
{
    return errorMessage.isEmpty();
}

bool FolderLoadResult::ok() const
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

FolderLoadResult loadFileSystemFolder(const QString &rootPath, const QString &folderRelativePath)
{
    FolderLoadResult result;
    result.rootPath = QFileInfo(rootPath).absoluteFilePath();
    result.folderRelativePath = cleanRelativePath(folderRelativePath);

    const QString folderPath = absolutePathForRelativeFolder(result.rootPath, result.folderRelativePath);
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
    const QFileInfoList directories = dir.entryInfoList(
        QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDir::NoSort);
    for (const QFileInfo &directoryInfo : directories) {
        if (!directoryInfo.isReadable()) {
            continue;
        }

        auto child = std::make_shared<CollectionFolderNode>();
        child->displayName = directoryInfo.fileName();
        child->relativePath = cleanRelativePath(QDir(QFileInfo(result.rootPath).absoluteFilePath()).relativeFilePath(directoryInfo.absoluteFilePath()));
        child->canLoadChildren = true;
        result.children.append(child);
    }
    sortFolderNodes(&result.children);

    const QFileInfoList files = dir.entryInfoList(
        QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDir::NoSort);
    for (const QFileInfo &fileInfo : files) {
        if (!fileInfo.isReadable() || !isSupportedImageFile(fileInfo)) {
            continue;
        }

        ImageEntry entry = imageEntryFromFileInfo(fileInfo);
        entry.sourceRelativePath = relativePathFromRoot(result.rootPath, fileInfo.absoluteFilePath());
        entry.sourceFolderPath = result.folderRelativePath;
        result.images.append(entry);
    }
    sortImageEntries(&result.images);

    return result;
}

CollectionTreeResult scanFileSystemSourceTree(const QString &folderPath, const QString &selectedPath)
{
    CollectionTreeResult result;
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

    result.rootPath = folderInfo.absoluteFilePath();
    const FolderLoadResult rootLoad = loadFileSystemFolder(result.rootPath, QString());
    if (!rootLoad.ok()) {
        result.errorMessage = rootLoad.errorMessage;
        return result;
    }

    result.root = std::make_shared<CollectionFolderNode>();
    result.root->relativePath = QString();
    result.root->displayName = folderInfo.fileName().isEmpty() ? result.rootPath : folderInfo.fileName();
    result.root->images = rootLoad.images;
    result.root->children = rootLoad.children;
    result.root->loaded = true;
    result.root->canLoadChildren = true;
    for (const auto &child : result.root->children) {
        child->parent = result.root.get();
    }

    result.selectedFolderPath = QString();
    result.selectedImageIndex = result.root->images.isEmpty() ? -1 : 0;
    if (!selectedPath.isEmpty()) {
        const QString relativeSelected = relativePathFromRoot(result.rootPath, selectedPath);
        const QString selectedFolder = cleanRelativePath(QFileInfo(relativeSelected).path());
        if (selectedFolder.isEmpty() || selectedFolder == QLatin1String(".")) {
            for (qsizetype i = 0; i < result.root->images.size(); ++i) {
                if (result.root->images.at(i).absolutePath == QFileInfo(selectedPath).absoluteFilePath()) {
                    result.selectedImageIndex = static_cast<int>(i);
                    break;
                }
            }
        } else {
            result.selectedFolderPath = selectedFolder;
            result.selectedImageIndex = -1;
        }
    }

    if (result.root->images.isEmpty() && result.root->children.isEmpty()) {
        result.errorMessage = QObject::tr("Folder contains no supported images or folders.");
        return result;
    }

    return result;
}

} // namespace insidious::core
