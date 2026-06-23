#include "ui/image_info_panel.h"

#include <QDateTime>
#include <QFormLayout>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

namespace insidious::ui {
namespace {

QLabel *createValueLabel()
{
    auto *label = new QLabel;
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    return label;
}

QLabel *createSectionHeader(const QString &title, QWidget *parent)
{
    auto *label = new QLabel(title, parent);
    QFont headerFont = label->font();
    headerFont.setBold(true);
    label->setFont(headerFont);
    return label;
}

QFormLayout *createSectionForm()
{
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
    form->setVerticalSpacing(6);
    return form;
}

QString formattedFileSize(qint64 bytes)
{
    return QLocale::system().formattedDataSize(bytes);
}

QString formattedDateTime(const QDateTime &dateTime)
{
    if (!dateTime.isValid()) {
        return {};
    }
    return QLocale::system().toString(dateTime.toLocalTime(), QLocale::ShortFormat);
}

QString formattedDimensions(const QSize &size)
{
    if (size.isEmpty()) {
        return {};
    }

    const double megapixels = static_cast<double>(size.width()) * static_cast<double>(size.height()) / 1'000'000.0;
    return QObject::tr("%1 x %2, %3 MP")
        .arg(size.width())
        .arg(size.height())
        .arg(QLocale::system().toString(megapixels, 'f', 2));
}

} // namespace

ImageInfoPanel::ImageInfoPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    m_infoHeader = createSectionHeader(tr("Info"), this);
    layout->addWidget(m_infoHeader);

    m_infoForm = createSectionForm();
    layout->addLayout(m_infoForm);

    auto *separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);

    m_exifHeader = createSectionHeader(tr("EXIF"), this);
    layout->addWidget(m_exifHeader);

    m_exifForm = createSectionForm();
    layout->addLayout(m_exifForm);
    layout->addStretch(1);

    showMessage(tr("No image loaded."));
}

void ImageInfoPanel::showMessage(const QString &message)
{
    setInfoMessage(message.isEmpty() ? tr("No image loaded.") : message);
    setExifMessage({});
}

void ImageInfoPanel::showEntry(const core::ImageEntry &entry, const QString &state)
{
    setEntryInfoFields(entry, state.isEmpty() ? tr("Ready") : state, tr("Loading..."));
    setExifMessage(tr("Loading..."));
}

void ImageInfoPanel::showDecodedImage(const core::ImageEntry &entry, const core::DecodedImage &decoded)
{
    setEntryInfoFields(entry, tr("Ready"), formattedDimensions(decoded.sourceSize));
    setExifFields(decoded.metadataFields);
}

void ImageInfoPanel::showError(const core::ImageEntry &entry, const QString &error)
{
    setEntryInfoFields(entry, error.isEmpty() ? tr("Error") : error, tr("Unavailable"));
    setExifMessage(tr("Unavailable"));
}

void ImageInfoPanel::setInfoMessage(const QString &message)
{
    clearInfoRows();
    addFieldRow(m_infoForm, m_infoRows, tr("State"), message);
}

void ImageInfoPanel::setEntryInfoFields(const core::ImageEntry &entry, const QString &state, const QString &dimensions)
{
    QVector<core::ImageMetadataField> fields;
    fields.append({tr("State"), state});
    fields.append({tr("Name"), entry.displayName.isEmpty() ? entry.absolutePath : entry.displayName});
    fields.append({tr("Path"), entry.absolutePath});
    fields.append({tr("Format"), entry.formatHint});
    fields.append({tr("File size"), formattedFileSize(entry.fileSize)});
    fields.append({tr("Dimensions"), dimensions});
    fields.append({tr("Modified"), formattedDateTime(entry.modifiedTime)});
    setInfoFields(fields);
}

void ImageInfoPanel::setInfoFields(const QVector<core::ImageMetadataField> &fields)
{
    clearInfoRows();
    for (const core::ImageMetadataField &field : fields) {
        addFieldRow(m_infoForm, m_infoRows, field.name, field.value);
    }
}

void ImageInfoPanel::setExifFields(const QVector<core::ImageMetadataField> &fields)
{
    if (fields.isEmpty()) {
        setExifMessage(tr("No EXIF found."));
        return;
    }

    clearExifRows();
    for (const core::ImageMetadataField &field : fields) {
        addFieldRow(m_exifForm, m_exifRows, field.name, field.value);
    }

    if (m_exifRows.isEmpty()) {
        setExifMessage(tr("No EXIF found."));
    }
}

void ImageInfoPanel::setExifMessage(const QString &message)
{
    clearExifRows();
    addFieldRow(m_exifForm, m_exifRows, {}, message);
}

void ImageInfoPanel::addFieldRow(QFormLayout *form, QVector<QWidget *> &rows, const QString &name, const QString &value)
{
    auto *valueLabel = createValueLabel();
    const QString displayValue = value.isEmpty() ? tr("N/A") : value;
    valueLabel->setText(displayValue);
    valueLabel->setToolTip(displayValue);

    if (name.isEmpty()) {
        form->addRow(QString(), valueLabel);
        rows.append(valueLabel);
        return;
    }

    auto *nameLabel = new QLabel(name, this);
    nameLabel->setAlignment(Qt::AlignRight | Qt::AlignTop);
    form->addRow(nameLabel, valueLabel);
    rows.append(nameLabel);
    rows.append(valueLabel);
}

void ImageInfoPanel::clearRows(QFormLayout *form, QVector<QWidget *> &rows)
{
    while (QLayoutItem *item = form->takeAt(0)) {
        delete item;
    }
    for (QWidget *widget : rows) {
        delete widget;
    }
    rows.clear();
}

void ImageInfoPanel::clearInfoRows()
{
    clearRows(m_infoForm, m_infoRows);
}

void ImageInfoPanel::clearExifRows()
{
    clearRows(m_exifForm, m_exifRows);
}

} // namespace insidious::ui
