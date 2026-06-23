#include "ui/image_list_model.h"

#include <utility>

namespace insidious::ui {

ImageListModel::ImageListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void ImageListModel::setEntries(QVector<core::ImageEntry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
}

int ImageListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_entries.size();
}

QVariant ImageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }

    const core::ImageEntry &entry = m_entries.at(index.row());
    if (role == Qt::DisplayRole || role == Qt::AccessibleTextRole) {
        return entry.displayName.isEmpty() ? entry.absolutePath : entry.displayName;
    }
    if (role == Qt::ToolTipRole || role == Qt::StatusTipRole) {
        return entry.sourceRelativePath.isEmpty() ? entry.absolutePath : entry.sourceRelativePath;
    }

    return {};
}

} // namespace insidious::ui
