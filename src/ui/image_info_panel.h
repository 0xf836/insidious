#pragma once

#include "core/decoded_image.h"
#include "core/image_entry.h"

#include <QWidget>

class QLabel;
class QFormLayout;

namespace insidious::ui {

class ImageInfoPanel : public QWidget {
    Q_OBJECT

public:
    explicit ImageInfoPanel(QWidget *parent = nullptr);

    void showMessage(const QString &message);
    void showEntry(const core::ImageEntry &entry, const QString &state = {});
    void showDecodedImage(const core::ImageEntry &entry, const core::DecodedImage &decoded);
    void showError(const core::ImageEntry &entry, const QString &error);

private:
    void setEntryFields(const core::ImageEntry &entry);
    void setDimensions(const QSize &sourceSize);
    void setExifFields(const QVector<core::ImageMetadataField> &fields);
    void setExifMessage(const QString &message);
    void clearExifRows();
    void setLabel(QLabel *label, const QString &value);

    QLabel *m_stateValue = nullptr;
    QLabel *m_nameValue = nullptr;
    QLabel *m_pathValue = nullptr;
    QLabel *m_formatValue = nullptr;
    QLabel *m_fileSizeValue = nullptr;
    QLabel *m_dimensionsValue = nullptr;
    QLabel *m_modifiedValue = nullptr;
    QLabel *m_exifHeader = nullptr;
    QFormLayout *m_exifForm = nullptr;
    QVector<QWidget *> m_exifRows;
};

} // namespace insidious::ui
