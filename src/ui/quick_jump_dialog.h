#pragma once

#include "core/image_entry.h"

#include <QDialog>
#include <QVector>

class QDialogButtonBox;
class QLineEdit;
class QListView;
class QSortFilterProxyModel;

namespace insidious::ui {

class ImageListModel;

class QuickJumpDialog : public QDialog {
    Q_OBJECT

public:
    explicit QuickJumpDialog(const QVector<core::ImageEntry> &entries, QWidget *parent = nullptr);

    [[nodiscard]] int selectedSourceRow() const;
    void focusSearch();

private:
    void updateSelection();

    ImageListModel *m_model = nullptr;
    QSortFilterProxyModel *m_proxy = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QListView *m_resultsView = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;
};

} // namespace insidious::ui
