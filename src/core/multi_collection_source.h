#pragma once

#include "core/folder_collection_source.h"

#include <QStringList>

namespace insidious::core {

[[nodiscard]] CollectionTreeResult fileListSourceTree(const QStringList &filePaths);
[[nodiscard]] CollectionTreeResult folderListSourceTree(const QStringList &folderPaths);
[[nodiscard]] CollectionTreeResult archiveListSourceTree(const QStringList &archivePaths, const QString &password = {});

} // namespace insidious::core
