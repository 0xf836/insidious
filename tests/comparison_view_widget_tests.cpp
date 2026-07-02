#include "ui/comparison_view_widget.h"

#include <QtTest>

using namespace insidious;

class ComparisonViewWidgetTests : public QObject {
    Q_OBJECT

private slots:
    void supportsRowCounts();
    void supportsHoverSwitchCounts();
};

void ComparisonViewWidgetTests::supportsRowCounts()
{
    QVERIFY(!ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::Row, 0));
    QVERIFY(ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::Row, 1));
    QVERIFY(ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::Row, 8));
}

void ComparisonViewWidgetTests::supportsHoverSwitchCounts()
{
    QVERIFY(!ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::HoverSwitch, 0));
    QVERIFY(!ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::HoverSwitch, 1));
    QVERIFY(ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::HoverSwitch, 2));
    QVERIFY(!ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::HoverSwitch, 3));
}

QTEST_MAIN(ComparisonViewWidgetTests)

#include "comparison_view_widget_tests.moc"
