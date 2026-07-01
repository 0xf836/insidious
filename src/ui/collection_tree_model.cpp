#include "ui/collection_tree_model.h"

#include <QIcon>

#include <utility>

namespace insidious::ui {

CollectionTreeModel::CollectionTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
{
}

void CollectionTreeModel::setRoot(std::shared_ptr<core::CollectionFolderNode> root)
{
    beginResetModel();
    m_root = std::move(root);
    rebuildIndexes();
    endResetModel();
}

void CollectionTreeModel::clear()
{
    setRoot(nullptr);
}

bool CollectionTreeModel::replaceLoadedFolderContents(
    core::CollectionFolderNode *folder,
    QVector<core::ImageEntry> images,
    QVector<std::shared_ptr<core::CollectionFolderNode>> children)
{
    if (folder == nullptr || !m_nodeRows.contains(folder)) {
        return false;
    }

    const QModelIndex folderIndex = indexForNode(folder);
    if (!folderIndex.isValid()) {
        return false;
    }

    if (!folder->children.isEmpty()) {
        beginRemoveRows(folderIndex, 0, static_cast<int>(folder->children.size()) - 1);
        for (const auto &child : folder->children) {
            removeSubtreeFromIndexes(child.get());
        }
        folder->children.clear();
        endRemoveRows();
    }

    folder->images = std::move(images);
    folder->loaded = true;

    if (children.isEmpty()) {
        folder->children.clear();
        return true;
    }

    beginInsertRows(folderIndex, 0, static_cast<int>(children.size()) - 1);
    folder->children = std::move(children);
    setChildRows(folder);
    for (const auto &child : folder->children) {
        child->parent = folder;
        indexSubtree(child.get());
    }
    endInsertRows();
    return true;
}

QModelIndex CollectionTreeModel::indexForFolderPath(const QString &relativePath) const
{
    return indexForNode(nodeForFolderPath(relativePath));
}

core::CollectionFolderNode *CollectionTreeModel::nodeForFolderPath(const QString &relativePath) const
{
    return m_nodesByPath.value(relativePath, nullptr);
}

core::CollectionFolderNode *CollectionTreeModel::nodeForIndex(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return nullptr;
    }
    return static_cast<core::CollectionFolderNode *>(index.internalPointer());
}

QString CollectionTreeModel::folderPathForIndex(const QModelIndex &index) const
{
    core::CollectionFolderNode *node = nodeForIndex(index);
    return node == nullptr ? QString() : node->relativePath;
}

QModelIndex CollectionTreeModel::index(int row, int column, const QModelIndex &parentIndex) const
{
    if (column != 0 || row < 0) {
        return {};
    }

    if (!parentIndex.isValid()) {
        if (m_root != nullptr && row == 0) {
            return createIndex(0, 0, m_root.get());
        }
        return {};
    }

    const core::CollectionFolderNode *parentNode = nodeForIndex(parentIndex);
    if (parentNode == nullptr || row >= parentNode->children.size()) {
        return {};
    }

    return createIndex(row, 0, parentNode->children.at(row).get());
}

QModelIndex CollectionTreeModel::parent(const QModelIndex &index) const
{
    core::CollectionFolderNode *node = nodeForIndex(index);
    if (node == nullptr || node->parent == nullptr) {
        return {};
    }

    return indexForNode(node->parent);
}

int CollectionTreeModel::rowCount(const QModelIndex &parentIndex) const
{
    if (parentIndex.column() > 0) {
        return 0;
    }
    if (!parentIndex.isValid()) {
        return m_root == nullptr ? 0 : 1;
    }

    const core::CollectionFolderNode *node = nodeForIndex(parentIndex);
    return node == nullptr ? 0 : node->children.size();
}

int CollectionTreeModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return 1;
}

QVariant CollectionTreeModel::data(const QModelIndex &index, int role) const
{
    const core::CollectionFolderNode *node = nodeForIndex(index);
    if (node == nullptr) {
        return {};
    }

    if (role == Qt::DisplayRole || role == Qt::AccessibleTextRole) {
        return node->displayName;
    }
    if (role == Qt::ToolTipRole || role == Qt::StatusTipRole) {
        return node->relativePath.isEmpty() ? node->displayName : node->relativePath;
    }
    if (role == Qt::DecorationRole) {
        return QIcon::fromTheme(QStringLiteral("folder"));
    }

    return {};
}

QModelIndex CollectionTreeModel::indexForNode(core::CollectionFolderNode *node) const
{
    if (node == nullptr || m_root == nullptr) {
        return {};
    }
    if (node == m_root.get()) {
        return createIndex(0, 0, node);
    }

    const auto row = m_nodeRows.constFind(node);
    if (row == m_nodeRows.constEnd() || *row < 0) {
        return {};
    }
    return createIndex(*row, 0, node);
}

core::CollectionFolderNode *CollectionTreeModel::rootNode() const
{
    return m_root.get();
}

void CollectionTreeModel::rebuildIndexes()
{
    m_nodesByPath.clear();
    m_nodeRows.clear();
    if (m_root == nullptr) {
        return;
    }

    m_nodeRows.insert(m_root.get(), 0);
    indexSubtree(m_root.get());
}

void CollectionTreeModel::indexSubtree(core::CollectionFolderNode *node)
{
    if (node == nullptr) {
        return;
    }

    m_nodesByPath.insert(node->relativePath, node);
    setChildRows(node);
    for (const auto &child : node->children) {
        child->parent = node;
        indexSubtree(child.get());
    }
}

void CollectionTreeModel::removeSubtreeFromIndexes(core::CollectionFolderNode *node)
{
    if (node == nullptr) {
        return;
    }

    m_nodesByPath.remove(node->relativePath);
    m_nodeRows.remove(node);
    for (const auto &child : node->children) {
        removeSubtreeFromIndexes(child.get());
    }
}

void CollectionTreeModel::setChildRows(core::CollectionFolderNode *parent)
{
    if (parent == nullptr) {
        return;
    }

    for (qsizetype row = 0; row < parent->children.size(); ++row) {
        m_nodeRows.insert(parent->children.at(row).get(), static_cast<int>(row));
    }
}

} // namespace insidious::ui
