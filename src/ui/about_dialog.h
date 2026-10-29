#pragma once

#include <QDialog>

namespace insidious::ui {
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent = nullptr);
};
} // namespace insidious::ui
