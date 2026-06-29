#include "core/archive_collection_source.h"
#include "core/app_settings.h"
#include "core/folder_collection_source.h"
#include "core/image_entry_validation.h"
#include "core/image_formats.h"
#include "core/memory_image_cache.h"
#include "core/natural_sort.h"
#include "core/render_memory_budget.h"
#include "core/vips_image_decoder.h"
#include "core/viewer_controller.h"

#include <QDir>
#include <QBuffer>
#include <QColor>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <archive.h>
#include <archive_entry.h>
#include <limits>
#include <vips/vips8>

using namespace insidious::core;

class CoreTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void naturalSortOrdersNumbers();
    void supportedExtensionsAreMvpOnly();
    void archiveExtensionsAreSupportedSeparately();
    void entryLoadValidationReportsMissingAndDirectory();
    void folderScanIsNonRecursiveAndSorted();
    void fileSystemSourceTreeLoadsFoldersLazily();
    void archiveScanFiltersImagesAndSorts();
    void archiveSourceTreeGroupsImagesByFolder();
    void archivePasswordIsRequiredAndAccepted();
    void viewerNavigationStopsAtBounds();
    void viewerCustomZoomClampsToSupportedRange();
    void memoryCacheEvictsByBudget();
    void memoryCacheConditionalInsertAcceptsCurrentGeneration();
    void memoryCacheConditionalInsertRejectsStaleGeneration();
    void renderMemoryEstimateUsesConservativeRasterBytes();
    void renderMemoryEstimateCeilsScaledSize();
    void renderMemoryBudgetTreatsOverflowAsExceeded();
    void decoderLoadsTemporaryPng();
    void decoderImageBufferOutlivesDecodedImageCopies();
    void decoderLoadsPngFromArchive();
    void decoderReportsRenderTooLargeReason();
    void decoderRejectsBrokenSupportedFile();
    void archiveScanRejectsBrokenArchive();
    void decodeRequestCacheKeyIncludesExifAutorotate();
    void shortcutSettingsDefaultsAndCustomization();
    void appSettingsRoundTripsThroughQSettings();
    void appSettingsFallbacksAndClampsInvalidValues();
};

namespace {

QByteArray pngBytes(QSize size = QSize(4, 4), QColor color = Qt::green)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

bool writeZipArchive(const QString &path, const QVector<QPair<QString, QByteArray>> &files)
{
    archive *writer = archive_write_new();
    archive_write_add_filter_none(writer);
    archive_write_set_format_zip(writer);

    const QByteArray archivePath = path.toUtf8();
    if (archive_write_open_filename(writer, archivePath.constData()) != ARCHIVE_OK) {
        archive_write_free(writer);
        return false;
    }

    for (const auto &file : files) {
        archive_entry *entry = archive_entry_new();
        const QByteArray entryPath = file.first.toUtf8();
        archive_entry_set_pathname_utf8(entry, entryPath.constData());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_entry_set_size(entry, file.second.size());

        if (archive_write_header(writer, entry) != ARCHIVE_OK) {
            archive_entry_free(entry);
            archive_write_close(writer);
            archive_write_free(writer);
            return false;
        }
        archive_write_data(writer, file.second.constData(), file.second.size());
        archive_entry_free(entry);
    }

    archive_write_close(writer);
    archive_write_free(writer);
    return true;
}

bool writeEncryptedZipArchive(const QString &archivePath, const QString &entryName, const QByteArray &data, const QString &password)
{
    const QString zip = QStandardPaths::findExecutable(QStringLiteral("zip"));
    if (zip.isEmpty()) {
        return false;
    }

    const QFileInfo archiveInfo(archivePath);
    QDir dir(archiveInfo.absolutePath());
    const QString sourcePath = dir.filePath(entryName);
    QFile source(sourcePath);
    if (!source.open(QIODevice::WriteOnly)) {
        return false;
    }
    source.write(data);
    source.close();

    const QStringList arguments = {
        QStringLiteral("-q"),
        QStringLiteral("-j"),
        QStringLiteral("-P"),
        password,
        archivePath,
        sourcePath,
    };
    return QProcess::execute(zip, arguments) == 0;
}

} // namespace

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

void CoreTests::archiveExtensionsAreSupportedSeparately()
{
    QVERIFY(isSupportedArchiveExtension(QStringLiteral("zip")));
    QVERIFY(isSupportedArchiveExtension(QStringLiteral(".7Z")));
    QVERIFY(isSupportedArchiveExtension(QStringLiteral("rar")));

    QVERIFY(!isSupportedImageExtension(QStringLiteral("zip")));
    QVERIFY(!isSupportedArchiveExtension(QStringLiteral("png")));
}

void CoreTests::entryLoadValidationReportsMissingAndDirectory()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    ImageEntry missing;
    missing.displayName = QStringLiteral("missing.png");
    missing.absolutePath = QDir(tempDir.path()).filePath(QStringLiteral("missing.png"));
    const EntryValidationResult missingResult = validateImageEntryLoadable(missing);
    QVERIFY(!missingResult.ok());
    QCOMPARE(missingResult.errorMessage, QStringLiteral("File no longer exists."));

    ImageEntry directory;
    directory.displayName = QStringLiteral("nested");
    directory.absolutePath = tempDir.path();
    const EntryValidationResult directoryResult = validateImageEntryLoadable(directory);
    QVERIFY(!directoryResult.ok());
    QCOMPARE(directoryResult.errorMessage, QStringLiteral("Path is not a regular file."));
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

void CoreTests::fileSystemSourceTreeLoadsFoldersLazily()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir root(tempDir.path());
    QVERIFY(root.mkpath(QStringLiteral("nested/deeper")));

    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(root.filePath(QStringLiteral("root.png"))));
    QVERIFY(image.save(root.filePath(QStringLiteral("nested/child.png"))));
    QVERIFY(image.save(root.filePath(QStringLiteral("nested/deeper/grandchild.png"))));

    const CollectionTreeResult tree = scanFileSystemSourceTree(tempDir.path());
    QVERIFY2(tree.ok(), qPrintable(tree.errorMessage));
    QVERIFY(tree.root != nullptr);
    QCOMPARE(tree.root->images.size(), 1);
    QCOMPARE(tree.root->images.first().displayName, QStringLiteral("root.png"));
    QCOMPARE(tree.root->children.size(), 1);
    QCOMPARE(tree.root->children.first()->relativePath, QStringLiteral("nested"));
    QVERIFY(!tree.root->children.first()->loaded);

    const FolderLoadResult nested = loadFileSystemFolder(tempDir.path(), QStringLiteral("nested"));
    QVERIFY2(nested.ok(), qPrintable(nested.errorMessage));
    QCOMPARE(nested.images.size(), 1);
    QCOMPARE(nested.images.first().displayName, QStringLiteral("child.png"));
    QCOMPARE(nested.images.first().sourceFolderPath, QStringLiteral("nested"));
    QCOMPARE(nested.children.size(), 1);
    QCOMPARE(nested.children.first()->relativePath, QStringLiteral("nested/deeper"));
}

void CoreTests::archiveScanFiltersImagesAndSorts()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("sample.zip"));

    QVERIFY(writeZipArchive(path, {
        { QStringLiteral("images/10.png"), pngBytes() },
        { QStringLiteral("images/2.png"), pngBytes() },
        { QStringLiteral("notes/readme.txt"), QByteArray("not an image") },
    }));

    const ScanResult result = scanArchive(path);

    QVERIFY2(result.ok(), qPrintable(result.errorMessage));
    QCOMPARE(result.entries.size(), 2);
    QCOMPARE(result.entries.at(0).displayName, QStringLiteral("images/2.png"));
    QCOMPARE(result.entries.at(1).displayName, QStringLiteral("images/10.png"));
    QCOMPARE(result.entries.at(0).source, ImageEntrySource::Archive);
}

void CoreTests::archiveSourceTreeGroupsImagesByFolder()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("sample.zip"));

    QVERIFY(writeZipArchive(path, {
        { QStringLiteral("images/10.png"), pngBytes() },
        { QStringLiteral("images/2.png"), pngBytes() },
        { QStringLiteral("other/1.png"), pngBytes() },
        { QStringLiteral("notes/readme.txt"), QByteArray("not an image") },
    }));

    const CollectionTreeResult tree = scanArchiveSourceTree(path);

    QVERIFY2(tree.ok(), qPrintable(tree.errorMessage));
    QVERIFY(tree.root != nullptr);
    QCOMPARE(tree.root->children.size(), 2);
    QCOMPARE(tree.root->children.at(0)->displayName, QStringLiteral("images"));
    QCOMPARE(tree.root->children.at(0)->images.size(), 2);
    QCOMPARE(tree.root->children.at(0)->images.at(0).displayName, QStringLiteral("2.png"));
    QCOMPARE(tree.root->children.at(0)->images.at(0).sourceRelativePath, QStringLiteral("images/2.png"));
    QCOMPARE(tree.root->children.at(1)->displayName, QStringLiteral("other"));
    QCOMPARE(tree.selectedFolderPath, QStringLiteral("images"));
}

void CoreTests::archivePasswordIsRequiredAndAccepted()
{
    if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()) {
        QSKIP("zip command is not available");
    }

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("secret.zip"));
    QVERIFY(writeEncryptedZipArchive(path, QStringLiteral("secret.png"), pngBytes(QSize(10, 5), Qt::blue), QStringLiteral("secret")));

    const ScanResult withoutPassword = scanArchive(path);
    QVERIFY(!withoutPassword.ok());
    QCOMPARE(withoutPassword.errorReason, ScanErrorReason::PasswordRequired);

    const ScanResult wrongPassword = scanArchive(path, QStringLiteral("wrong"));
    QVERIFY(!wrongPassword.ok());
    QCOMPARE(wrongPassword.errorReason, ScanErrorReason::InvalidPassword);

    const ScanResult correctPassword = scanArchive(path, QStringLiteral("secret"));
    QVERIFY2(correctPassword.ok(), qPrintable(correctPassword.errorMessage));
    QCOMPARE(correctPassword.entries.size(), 1);

    const CollectionTreeResult correctTree = scanArchiveSourceTree(path, QStringLiteral("secret"));
    QVERIFY2(correctTree.ok(), qPrintable(correctTree.errorMessage));
    QVERIFY(correctTree.root != nullptr);
    QCOMPARE(correctTree.root->images.size(), 1);

    const ArchiveEntryReadResult wrongRead = readArchiveEntry(correctPassword.entries.first(), QStringLiteral("wrong"));
    QVERIFY(!wrongRead.ok());
    QCOMPARE(wrongRead.errorReason, ArchiveEntryReadErrorReason::InvalidPassword);

    DecodeRequest request;
    request.viewportSize = QSize(5, 5);
    request.fitToWindow = true;

    VipsImageDecoder decoder;
    const DecodedImage decoded = decoder.decode(correctPassword.entries.first(), request, QStringLiteral("secret"));
    QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
    QCOMPARE(decoded.sourceSize, QSize(10, 5));
}

void CoreTests::viewerNavigationStopsAtBounds()
{
    ViewerController controller;
    QVector<ImageEntry> entries;
    ImageEntry first;
    first.displayName = QStringLiteral("1.png");
    first.absolutePath = QStringLiteral("/tmp/1.png");
    first.fileSize = 1;
    first.formatHint = QStringLiteral("PNG");
    entries.append(first);

    ImageEntry second;
    second.displayName = QStringLiteral("2.png");
    second.absolutePath = QStringLiteral("/tmp/2.png");
    second.fileSize = 1;
    second.formatHint = QStringLiteral("PNG");
    entries.append(second);

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

void CoreTests::viewerCustomZoomClampsToSupportedRange()
{
    ViewerController controller;

    controller.setCustomZoom(MinimumZoomFactor / 10.0);
    QCOMPARE(controller.zoomMode(), ZoomMode::Custom);
    QCOMPARE(controller.zoomFactor(), MinimumZoomFactor);

    controller.setCustomZoom(MaximumZoomFactor * 10.0);
    QCOMPARE(controller.zoomMode(), ZoomMode::Custom);
    QCOMPARE(controller.zoomFactor(), MaximumZoomFactor);
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

void CoreTests::memoryCacheConditionalInsertAcceptsCurrentGeneration()
{
    MemoryImageCache cache(1024);

    DecodedImage image;
    image.image = QImage(4, 4, QImage::Format_RGBA8888);
    image.sourceSize = image.image.size();
    image.displaySize = image.image.size();

    const quint64 generation = cache.generation();
    QVERIFY(cache.insertIfGeneration(QStringLiteral("current"), image, generation));
    QVERIFY(cache.contains(QStringLiteral("current")));
}

void CoreTests::memoryCacheConditionalInsertRejectsStaleGeneration()
{
    MemoryImageCache cache(1024);

    DecodedImage image;
    image.image = QImage(4, 4, QImage::Format_RGBA8888);
    image.sourceSize = image.image.size();
    image.displaySize = image.image.size();

    const quint64 staleGeneration = cache.generation();
    cache.clear();

    QVERIFY(!cache.insertIfGeneration(QStringLiteral("stale"), image, staleGeneration));
    QVERIFY(!cache.contains(QStringLiteral("stale")));
}

void CoreTests::renderMemoryEstimateUsesConservativeRasterBytes()
{
    const RenderMemoryEstimate estimate = estimateRenderMemory(QSize(16, 8), 3, 1.0);

    QVERIFY(!estimate.overflowed);
    QCOMPARE(estimate.outputSize, QSize(16, 8));
    QCOMPARE(estimate.bytesPerPixel, 4);
    QCOMPARE(estimate.bytes, 16LL * 8LL * 4LL);
    QVERIFY(!exceedsRenderMemoryBudget(QSize(16, 8), 3, 1.0, estimate.bytes));
    QVERIFY(exceedsRenderMemoryBudget(QSize(16, 8), 3, 1.0, estimate.bytes - 1));
}

void CoreTests::renderMemoryEstimateCeilsScaledSize()
{
    const RenderMemoryEstimate estimate = estimateRenderMemory(QSize(10, 7), 4, 1.25);

    QVERIFY(!estimate.overflowed);
    QCOMPARE(estimate.outputSize, QSize(13, 9));
    QCOMPARE(estimate.bytesPerPixel, 4);
    QCOMPARE(estimate.bytes, 13LL * 9LL * 4LL);
}

void CoreTests::renderMemoryBudgetTreatsOverflowAsExceeded()
{
    const RenderMemoryEstimate estimate = estimateRenderMemory(
        QSize(std::numeric_limits<int>::max(), std::numeric_limits<int>::max()),
        4,
        2.0);

    QVERIFY(estimate.overflowed);
    QCOMPARE(estimate.bytes, std::numeric_limits<qint64>::max());
    QVERIFY(exceedsRenderMemoryBudget(
        QSize(std::numeric_limits<int>::max(), std::numeric_limits<int>::max()),
        4,
        2.0,
        std::numeric_limits<qint64>::max()));
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

void CoreTests::decoderImageBufferOutlivesDecodedImageCopies()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("sample.png"));

    QImage source(8, 8, QImage::Format_RGB32);
    source.fill(Qt::blue);
    QVERIFY(source.save(path));

    QImage retained;
    {
        const ImageEntry entry = imageEntryFromFileInfo(QFileInfo(path));
        DecodeRequest request;
        request.viewportSize = QSize(8, 8);
        request.fitToWindow = true;

        VipsImageDecoder decoder;
        const DecodedImage decoded = decoder.decode(entry, request);
        QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
        retained = decoded.image;
    }

    QVERIFY(!retained.isNull());
    QCOMPARE(QColor(retained.pixel(0, 0)), QColor(Qt::blue));
}

void CoreTests::decoderLoadsPngFromArchive()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("sample.zip"));
    QVERIFY(writeZipArchive(path, {
        { QStringLiteral("sample.png"), pngBytes(QSize(12, 6), Qt::blue) },
    }));

    const ScanResult scan = scanArchive(path);
    QVERIFY2(scan.ok(), qPrintable(scan.errorMessage));
    QCOMPARE(scan.entries.size(), 1);

    DecodeRequest request;
    request.viewportSize = QSize(6, 6);
    request.fitToWindow = true;

    VipsImageDecoder decoder;
    const DecodedImage decoded = decoder.decode(scan.entries.first(), request);
    QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
    QCOMPARE(decoded.sourceSize, QSize(12, 6));
    QVERIFY(decoded.displaySize.width() <= 6);
    QVERIFY(decoded.displaySize.height() <= 6);
}

void CoreTests::decoderReportsRenderTooLargeReason()
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
    request.fitToWindow = false;
    request.scaleFactor = 100.0;
    request.maxDecodedBytes = 1024;

    VipsImageDecoder decoder;
    const DecodedImage decoded = decoder.decode(entry, request);
    QVERIFY(!decoded.ok());
    QVERIFY(!decoded.errorMessage.isEmpty());
    QCOMPARE(decoded.errorReason, DecodeErrorReason::RenderTooLarge);
}

void CoreTests::decoderRejectsBrokenSupportedFile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("broken.png"));

    QFile broken(path);
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("not an image");
    broken.close();

    const ImageEntry entry = imageEntryFromFileInfo(QFileInfo(path));
    DecodeRequest request;
    request.viewportSize = QSize(8, 8);
    request.fitToWindow = true;

    VipsImageDecoder decoder;
    const DecodedImage decoded = decoder.decode(entry, request);
    QVERIFY(!decoded.ok());
    QVERIFY(!decoded.errorMessage.isEmpty());
    QCOMPARE(decoded.errorReason, DecodeErrorReason::Other);
}

void CoreTests::archiveScanRejectsBrokenArchive()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("broken.zip"));

    QFile broken(path);
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("not an archive");
    broken.close();

    const ScanResult result = scanArchive(path);
    QVERIFY(!result.ok());
    QVERIFY(!result.errorMessage.isEmpty());
}

void CoreTests::decodeRequestCacheKeyIncludesExifAutorotate()
{
    DecodeRequest enabled;
    enabled.viewportSize = QSize(800, 600);
    enabled.scaleFactor = 1.5;
    enabled.rotationDegrees = 90;
    enabled.fitToWindow = false;
    enabled.exifAutorotate = true;

    DecodeRequest disabled = enabled;
    disabled.exifAutorotate = false;

    QVERIFY(enabled.cacheKeySuffix() != disabled.cacheKeySuffix());
}

void CoreTests::shortcutSettingsDefaultsAndCustomization()
{
    AppSettings values = AppSettings::defaults();

    QCOMPARE(shortcutSequences(values.shortcuts, ShortcutCommand::PreviousImage), defaultShortcutSequences(ShortcutCommand::PreviousImage));
    QCOMPARE(shortcutSequences(values.shortcuts, ShortcutCommand::PreviousFolder), QStringList({ QStringLiteral("MouseBack") }));
    QCOMPARE(shortcutSequences(values.shortcuts, ShortcutCommand::NextFolder), QStringList({ QStringLiteral("MouseForward") }));
    QCOMPARE(shortcutSequences(values.shortcuts, ShortcutCommand::ExifAutorotate), QStringList());

    setShortcutSequences(&values.shortcuts, ShortcutCommand::NextImage, QStringList { QStringLiteral("N") });
    QCOMPARE(shortcutSequences(values.shortcuts, ShortcutCommand::NextImage), QStringList({ QStringLiteral("N") }));

    const MouseShortcut mouseShortcut { Qt::ControlModifier, Qt::BackButton };
    QCOMPARE(mouseShortcutToSettingsString(mouseShortcut), QStringLiteral("Ctrl+MouseBack"));
    QVERIFY(shortcutSequenceIsMouseInput(QStringLiteral("Ctrl+MouseBack")));
    MouseShortcut parsedMouseShortcut;
    QVERIFY(mouseShortcutFromSettingsString(QStringLiteral("Ctrl+MouseBack"), &parsedMouseShortcut));
    QCOMPARE(parsedMouseShortcut.modifiers, Qt::KeyboardModifiers(Qt::ControlModifier));
    QCOMPARE(parsedMouseShortcut.button, Qt::BackButton);
    QCOMPARE(shortcutSequenceDisplayString(QStringLiteral("Ctrl+MouseBack")), QStringLiteral("Ctrl+Mouse Back"));

    setShortcutSequences(&values.shortcuts, ShortcutCommand::PreviousImage, QStringList { QStringLiteral("Ctrl+MouseBack") });
    QCOMPARE(shortcutSequences(values.shortcuts, ShortcutCommand::PreviousImage), QStringList({ QStringLiteral("Ctrl+MouseBack") }));

    setShortcutSequences(&values.shortcuts, ShortcutCommand::NextImage, QStringList { QStringLiteral("Down"), QStringLiteral("MouseExtra1") });
    QCOMPARE(shortcutSequences(values.shortcuts, ShortcutCommand::NextImage), QStringList({ QStringLiteral("Down"), QStringLiteral("MouseExtra1") }));

    setShortcutSequences(&values.shortcuts, ShortcutCommand::NextImage, QStringList());
    QCOMPARE(shortcutSequences(values.shortcuts, ShortcutCommand::NextImage), QStringList());

    resetShortcutSequences(&values.shortcuts, ShortcutCommand::NextImage);
    QCOMPARE(shortcutSequences(values.shortcuts, ShortcutCommand::NextImage), defaultShortcutSequences(ShortcutCommand::NextImage));
}

void CoreTests::appSettingsRoundTripsThroughQSettings()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString settingsPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));

    AppSettings values = AppSettings::defaults();
    values.viewer.defaultZoomMode = ZoomMode::ActualSize;
    values.viewer.imageSwitchPolicy = ImageSwitchPolicy::PreserveView;
    values.viewer.clickZoomBehavior = ClickZoomBehavior::HoldToZoomPan;
    values.viewer.exifAutorotate = false;
    values.viewer.clickZoomMultiplier = 3.5;
    values.viewer.wheelAction = WheelAction::NoAction;
    values.viewer.ctrlWheelAction = WheelAction::NavigateImages;
    values.viewer.shiftWheelAction = WheelAction::Zoom;
    values.viewer.zoomStepPercent = 40.0;
    values.fullscreen.sideTriggerRatio = 0.12;
    values.fullscreen.verticalTriggerRatio = 0.08;
    values.fullscreen.infoLineEnabled = false;
    values.fullscreen.infoLineFontFamily = QStringLiteral("Serif");
    values.fullscreen.infoLineFontPointSize = 18;
    values.fullscreen.infoLineColor = QStringLiteral("#aabbcc");
    values.interface.rememberLayout = true;
    values.interface.mainWindowGeometry = QByteArray("geometry");
    values.interface.mainWindowState = QByteArray("state");
    values.interface.fileSplitterState = QByteArray("files");
    values.interface.fullscreenFileSplitterState = QByteArray("fullscreen-files");
    setShortcutSequences(&values.shortcuts, ShortcutCommand::NextImage, QStringList { QStringLiteral("N"), QStringLiteral("MouseExtra1") });
    setShortcutSequences(&values.shortcuts, ShortcutCommand::PreviousImage, QStringList { QStringLiteral("MouseExtra2") });
    setShortcutSequences(&values.shortcuts, ShortcutCommand::ExifAutorotate, QStringList());

    QSettings writeSettings(settingsPath, QSettings::IniFormat);
    values.write(writeSettings);
    writeSettings.sync();

    QSettings readSettings(settingsPath, QSettings::IniFormat);
    const AppSettings loaded = AppSettings::read(readSettings);

    QCOMPARE(loaded.viewer.defaultZoomMode, ZoomMode::ActualSize);
    QCOMPARE(loaded.viewer.imageSwitchPolicy, ImageSwitchPolicy::PreserveView);
    QCOMPARE(loaded.viewer.clickZoomBehavior, ClickZoomBehavior::HoldToZoomPan);
    QVERIFY(!loaded.viewer.exifAutorotate);
    QCOMPARE(loaded.viewer.clickZoomMultiplier, 3.5);
    QCOMPARE(loaded.viewer.wheelAction, WheelAction::NoAction);
    QCOMPARE(loaded.viewer.ctrlWheelAction, WheelAction::NavigateImages);
    QCOMPARE(loaded.viewer.shiftWheelAction, WheelAction::Zoom);
    QCOMPARE(loaded.viewer.zoomStepPercent, 40.0);
    QCOMPARE(loaded.fullscreen.sideTriggerRatio, 0.12);
    QCOMPARE(loaded.fullscreen.verticalTriggerRatio, 0.08);
    QVERIFY(!loaded.fullscreen.infoLineEnabled);
    QCOMPARE(loaded.fullscreen.infoLineFontFamily, QStringLiteral("Serif"));
    QCOMPARE(loaded.fullscreen.infoLineFontPointSize, 18);
    QCOMPARE(loaded.fullscreen.infoLineColor, QStringLiteral("#aabbcc"));
    QVERIFY(loaded.interface.rememberLayout);
    QCOMPARE(loaded.interface.mainWindowGeometry, QByteArray("geometry"));
    QCOMPARE(loaded.interface.mainWindowState, QByteArray("state"));
    QCOMPARE(loaded.interface.fileSplitterState, QByteArray("files"));
    QCOMPARE(loaded.interface.fullscreenFileSplitterState, QByteArray("fullscreen-files"));
    QCOMPARE(shortcutSequences(loaded.shortcuts, ShortcutCommand::NextImage), QStringList({ QStringLiteral("N"), QStringLiteral("MouseExtra1") }));
    QCOMPARE(shortcutSequences(loaded.shortcuts, ShortcutCommand::PreviousImage), QStringList({ QStringLiteral("MouseExtra2") }));
    QCOMPARE(shortcutSequences(loaded.shortcuts, ShortcutCommand::ExifAutorotate), QStringList());
}

void CoreTests::appSettingsFallbacksAndClampsInvalidValues()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString settingsPath = QDir(tempDir.path()).filePath(QStringLiteral("settings.ini"));

    QSettings writeSettings(settingsPath, QSettings::IniFormat);
    writeSettings.beginGroup(QStringLiteral("viewer"));
    writeSettings.setValue(QStringLiteral("defaultZoomMode"), QStringLiteral("invalid"));
    writeSettings.setValue(QStringLiteral("imageSwitchPolicy"), QStringLiteral("invalid"));
    writeSettings.setValue(QStringLiteral("clickZoomBehavior"), QStringLiteral("invalid"));
    writeSettings.setValue(QStringLiteral("clickZoomMultiplier"), 100.0);
    writeSettings.setValue(QStringLiteral("wheelAction"), QStringLiteral("invalid"));
    writeSettings.setValue(QStringLiteral("ctrlWheelAction"), QStringLiteral("invalid"));
    writeSettings.setValue(QStringLiteral("shiftWheelAction"), QStringLiteral("invalid"));
    writeSettings.setValue(QStringLiteral("zoomStepPercent"), 999.0);
    writeSettings.endGroup();
    writeSettings.beginGroup(QStringLiteral("fullscreen"));
    writeSettings.setValue(QStringLiteral("sideTriggerRatio"), -1.0);
    writeSettings.setValue(QStringLiteral("verticalTriggerRatio"), 99.0);
    writeSettings.setValue(QStringLiteral("infoLineFontPointSize"), 99);
    writeSettings.setValue(QStringLiteral("infoLineColor"), QStringLiteral("not-a-color"));
    writeSettings.endGroup();
    writeSettings.beginGroup(QStringLiteral("shortcuts"));
    writeSettings.beginGroup(QStringLiteral("nextImage"));
    writeSettings.setValue(QStringLiteral("customized"), true);
    writeSettings.setValue(QStringLiteral("sequences"), QStringList { QString() });
    writeSettings.endGroup();
    writeSettings.beginGroup(QStringLiteral("previousImage"));
    writeSettings.setValue(QStringLiteral("customized"), true);
    writeSettings.setValue(QStringLiteral("sequences"), QStringList());
    writeSettings.endGroup();
    writeSettings.endGroup();
    writeSettings.sync();

    QSettings readSettings(settingsPath, QSettings::IniFormat);
    const AppSettings loaded = AppSettings::read(readSettings);

    QCOMPARE(loaded.viewer.defaultZoomMode, ZoomMode::FitToWindow);
    QCOMPARE(loaded.viewer.imageSwitchPolicy, ImageSwitchPolicy::ResetToBaseMode);
    QCOMPARE(loaded.viewer.clickZoomBehavior, ClickZoomBehavior::ToggleOnClick);
    QVERIFY(loaded.viewer.exifAutorotate);
    QCOMPARE(loaded.viewer.clickZoomMultiplier, 8.0);
    QCOMPARE(loaded.viewer.wheelAction, WheelAction::NavigateImages);
    QCOMPARE(loaded.viewer.ctrlWheelAction, WheelAction::Zoom);
    QCOMPARE(loaded.viewer.shiftWheelAction, WheelAction::NoAction);
    QCOMPARE(loaded.viewer.zoomStepPercent, 100.0);
    QCOMPARE(loaded.fullscreen.sideTriggerRatio, 0.01);
    QCOMPARE(loaded.fullscreen.verticalTriggerRatio, 0.30);
    QVERIFY(loaded.fullscreen.infoLineEnabled);
    QCOMPARE(loaded.fullscreen.infoLineFontPointSize, 32);
    QCOMPARE(loaded.fullscreen.infoLineColor, AppSettings::defaults().fullscreen.infoLineColor);
    QCOMPARE(shortcutSequences(loaded.shortcuts, ShortcutCommand::NextImage), defaultShortcutSequences(ShortcutCommand::NextImage));
    QCOMPARE(shortcutSequences(loaded.shortcuts, ShortcutCommand::PreviousImage), QStringList());

    writeSettings.beginGroup(QStringLiteral("viewer"));
    writeSettings.setValue(QStringLiteral("zoomStepPercent"), 1.0);
    writeSettings.endGroup();
    writeSettings.sync();

    QSettings lowStepReadSettings(settingsPath, QSettings::IniFormat);
    const AppSettings lowStepLoaded = AppSettings::read(lowStepReadSettings);
    QCOMPARE(lowStepLoaded.viewer.zoomStepPercent, 5.0);
}

QTEST_MAIN(CoreTests)

#include "core_tests.moc"
