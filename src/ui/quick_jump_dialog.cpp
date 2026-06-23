#include "ui/quick_jump_dialog.h"

#include "ui/image_list_model.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QVBoxLayout>

namespace insidious::ui {

QuickJumpDialog::QuickJumpDialog(const QVector<core::ImageEntry> &entries, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Quick Jump"));
    setModal(true);
    resize(480, 360);

    m_model = new ImageListModel(this);
    m_model->setEntries(entries);

    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterKeyColumn(0);
    m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Type file name..."));
    m_searchEdit->setClearButtonEnabled(true);

    m_resultsView = new QListView(this);
    m_resultsView->setModel(m_proxy);
    m_resultsView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsView->setAlternatingRowColors(true);
    m_resultsView->setUniformItemSizes(true);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_searchEdit);
    layout->addWidget(m_resultsView, 1);
    layout->addWidget(m_buttonBox);

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_proxy->setFilterFixedString(text);
        updateSelection();
    });
    connect(m_resultsView, &QListView::activated, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateSelection();
    m_searchEdit->setFocus(Qt::OtherFocusReason);
}

int QuickJumpDialog::selectedSourceRow() const
{
    const QModelIndex proxyIndex = m_resultsView->currentIndex();
    if (!proxyIndex.isValid()) {
        return -1;
    }

    const QModelIndex sourceIndex = m_proxy->mapToSource(proxyIndex);
    return sourceIndex.isValid() ? sourceIndex.row() : -1;
}

void QuickJumpDialog::updateSelection()
{
    const bool hasMatches = m_proxy->rowCount() > 0;
    QPushButton *okButton = m_buttonBox->button(QDialogButtonBox::Ok);
    if (okButton != nullptr) {
        okButton->setEnabled(hasMatches);
    }

    if (!hasMatches) {
        m_resultsView->setCurrentIndex(QModelIndex());
        return;
    }

    const QModelIndex firstIndex = m_proxy->index(0, 0);
    m_resultsView->setCurrentIndex(firstIndex);
    m_resultsView->scrollTo(firstIndex, QAbstractItemView::PositionAtTop);
}

} // namespace insidious::ui
