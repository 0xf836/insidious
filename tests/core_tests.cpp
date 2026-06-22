#include "core/folder_collection_source.h"
#include "core/image_formats.h"
#include "core/memory_image_cache.h"
#include "core/natural_sort.h"
#include "core/vips_image_decoder.h"
#include "core/viewer_controller.h"

#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

#include <vips/vips8>

using namespace insidious::core;

class CoreTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void naturalSortOrdersNumbers();
    void supportedExtensionsAreMvpOnly();
    void folderScanIsNonRecursiveAndSorted();
    void viewerNavigationStopsAtBounds();
    void memoryCacheEvictsByBudget();
    void decoderLoadsTemporaryPng();
};

void CoreTests::initTestCase()
{
    if (VIPS_INIT("insidious_core_tests")) {
        QFAIL(vips_error_buffer());
    }
}

void CoreTests::cleanupTestCase()
{
    vips_shutdown();
}

void CoreTests::naturalSortOrdersNumbers()
{
    QStringList names = {
        QStringLiteral("image-10.png"),
        QStringLiteral("image-2.png"),
        QStringLiteral("Image-1.png"),
        QStringLiteral("image-001.png"),
        QStringLiteral("image-02.png"),
    };

    std::stable_sort(names.begin(), names.end(), naturalLess);

    QCOMPARE(names.at(0), QStringLiteral("Image-1.png"));
    QCOMPARE(names.at(1), QStringLiteral("image-001.png"));
    QCOMPARE(names.at(2), QStringLiteral("image-2.png"));
    QCOMPARE(names.at(3), QStringLiteral("image-02.png"));
    QCOMPARE(names.at(4), QStringLiteral("image-10.png"));
}

void CoreTests::supportedExtensionsAreMvpOnly()
{
    QVERIFY(isSupportedImageExtension(QStringLiteral("jpg")));
    QVERIFY(isSupportedImageExtension(QStringLiteral(".JPEG")));
    QVERIFY(isSupportedImageExtension(QStringLiteral("png")));
    QVERIFY(isSupportedImageExtension(QStringLiteral("webp")));
    QVERIFY(isSupportedImageExtension(QStringLiteral("bmp")));

    QVERIFY(!isSupportedImageExtension(QStringLiteral("gif")));
    QVERIFY(!isSupportedImageExtension(QStringLiteral("tiff")));
    QVERIFY(!isSupportedImageExtension(QStringLiteral("avif")));
    QVERIFY(!isSupportedImageExtension(QStringLiteral("jxl")));
    QVERIFY(!isSupportedImageExtension(QStringLiteral("raw")));
}

void CoreTests::folderScanIsNonRecursiveAndSorted()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir root(tempDir.path());
    QVERIFY(root.mkpath(QStringLiteral("nested")));

    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(root.filePath(QStringLiteral("10.png"))));
    QVERIFY(image.save(root.filePath(QStringLiteral("2.png"))));
    QVERIFY(image.save(root.filePath(QStringLiteral("nested/1.png"))));
    QFile textFile(root.filePath(QStringLiteral("note.txt")));
    QVERIFY(textFile.open(QIODevice::WriteOnly));
    textFile.write("not an image");
    textFile.close();

    const ScanResult result = scanFolder(tempDir.path());

    QVERIFY2(result.ok(), qPrintable(result.errorMessage));
    QCOMPARE(result.entries.size(), 2);
    QCOMPARE(result.entries.at(0).displayName, QStringLiteral("2.png"));
    QCOMPARE(result.entries.at(1).displayName, QStringLiteral("10.png"));
}

void CoreTests::viewerNavigationStopsAtBounds()
{
    ViewerController controller;
    QVector<ImageEntry> entries;
    entries.append(ImageEntry { QStringLiteral("1.png"), QStringLiteral("/tmp/1.png"), 1, QDateTime(), QStringLiteral("PNG") });
    entries.append(ImageEntry { QStringLiteral("2.png"), QStringLiteral("/tmp/2.png"), 1, QDateTime(), QStringLiteral("PNG") });

    controller.setCollection(entries, 0);
    QCOMPARE(controller.currentIndex(), 0);
    controller.setCustomZoom(2.5);
    QCOMPARE(controller.zoomMode(), ZoomMode::Custom);
    QCOMPARE(controller.zoomFactor(), 2.5);
    QVERIFY(!controller.goPrevious());
    QVERIFY(controller.goNext());
    QCOMPARE(controller.currentIndex(), 1);
    QVERIFY(!controller.goNext());
    QVERIFY(controller.goFirst());
    QCOMPARE(controller.currentIndex(), 0);
    QVERIFY(controller.goLast());
    QCOMPARE(controller.currentIndex(), 1);
}

void CoreTests::memoryCacheEvictsByBudget()
{
    MemoryImageCache cache(64);

    DecodedImage first;
    first.image = QImage(4, 4, QImage::Format_RGBA8888);
    first.sourceSize = first.image.size();
    first.displaySize = first.image.size();

    DecodedImage second = first;

    cache.insert(QStringLiteral("first"), first);
    QCOMPARE(cache.size(), 1);

    cache.insert(QStringLiteral("second"), second);
    QCOMPARE(cache.size(), 1);
    QVERIFY(!cache.contains(QStringLiteral("first")));
    QVERIFY(cache.contains(QStringLiteral("second")));
}

void CoreTests::decoderLoadsTemporaryPng()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("sample.png"));

    QImage source(16, 8, QImage::Format_RGB32);
    source.fill(Qt::blue);
    QVERIFY(source.save(path));

    const ImageEntry entry = imageEntryFromFileInfo(QFileInfo(path));
    DecodeRequest request;
    request.viewportSize = QSize(8, 8);
    request.fitToWindow = true;

    VipsImageDecoder decoder;
    const DecodedImage decoded = decoder.decode(entry, request);
    QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
    QCOMPARE(decoded.sourceSize, QSize(16, 8));
    QVERIFY(decoded.displaySize.width() <= 8);
    QVERIFY(decoded.displaySize.height() <= 8);
}

QTEST_MAIN(CoreTests)

#include "core_tests.moc"
