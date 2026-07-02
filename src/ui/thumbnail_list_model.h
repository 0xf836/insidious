#pragma once

#include "core/image_entry.h"

#include <QAbstractListModel>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QVector>

namespace insidious::ui {

class ThumbnailListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        ThumbnailImageRole = Qt::UserRole + 1,
    };

    explicit ThumbnailListModel(QObject *parent = nullptr);

    void setEntries(QVector<core::ImageEntry> entries);
    void clearThumbnails();
    void setThumbnail(const QString &identityKey, const QImage &thumbnail);
    void markFailed(const QString &identityKey);

    [[nodiscard]] bool hasThumbnail(int row) const;
    [[nodiscard]] bool isFailed(int row) const;
    [[nodiscard]] const core::ImageEntry *entryAt(int row) const;

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

private:
    [[nodiscard]] int rowForIdentityKey(const QString &identityKey) const;

    QVector<core::ImageEntry> m_entries;
    QHash<QString, QImage> m_thumbnailsByIdentity;
    QSet<QString> m_failedIdentities;
};

} // namespace insidious::ui
