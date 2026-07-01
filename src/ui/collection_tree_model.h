#pragma once

#include "core/folder_collection_source.h"

#include <QAbstractItemModel>
#include <QHash>

#include <memory>

namespace insidious::ui {

class CollectionTreeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    explicit CollectionTreeModel(QObject *parent = nullptr);

    void setRoot(std::shared_ptr<core::CollectionFolderNode> root);
    void clear();
    bool replaceLoadedFolderContents(
        core::CollectionFolderNode *folder,
        QVector<core::ImageEntry> images,
        QVector<std::shared_ptr<core::CollectionFolderNode>> children);

    [[nodiscard]] QModelIndex indexForFolderPath(const QString &relativePath) const;
    [[nodiscard]] core::CollectionFolderNode *nodeForFolderPath(const QString &relativePath) const;
    [[nodiscard]] core::CollectionFolderNode *nodeForIndex(const QModelIndex &index) const;
    [[nodiscard]] QString folderPathForIndex(const QModelIndex &index) const;

    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex &index) const override;
    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

private:
    [[nodiscard]] QModelIndex indexForNode(core::CollectionFolderNode *node) const;
    [[nodiscard]] core::CollectionFolderNode *rootNode() const;
    void rebuildIndexes();
    void indexSubtree(core::CollectionFolderNode *node);
    void removeSubtreeFromIndexes(core::CollectionFolderNode *node);
    void setChildRows(core::CollectionFolderNode *parent);

    std::shared_ptr<core::CollectionFolderNode> m_root;
    QHash<QString, core::CollectionFolderNode *> m_nodesByPath;
    QHash<core::CollectionFolderNode *, int> m_nodeRows;
};

} // namespace insidious::ui
