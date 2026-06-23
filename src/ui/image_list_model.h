#pragma once

#include "core/image_entry.h"

#include <QAbstractListModel>
#include <QVector>

namespace insidious::ui {

class ImageListModel : public QAbstractListModel {
    Q_OBJECT

public:
    explicit ImageListModel(QObject *parent = nullptr);

    void setEntries(QVector<core::ImageEntry> entries);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

private:
    QVector<core::ImageEntry> m_entries;
};

} // namespace insidious::ui
