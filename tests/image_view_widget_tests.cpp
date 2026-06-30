#include "ui/image_view_widget.h"

#include <QApplication>
#include <QSignalSpy>
#include <QtTest>

using namespace insidious::ui;

class ImageViewWidgetTests : public QObject {
    Q_OBJECT

private slots:
    void middleButtonFitHandPanEmitsPan();
    void ctrlLeftFitHandPanDoesNotClick();
    void disabledFitHandPanIgnoresGestures();
};

namespace {

void configureWidget(ImageViewWidget *widget)
{
    widget->resize(320, 240);
    widget->setImage(QImage(640, 480, QImage::Format_RGB32));
    widget->setFitHandPanEnabled(true);
}

} // namespace

void ImageViewWidgetTests::middleButtonFitHandPanEmitsPan()
{
    ImageViewWidget widget;
    configureWidget(&widget);
    QSignalSpy started(&widget, &ImageViewWidget::fitHandPanStarted);
    QSignalSpy moved(&widget, &ImageViewWidget::fitHandPanMoved);
    QSignalSpy ended(&widget, &ImageViewWidget::fitHandPanEnded);
    QSignalSpy panChanged(&widget, &ImageViewWidget::panChanged);

    QTest::mousePress(&widget, Qt::MiddleButton, Qt::NoModifier, QPoint(20, 20));
    QTest::mouseMove(&widget, QPoint(26, 28));
    QTest::mouseRelease(&widget, Qt::MiddleButton, Qt::NoModifier, QPoint(26, 28));

    QCOMPARE(started.count(), 1);
    QCOMPARE(moved.count(), 1);
    QCOMPARE(ended.count(), 1);
    QCOMPARE(panChanged.count(), 1);
    QCOMPARE(widget.panOffset(), QPointF(6.0, 8.0));
}

void ImageViewWidgetTests::ctrlLeftFitHandPanDoesNotClick()
{
    ImageViewWidget widget;
    configureWidget(&widget);
    QSignalSpy started(&widget, &ImageViewWidget::fitHandPanStarted);
    QSignalSpy moved(&widget, &ImageViewWidget::fitHandPanMoved);
    QSignalSpy ended(&widget, &ImageViewWidget::fitHandPanEnded);
    QSignalSpy clicked(&widget, &ImageViewWidget::imageClicked);

    QTest::mousePress(&widget, Qt::LeftButton, Qt::ControlModifier, QPoint(30, 30));
    QTest::mouseMove(&widget, QPoint(37, 35));
    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::ControlModifier, QPoint(37, 35));
    QTest::qWait(QApplication::doubleClickInterval() + 20);

    QCOMPARE(started.count(), 1);
    QCOMPARE(moved.count(), 1);
    QCOMPARE(ended.count(), 1);
    QCOMPARE(clicked.count(), 0);
    QCOMPARE(widget.panOffset(), QPointF(7.0, 5.0));
}

void ImageViewWidgetTests::disabledFitHandPanIgnoresGestures()
{
    ImageViewWidget widget;
    configureWidget(&widget);
    widget.setFitHandPanEnabled(false);
    QSignalSpy started(&widget, &ImageViewWidget::fitHandPanStarted);
    QSignalSpy moved(&widget, &ImageViewWidget::fitHandPanMoved);
    QSignalSpy ended(&widget, &ImageViewWidget::fitHandPanEnded);
    QSignalSpy panChanged(&widget, &ImageViewWidget::panChanged);

    QTest::mousePress(&widget, Qt::MiddleButton, Qt::NoModifier, QPoint(20, 20));
    QTest::mouseMove(&widget, QPoint(26, 28));
    QTest::mouseRelease(&widget, Qt::MiddleButton, Qt::NoModifier, QPoint(26, 28));

    QCOMPARE(started.count(), 0);
    QCOMPARE(moved.count(), 0);
    QCOMPARE(ended.count(), 0);
    QCOMPARE(panChanged.count(), 0);
    QCOMPARE(widget.panOffset(), QPointF());
}

QTEST_MAIN(ImageViewWidgetTests)

#include "image_view_widget_tests.moc"
