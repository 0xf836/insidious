#include "ui/settings_dialog.h"

#include <QLayout>
#include <QScrollArea>
#include <QStackedWidget>
#include <QtTest>

using namespace insidious;

class SettingsDialogTests : public QObject {
    Q_OBJECT

private slots:
    void viewerPageUsesScrollingInsteadOfCompressingSections();
};

void SettingsDialogTests::viewerPageUsesScrollingInsteadOfCompressingSections()
{
    ui::SettingsDialog dialog;
    dialog.resize(dialog.minimumSize());
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto *pages = dialog.findChild<QStackedWidget *>();
    QVERIFY(pages != nullptr);
    QVERIFY(pages->count() > 0);

    auto *viewerScrollArea = qobject_cast<QScrollArea *>(pages->widget(0));
    QVERIFY(viewerScrollArea != nullptr);
    QVERIFY(viewerScrollArea->widgetResizable());
    QVERIFY(viewerScrollArea->widget() != nullptr);

    dialog.layout()->activate();
    viewerScrollArea->widget()->layout()->activate();

    QVERIFY(viewerScrollArea->widget()->height() >= viewerScrollArea->widget()->minimumSizeHint().height());
}

QTEST_MAIN(SettingsDialogTests)

#include "settings_dialog_tests.moc"
