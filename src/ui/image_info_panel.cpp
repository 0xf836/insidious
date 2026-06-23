#include "ui/image_info_panel.h"

#include <QDateTime>
#include <QFormLayout>
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
}

void ImageInfoPanel::showError(const core::ImageEntry &entry, const QString &error)
{
    setLabel(m_stateValue, error.isEmpty() ? tr("Error") : error);
    setEntryFields(entry);
    setLabel(m_dimensionsValue, tr("Unavailable"));
}

void ImageInfoPanel::setEntryFields(const core::ImageEntry &entry)
{
    setLabel(m_nameValue, entry.displayName.isEmpty() ? entry.absolutePath : entry.displayName);
    setLabel(m_pathValue, entry.absolutePath);
    setLabel(m_formatValue, entry.formatHint);
    setLabel(m_fileSizeValue, formattedFileSize(entry.fileSize));
    setLabel(m_modifiedValue, formattedDateTime(entry.modifiedTime));
}

void ImageInfoPanel::setDimensions(const QSize &sourceSize)
{
    setLabel(m_dimensionsValue, formattedDimensions(sourceSize));
}

void ImageInfoPanel::setLabel(QLabel *label, const QString &value)
{
    const QString displayValue = value.isEmpty() ? tr("N/A") : value;
    label->setText(displayValue);
    label->setToolTip(displayValue);
}

} // namespace insidious::ui
