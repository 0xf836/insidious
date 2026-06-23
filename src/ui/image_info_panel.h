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
    void setInfoMessage(const QString &message);
    void setEntryInfoFields(const core::ImageEntry &entry, const QString &state, const QString &dimensions);
    void setInfoFields(const QVector<core::ImageMetadataField> &fields);
    void setExifFields(const QVector<core::ImageMetadataField> &fields);
    void setExifMessage(const QString &message);
    void addFieldRow(QFormLayout *form, QVector<QWidget *> &rows, const QString &name, const QString &value);
    void clearRows(QFormLayout *form, QVector<QWidget *> &rows);
    void clearInfoRows();
    void clearExifRows();

    QLabel *m_infoHeader = nullptr;
    QFormLayout *m_infoForm = nullptr;
    QVector<QWidget *> m_infoRows;
    QLabel *m_exifHeader = nullptr;
    QFormLayout *m_exifForm = nullptr;
    QVector<QWidget *> m_exifRows;
};

} // namespace insidious::ui
