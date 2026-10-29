#include "ui/about_dialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

namespace insidious::ui {
AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About Insidious"));
    setObjectName(QStringLiteral("aboutDialog"));
    auto *layout = new QVBoxLayout(this);
    auto *version = new QLabel(tr("Insidious %1").arg(QApplication::applicationVersion()), this);
    version->setObjectName(QStringLiteral("aboutVersion"));
    version->setTextFormat(Qt::PlainText);
    version->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    layout->addWidget(version);
    layout->addWidget(new QLabel(tr("Native Linux image viewer."), this));
    auto *license = new QLabel(tr("MIT License\nCopyright (c) 2026 Insidious contributors"), this);
    license->setObjectName(QStringLiteral("aboutLicense"));
    layout->addWidget(license);
    auto *link = new QLabel(QStringLiteral("<a href=\"https://github.com/0xf836/insidious\">github.com/0xf836/insidious</a>"), this);
    link->setObjectName(QStringLiteral("aboutProjectLink"));
    // Opening the dialog is offline; only an explicit link click opens a browser.
    link->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    link->setOpenExternalLinks(true);
    layout->addWidget(link);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
} // namespace insidious::ui
