#include "ui/comparison_drag_mime_data.h"
#include "ui/comparison_view_widget.h"

#include <QFileInfo>
#include <QtTest>

using namespace insidious;

class ComparisonViewWidgetTests : public QObject {
    Q_OBJECT

private slots:
    void supportsRowCounts();
    void supportsGridCounts();
    void supportsCarouselCounts();
    void supportsHoverSwitchCounts();
    void keepsCarouselSessionState();
    void comparisonDragMimeDataKeepsItems();
    void exposesAlignmentPresetState();
};

void ComparisonViewWidgetTests::supportsRowCounts()
{
    QVERIFY(!ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::Row, 0));
    QVERIFY(ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::Row, 1));
    QVERIFY(ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::Row, 8));
}

void ComparisonViewWidgetTests::supportsGridCounts()
{
    QVERIFY(!ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::Grid, 0));
    QVERIFY(ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::Grid, 1));
    QVERIFY(ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::Grid, 8));
}

void ComparisonViewWidgetTests::supportsCarouselCounts()
{
    QVERIFY(!ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::Carousel, 0));
    QVERIFY(ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::Carousel, 1));
    QVERIFY(ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::Carousel, 8));
}

void ComparisonViewWidgetTests::supportsHoverSwitchCounts()
{
    QVERIFY(!ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::HoverSwitch, 0));
    QVERIFY(!ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::HoverSwitch, 1));
    QVERIFY(ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::HoverSwitch, 2));
    QVERIFY(!ui::ComparisonViewWidget::supportsItemCount(ui::ComparisonMode::HoverSwitch, 3));
}

void ComparisonViewWidgetTests::keepsCarouselSessionState()
{
    auto item = [](const QString &path) {
        ui::ComparisonSessionItem item;
        item.entry.absolutePath = path;
        item.entry.displayName = QFileInfo(path).fileName();
        return item;
    };

    ui::ComparisonViewWidget widget;
    widget.setSession(
        QVector<ui::ComparisonSessionItem> {
            item(QStringLiteral("/tmp/compare-carousel-a.jpg")),
            item(QStringLiteral("/tmp/compare-carousel-b.jpg")),
        },
        ui::ComparisonRenderSettings {},
        ui::ComparisonMode::Carousel);

    QCOMPARE(widget.mode(), ui::ComparisonMode::Carousel);
    QCOMPARE(widget.itemCount(), 2);

    const int added = widget.appendItems(QVector<ui::ComparisonSessionItem> {
        item(QStringLiteral("/tmp/compare-carousel-c.jpg")),
    });

    QCOMPARE(added, 1);
    QCOMPARE(widget.mode(), ui::ComparisonMode::Carousel);
    QCOMPARE(widget.itemCount(), 3);
}

void ComparisonViewWidgetTests::comparisonDragMimeDataKeepsItems()
{
    ui::ComparisonSessionItem item;
    item.entry.absolutePath = QStringLiteral("/tmp/compare-drag.jpg");
    item.entry.displayName = QStringLiteral("compare-drag.jpg");
    item.archivePassword = QStringLiteral("in-memory-only");

    ui::ComparisonDragMimeData mimeData(QVector<ui::ComparisonSessionItem> { item });

    QVERIFY(mimeData.hasFormat(ui::ComparisonDragMimeData::mimeType()));
    QCOMPARE(mimeData.items().size(), 1);
    QCOMPARE(mimeData.items().first().entry.absolutePath, item.entry.absolutePath);
    QCOMPARE(mimeData.items().first().archivePassword, item.archivePassword);
}

void ComparisonViewWidgetTests::exposesAlignmentPresetState()
{
    ui::ComparisonViewWidget widget;

    QCOMPARE(widget.alignmentPreset(), ui::ComparisonAlignmentPreset::FitCenter);
    widget.setAlignmentPreset(ui::ComparisonAlignmentPreset::BottomCenter);
    QCOMPARE(widget.alignmentPreset(), ui::ComparisonAlignmentPreset::BottomCenter);
}

QTEST_MAIN(ComparisonViewWidgetTests)

#include "comparison_view_widget_tests.moc"
