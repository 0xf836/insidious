#include "ui/thumbnail_list_model.h"

#include <QtTest>

using namespace insidious;

class ThumbnailListModelTests : public QObject {
    Q_OBJECT

private slots:
    void exposesFilenamesAndThumbnailState();
};

void ThumbnailListModelTests::exposesFilenamesAndThumbnailState()
{
    core::ImageEntry first;
    first.displayName = QStringLiteral("first.jpg");
    first.absolutePath = QStringLiteral("/tmp/first.jpg");
    first.sourceRelativePath = QStringLiteral("folder/first.jpg");

    core::ImageEntry second;
    second.displayName = QStringLiteral("second.jpg");
    second.absolutePath = QStringLiteral("/tmp/second.jpg");
    second.sourceRelativePath = QStringLiteral("folder/second.jpg");

    ui::ThumbnailListModel model;
    model.setEntries({ first, second });

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("first.jpg"));
    QCOMPARE(model.data(model.index(1, 0), Qt::ToolTipRole).toString(), QStringLiteral("folder/second.jpg"));
    QVERIFY(!model.hasThumbnail(0));
    QVERIFY(!model.isFailed(0));

    QImage thumbnail(16, 12, QImage::Format_RGB32);
    thumbnail.fill(Qt::red);
    model.setThumbnail(first.identityKey(), thumbnail);
    QVERIFY(model.hasThumbnail(0));
    QVERIFY(!model.data(model.index(0, 0), Qt::DecorationRole).isNull());
    QVERIFY(!model.data(model.index(0, 0), ui::ThumbnailListModel::ThumbnailImageRole).value<QImage>().isNull());

    model.markFailed(second.identityKey());
    QVERIFY(model.isFailed(1));

    model.clearThumbnails();
    QVERIFY(!model.hasThumbnail(0));
    QVERIFY(!model.isFailed(1));
}

QTEST_MAIN(ThumbnailListModelTests)

#include "thumbnail_list_model_tests.moc"
