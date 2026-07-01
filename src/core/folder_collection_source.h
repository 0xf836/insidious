#pragma once

#include "core/image_entry.h"

#include <QVector>

#include <memory>

class QFileInfo;

namespace insidious::core {

enum class ScanErrorReason {
    None,
    Generic,
    PasswordRequired,
    InvalidPassword,
};

struct ScanResult {
    QVector<ImageEntry> entries;
    int selectedIndex = -1;
    QString errorMessage;
    ScanErrorReason errorReason = ScanErrorReason::None;

    [[nodiscard]] bool ok() const;
};

struct CollectionFolderNode {
    QString relativePath;
    QString displayName;
    QString loadRootPath;
    QString loadFolderRelativePath;
    QString sourceTreePrefix;
    QVector<ImageEntry> images;
    QVector<std::shared_ptr<CollectionFolderNode>> children;
    bool loaded = false;
    bool canLoadChildren = false;
    bool preserveImageOrder = false;
    CollectionFolderNode *parent = nullptr;
};

struct CollectionTreeResult {
    std::shared_ptr<CollectionFolderNode> root;
    QString rootPath;
    QString selectedFolderPath;
    int selectedImageIndex = -1;
    QString errorMessage;
    ScanErrorReason errorReason = ScanErrorReason::None;

    [[nodiscard]] bool ok() const;
};

struct FolderLoadResult {
    QString rootPath;
    QString folderRelativePath;
    QVector<ImageEntry> images;
    QVector<std::shared_ptr<CollectionFolderNode>> children;
    QString errorMessage;

    [[nodiscard]] bool ok() const;
};

[[nodiscard]] ImageEntry imageEntryFromFileInfo(const QFileInfo &fileInfo);
[[nodiscard]] ScanResult scanFolder(const QString &folderPath, const QString &selectedPath = {});
[[nodiscard]] ScanResult singleImageCollection(const QString &filePath);
[[nodiscard]] CollectionTreeResult scanFileSystemSourceTree(const QString &folderPath, const QString &selectedPath = {});
[[nodiscard]] FolderLoadResult loadFileSystemFolder(const QString &rootPath, const QString &folderRelativePath);

} // namespace insidious::core
