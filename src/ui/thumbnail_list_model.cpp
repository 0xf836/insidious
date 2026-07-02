#include "ui/thumbnail_list_model.h"

#include <QPixmap>

#include <utility>

namespace insidious::ui {

ThumbnailListModel::ThumbnailListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void ThumbnailListModel::setEntries(QVector<core::ImageEntry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    m_thumbnailsByIdentity.clear();
    m_failedIdentities.clear();
    endResetModel();
}

void ThumbnailListModel::clearThumbnails()
{
    if (m_thumbnailsByIdentity.isEmpty() && m_failedIdentities.isEmpty()) {
        return;
    }

    m_thumbnailsByIdentity.clear();
    m_failedIdentities.clear();
    if (!m_entries.isEmpty()) {
        emit dataChanged(index(0, 0), index(m_entries.size() - 1, 0), { Qt::DecorationRole });
    }
}

void ThumbnailListModel::setThumbnail(const QString &identityKey, const QImage &thumbnail)
{
    if (identityKey.isEmpty() || thumbnail.isNull()) {
        return;
    }

    QImage storedThumbnail = thumbnail;
    storedThumbnail.setDevicePixelRatio(1.0);
    m_thumbnailsByIdentity.insert(identityKey, storedThumbnail);
    m_failedIdentities.remove(identityKey);
    const int row = rowForIdentityKey(identityKey);
    if (row >= 0) {
        emit dataChanged(index(row, 0), index(row, 0), { Qt::DecorationRole });
    }
}

void ThumbnailListModel::markFailed(const QString &identityKey)
{
    if (identityKey.isEmpty()) {
        return;
    }

    if (m_thumbnailsByIdentity.contains(identityKey)) {
        return;
    }

    m_failedIdentities.insert(identityKey);
}

bool ThumbnailListModel::hasThumbnail(int row) const
{
    const core::ImageEntry *entry = entryAt(row);
    return entry != nullptr && m_thumbnailsByIdentity.contains(entry->identityKey());
}

bool ThumbnailListModel::isFailed(int row) const
{
    const core::ImageEntry *entry = entryAt(row);
    return entry != nullptr && m_failedIdentities.contains(entry->identityKey());
}

const core::ImageEntry *ThumbnailListModel::entryAt(int row) const
{
    if (row < 0 || row >= m_entries.size()) {
        return nullptr;
    }
    return &m_entries.at(row);
}

int ThumbnailListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_entries.size();
}

QVariant ThumbnailListModel::data(const QModelIndex &index, int role) const
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
    if (role == Qt::DecorationRole) {
        const auto thumbnail = m_thumbnailsByIdentity.constFind(entry.identityKey());
        if (thumbnail != m_thumbnailsByIdentity.constEnd()) {
            return QPixmap::fromImage(thumbnail.value());
        }
    }
    if (role == ThumbnailImageRole) {
        const auto thumbnail = m_thumbnailsByIdentity.constFind(entry.identityKey());
        if (thumbnail != m_thumbnailsByIdentity.constEnd()) {
            return thumbnail.value();
        }
    }

    return {};
}

int ThumbnailListModel::rowForIdentityKey(const QString &identityKey) const
{
    for (qsizetype row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).identityKey() == identityKey) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

} // namespace insidious::ui
