#include "core/multi_collection_source.h"

#include "core/archive_collection_source.h"
#include "core/image_formats.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>

namespace insidious::core {
namespace {

QString joinPath(const QString &left, const QString &right)
{
    if (left.isEmpty()) {
        return right;
    }
    if (right.isEmpty()) {
        return left;
    }
    return left + QLatin1Char('/') + right;
}

QString indexedPath(const QString &prefix, qsizetype index)
{
    return QStringLiteral("%1-%2").arg(prefix, QString::number(index + 1));
}

QString sourceDisplayName(const QFileInfo &info)
{
    return info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName();
}

void assignParents(const std::shared_ptr<CollectionFolderNode> &node)
{
    if (node == nullptr) {
        return;
    }
    for (const auto &child : node->children) {
        child->parent = node.get();
        assignParents(child);
    }
}

void rebaseFileSystemNode(
    const std::shared_ptr<CollectionFolderNode> &node,
    const QString &rootPath,
    const QString &prefix,
    const QString &folderRelativePath)
{
    if (node == nullptr) {
        return;
    }

    node->sourceTreePrefix = prefix;
    node->loadRootPath = rootPath;
    node->loadFolderRelativePath = folderRelativePath;
    node->relativePath = joinPath(prefix, folderRelativePath);
    for (ImageEntry &entry : node->images) {
        entry.sourceFolderPath = node->relativePath;
        entry.sourceRelativePath = joinPath(node->relativePath, entry.displayName);
    }

    for (const auto &child : node->children) {
        const QString childFolder = child->loadFolderRelativePath.isEmpty()
            ? child->relativePath
            : child->loadFolderRelativePath;
        rebaseFileSystemNode(child, rootPath, prefix, childFolder);
        child->parent = node.get();
    }
}

void rebaseArchiveNode(
    const std::shared_ptr<CollectionFolderNode> &node,
    const QString &prefix,
    const QString &archiveName,
    const QString &folderRelativePath)
{
    if (node == nullptr) {
        return;
    }

    node->relativePath = joinPath(prefix, folderRelativePath);
    for (ImageEntry &entry : node->images) {
        entry.sourceFolderPath = node->relativePath;
        entry.sourceRelativePath = joinPath(archiveName, entry.archiveEntryPath);
    }

    for (const auto &child : node->children) {
        const QString childRelative = child->relativePath;
        rebaseArchiveNode(child, prefix, archiveName, childRelative);
        child->parent = node.get();
    }
}

const CollectionFolderNode *firstNavigableFolder(const std::shared_ptr<CollectionFolderNode> &node)
{
    if (node == nullptr) {
        return nullptr;
    }
    if (!node->images.isEmpty() || (node->canLoadChildren && !node->loaded)) {
        return node.get();
    }
    for (const auto &child : node->children) {
        if (const CollectionFolderNode *candidate = firstNavigableFolder(child)) {
            return candidate;
        }
    }
    return nullptr;
}

void fail(CollectionTreeResult *result, const QString &message, ScanErrorReason reason = ScanErrorReason::Generic)
{
    result->root.reset();
    result->rootPath.clear();
    result->selectedFolderPath.clear();
    result->selectedImageIndex = -1;
    result->errorMessage = message;
    result->errorReason = reason;
}

} // namespace

CollectionTreeResult fileListSourceTree(const QStringList &filePaths)
{
    CollectionTreeResult result;
    if (filePaths.isEmpty()) {
        fail(&result, QObject::tr("No image files were selected."));
        return result;
    }

    auto root = std::make_shared<CollectionFolderNode>();
    root->displayName = QObject::tr("Selected files");
    root->relativePath = QString();
    root->loaded = true;
    root->canLoadChildren = false;
    root->preserveImageOrder = true;

    for (const QString &path : filePaths) {
        const QFileInfo fileInfo(path);
        if (!fileInfo.exists()) {
            fail(&result, QObject::tr("File does not exist: %1").arg(path));
            return result;
        }
        if (!fileInfo.isFile()) {
            fail(&result, QObject::tr("Path is not a regular file: %1").arg(path));
            return result;
        }
        if (!fileInfo.isReadable()) {
            fail(&result, QObject::tr("File is not readable: %1").arg(path));
            return result;
        }
        if (!isSupportedImageFile(fileInfo)) {
            fail(&result, QObject::tr("Unsupported image format: %1").arg(path));
            return result;
        }

        ImageEntry entry = imageEntryFromFileInfo(fileInfo);
        entry.sourceRelativePath = entry.absolutePath;
        root->images.append(entry);
    }

    result.root = root;
    result.rootPath = QStringLiteral("selected-files");
    result.selectedFolderPath = QString();
    result.selectedImageIndex = 0;
    return result;
}

CollectionTreeResult folderListSourceTree(const QStringList &folderPaths)
{
    CollectionTreeResult result;
    if (folderPaths.isEmpty()) {
        fail(&result, QObject::tr("No folders were selected."));
        return result;
    }

    auto root = std::make_shared<CollectionFolderNode>();
    root->displayName = QObject::tr("Selected folders");
    root->relativePath = QString();
    root->loaded = true;
    root->canLoadChildren = false;

    for (qsizetype index = 0; index < folderPaths.size(); ++index) {
        const QFileInfo folderInfo(folderPaths.at(index));
        if (!folderInfo.exists()) {
            fail(&result, QObject::tr("Folder does not exist: %1").arg(folderPaths.at(index)));
            return result;
        }
        if (!folderInfo.isDir()) {
            fail(&result, QObject::tr("Path is not a folder: %1").arg(folderPaths.at(index)));
            return result;
        }
        if (!folderInfo.isReadable()) {
            fail(&result, QObject::tr("Folder is not readable: %1").arg(folderPaths.at(index)));
            return result;
        }

        FolderLoadResult load = loadFileSystemFolder(folderInfo.absoluteFilePath(), QString());
        if (!load.ok()) {
            fail(&result, QObject::tr("%1: %2").arg(sourceDisplayName(folderInfo), load.errorMessage));
            return result;
        }

        auto child = std::make_shared<CollectionFolderNode>();
        child->displayName = sourceDisplayName(folderInfo);
        child->images = load.images;
        child->children = load.children;
        child->loaded = true;
        child->canLoadChildren = true;
        child->parent = root.get();

        const QString prefix = indexedPath(QStringLiteral("folder"), index);
        rebaseFileSystemNode(child, folderInfo.absoluteFilePath(), prefix, QString());
        root->children.append(child);
    }

    assignParents(root);

    const CollectionFolderNode *selected = firstNavigableFolder(root);
    if (selected == nullptr) {
        fail(&result, QObject::tr("Selected folders contain no supported images or folders."));
        return result;
    }

    result.root = root;
    result.rootPath = QStringLiteral("selected-folders");
    result.selectedFolderPath = selected->relativePath;
    result.selectedImageIndex = selected->images.isEmpty() ? -1 : 0;
    return result;
}

CollectionTreeResult archiveListSourceTree(const QStringList &archivePaths, const QString &password)
{
    CollectionTreeResult result;
    if (archivePaths.isEmpty()) {
        fail(&result, QObject::tr("No archives were selected."));
        return result;
    }

    auto root = std::make_shared<CollectionFolderNode>();
    root->displayName = QObject::tr("Selected archives");
    root->relativePath = QString();
    root->loaded = true;
    root->canLoadChildren = false;

    for (qsizetype index = 0; index < archivePaths.size(); ++index) {
        const QFileInfo archiveInfo(archivePaths.at(index));
        if (!archiveInfo.exists()) {
            fail(&result, QObject::tr("Archive does not exist: %1").arg(archivePaths.at(index)));
            return result;
        }
        if (!archiveInfo.isFile()) {
            fail(&result, QObject::tr("Path is not a regular file: %1").arg(archivePaths.at(index)));
            return result;
        }
        if (!archiveInfo.isReadable()) {
            fail(&result, QObject::tr("Archive is not readable: %1").arg(archivePaths.at(index)));
            return result;
        }
        if (!isSupportedArchiveFile(archiveInfo)) {
            fail(&result, QObject::tr("Unsupported archive format: %1").arg(archivePaths.at(index)));
            return result;
        }

        CollectionTreeResult archive = scanArchiveSourceTree(archiveInfo.absoluteFilePath(), password);
        if (!archive.ok()) {
            fail(&result, QObject::tr("%1: %2").arg(sourceDisplayName(archiveInfo), archive.errorMessage), archive.errorReason);
            return result;
        }

        const QString prefix = indexedPath(QStringLiteral("archive"), index);
        archive.root->displayName = sourceDisplayName(archiveInfo);
        archive.root->parent = root.get();
        rebaseArchiveNode(archive.root, prefix, sourceDisplayName(archiveInfo), QString());
        root->children.append(archive.root);
    }

    assignParents(root);

    const CollectionFolderNode *selected = firstNavigableFolder(root);
    if (selected == nullptr) {
        fail(&result, QObject::tr("Selected archives contain no supported images."));
        return result;
    }

    result.root = root;
    result.rootPath = QStringLiteral("selected-archives");
    result.selectedFolderPath = selected->relativePath;
    result.selectedImageIndex = selected->images.isEmpty() ? -1 : 0;
    return result;
}

} // namespace insidious::core
