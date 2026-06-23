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
    endResetModel();
}

void CollectionTreeModel::clear()
{
    setRoot(nullptr);
}

QModelIndex CollectionTreeModel::indexForFolderPath(const QString &relativePath) const
{
    core::CollectionFolderNode *root = rootNode();
    if (root == nullptr) {
        return {};
    }

    if (root->relativePath == relativePath) {
        return indexForNode(root);
    }

    QVector<core::CollectionFolderNode *> stack;
    stack.append(root);
    while (!stack.isEmpty()) {
        core::CollectionFolderNode *node = stack.takeLast();
        if (node->relativePath == relativePath) {
            return indexForNode(node);
        }
        for (const auto &child : node->children) {
            stack.append(child.get());
        }
    }

    return {};
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

    const core::CollectionFolderNode *parentNode = node->parent;
    if (parentNode == nullptr) {
        return {};
    }

    for (qsizetype row = 0; row < parentNode->children.size(); ++row) {
        if (parentNode->children.at(row).get() == node) {
            return createIndex(static_cast<int>(row), 0, node);
        }
    }
    return {};
}

core::CollectionFolderNode *CollectionTreeModel::rootNode() const
{
    return m_root.get();
}

} // namespace insidious::ui
