#include "ui/image_info_panel.h"

#include <QDateTime>
#include <QFormLayout>
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

    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);

    m_stateValue = createValueLabel();
    m_nameValue = createValueLabel();
    m_pathValue = createValueLabel();
    m_formatValue = createValueLabel();
    m_fileSizeValue = createValueLabel();
    m_dimensionsValue = createValueLabel();
    m_modifiedValue = createValueLabel();

    form->addRow(tr("State"), m_stateValue);
    form->addRow(tr("Name"), m_nameValue);
    form->addRow(tr("Path"), m_pathValue);
    form->addRow(tr("Format"), m_formatValue);
    form->addRow(tr("File size"), m_fileSizeValue);
    form->addRow(tr("Dimensions"), m_dimensionsValue);
    form->addRow(tr("Modified"), m_modifiedValue);

    layout->addLayout(form);

    auto *separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);

    m_exifHeader = new QLabel(tr("EXIF"), this);
    QFont headerFont = m_exifHeader->font();
    headerFont.setBold(true);
    m_exifHeader->setFont(headerFont);
    layout->addWidget(m_exifHeader);

    m_exifForm = new QFormLayout;
    m_exifForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_exifForm->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
    layout->addLayout(m_exifForm);
    layout->addStretch(1);

    showMessage(tr("No image loaded."));
}

void ImageInfoPanel::showMessage(const QString &message)
{
    setLabel(m_stateValue, message.isEmpty() ? tr("No image loaded.") : message);
    setLabel(m_nameValue, {});
    setLabel(m_pathValue, {});
    setLabel(m_formatValue, {});
    setLabel(m_fileSizeValue, {});
    setLabel(m_dimensionsValue, {});
    setLabel(m_modifiedValue, {});
    setExifMessage({});
}

void ImageInfoPanel::showEntry(const core::ImageEntry &entry, const QString &state)
{
    setLabel(m_stateValue, state.isEmpty() ? tr("Ready") : state);
    setEntryFields(entry);
    setLabel(m_dimensionsValue, tr("Loading..."));
}

void ImageInfoPanel::showDecodedImage(const core::ImageEntry &entry, const core::DecodedImage &decoded)
{
    setLabel(m_stateValue, tr("Ready"));
    setEntryFields(entry);
    setDimensions(decoded.sourceSize);
    setExifFields(decoded.metadataFields);
}

void ImageInfoPanel::showError(const core::ImageEntry &entry, const QString &error)
{
    setLabel(m_stateValue, error.isEmpty() ? tr("Error") : error);
    setEntryFields(entry);
    setLabel(m_dimensionsValue, tr("Unavailable"));
    setExifMessage(tr("Unavailable"));
}

void ImageInfoPanel::setEntryFields(const core::ImageEntry &entry)
{
    setLabel(m_nameValue, entry.displayName.isEmpty() ? entry.absolutePath : entry.displayName);
    setLabel(m_pathValue, entry.absolutePath);
    setLabel(m_formatValue, entry.formatHint);
    setLabel(m_fileSizeValue, formattedFileSize(entry.fileSize));
    setLabel(m_modifiedValue, formattedDateTime(entry.modifiedTime));
    setExifMessage(tr("Loading..."));
}

void ImageInfoPanel::setDimensions(const QSize &sourceSize)
{
    setLabel(m_dimensionsValue, formattedDimensions(sourceSize));
}

void ImageInfoPanel::setExifFields(const QVector<core::ImageMetadataField> &fields)
{
    if (fields.isEmpty()) {
        setExifMessage(tr("No EXIF found."));
        return;
    }

    clearExifRows();
    for (const core::ImageMetadataField &field : fields) {
        if (field.name.isEmpty() || field.value.isEmpty()) {
            continue;
        }

        auto *nameLabel = new QLabel(field.name, this);
        nameLabel->setAlignment(Qt::AlignRight | Qt::AlignTop);
        auto *valueLabel = createValueLabel();
        valueLabel->setText(field.value);
        valueLabel->setToolTip(field.value);
        m_exifForm->addRow(nameLabel, valueLabel);
        m_exifRows.append(nameLabel);
        m_exifRows.append(valueLabel);
    }

    if (m_exifRows.isEmpty()) {
        setExifMessage(tr("No EXIF found."));
    }
}

void ImageInfoPanel::setExifMessage(const QString &message)
{
    clearExifRows();
    auto *valueLabel = createValueLabel();
    const QString displayValue = message.isEmpty() ? tr("N/A") : message;
    valueLabel->setText(displayValue);
    valueLabel->setToolTip(displayValue);
    m_exifForm->addRow(QString(), valueLabel);
    m_exifRows.append(valueLabel);
}

void ImageInfoPanel::clearExifRows()
{
    while (QLayoutItem *item = m_exifForm->takeAt(0)) {
        delete item;
    }
    for (QWidget *widget : m_exifRows) {
        delete widget;
    }
    m_exifRows.clear();
}

void ImageInfoPanel::setLabel(QLabel *label, const QString &value)
{
    const QString displayValue = value.isEmpty() ? tr("N/A") : value;
    label->setText(displayValue);
    label->setToolTip(displayValue);
}

} // namespace insidious::ui
