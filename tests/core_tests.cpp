#include "core/archive_collection_source.h"
#include "core/archive_entry_stream.h"
#include "core/archive_read_error.h"
#include "core/app_settings.h"
#include "core/animation_decode_session.h"
#include "core/folder_collection_source.h"
#include "core/image_entry_validation.h"
#include "core/image_formats.h"
#include "core/image_sort.h"
#include "core/memory_image_cache.h"
#include "core/multi_collection_source.h"
#include "core/natural_sort.h"
#include "core/render_memory_budget.h"
#include "core/vips_image_decoder.h"
#include "core/jxl_rs_image_decoder.h"
#include "core/vips_raster_renderer.h"
#include "core/viewer_controller.h"

#include <QDir>
#include <QDataStream>
#include <QBuffer>
#include <QColor>
#include <QColorSpace>
#include <QFileInfo>
#include <QImage>
#include <QMimeDatabase>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <QtEndian>
#include <QtConcurrent>

#include <jxl/encode.h>
#include <archive.h>
#include <archive_entry.h>
#include <array>
#include <cerrno>
#include <cmath>
#include <limits>
#include <vips/vips8>

using namespace insidious::core;

class CoreTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void naturalSortOrdersNumbers();
    void supportedExtensionsUseExplicitAllowlist();
    void archiveExtensionsAreSupportedSeparately();
    void entryLoadValidationReportsMissingAndDirectory();
    void folderScanIsNonRecursiveAndSorted();
    void imageSortOrdersByNameTypeSizeAndDate();
    void fileSystemSourceTreeLoadsFoldersLazily();
    void fileListSourceTreePreservesInputOrder();
    void folderListSourceTreePreservesTopLevelOrder();
    void archiveScanFiltersImagesAndSorts();
    void archiveSourceTreeGroupsImagesByFolder();
    void archiveListSourceTreePreservesArchiveRoots();
    void archiveListSourceTreeUsesSharedPassword();
    void archivePasswordIsRequiredAndAccepted();
    void archiveErrorsKeepTheirReason_data();
    void archiveErrorsKeepTheirReason();
    void encryptedArchiveCapabilities_data();
    void encryptedArchiveCapabilities();
    void encryptedArchiveCorruptionIsNotInvalidPassword();
    void viewerNavigationStopsAtBounds();
    void viewerRotationNormalizesAndResetsOnImageSwitch();
    void viewerCustomZoomClampsToSupportedRange();
    void memoryCacheEvictsByBudget();
    void memoryCacheConditionalInsertAcceptsCurrentGeneration();
    void memoryCacheConditionalInsertRejectsStaleGeneration();
    void renderMemoryEstimateUsesConservativeRasterBytes();
    void renderMemoryEstimateCeilsScaledSize();
    void renderMemoryBudgetTreatsOverflowAsExceeded();
    void decoderLoadsTemporaryPng();
    void decoderColorManagementTagsSrgb();
    void decoderColorManagementCanBeDisabled();
    void decoderReportsEmbeddedIccConversion();
    void decoderKeepsInvalidIccImageViewable();
    void rarImageRoundTrip_data();
    void rarImageRoundTrip();
    void dibVariants_data();
    void dibVariants();
    void decoderSupportedFormats_data();
    void decoderSupportedFormats();
    void decoderTiffLayouts_data();
    void decoderTiffLayouts();
    void decoderGifFirstFrame_data();
    void decoderGifFirstFrame();
    void decoderModernFormats_data();
    void decoderModernFormats();
    void decoderJxlRsErrors();
    void decoderJxlAnimationLimitKeepsPoster();
    void decoderJxlStreamingAndMetadata();
    void decoderJxlThumbnailBudgetAndConcurrentSources();
    void decoderJxlOrientationAndRegion();
    void decoderModernFormatFrames_data();
    void decoderModernFormatFrames();
    void decoderExifRotationAndRegion_data();
    void decoderExifRotationAndRegion();
    void decoderTransformedSampling_data();
    void decoderTransformedSampling();
    void decoderRegionFilteringWithoutOverscan();
    void decoderNativeIntermediateBudget();
    void decoderFitHonorsDevicePixelRatio();
    void decoderActualSizeHonorsDevicePixelRatio();
    void decoderAppliesViewRotation();
    void decoderImageBufferOutlivesDecodedImageCopies();
    void decoderLoadsPngFromArchive();
    void archiveEntryStreamReadsSequentially();
    void decoderLoadsArchiveEntryWhenBoundedReadWouldFail();
    void decoderReportsMissingArchiveEntry();
    void decoderRegionRenderingLimitsZoomOutput();
    void decoderRegionRenderingHonorsDevicePixelRatio();
    void decoderReportsRenderTooLargeReason();
    void decoderUsesConfiguredScalingQuality();
    void decoderUsesConfiguredScalingQuality_data();
    void decoderRejectsBrokenSupportedFile();
    void archiveScanRejectsBrokenArchive();
    void decodeRequestCacheKeyIncludesExifAutorotate();
    void decodeRequestCacheKeyIncludesRotation();
    void decodeRequestCacheKeyIncludesRegionAndPan();
    void decodeRequestCacheKeyIncludesScalingQuality();
    void decodeRequestCacheKeyIncludesColorManagement();
    void decodeRequestCacheKeyIncludesDevicePixelRatio();
    void prefetchPolicyAllowsOnlyFitSizedRequests();
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

bool writeZipArchive(const QString &path, const QVector<QPair<QString, QByteArray>> &files, bool sevenZip = false)
{
    archive *writer = archive_write_new();
    archive_write_add_filter_none(writer);
    if (sevenZip) {
        archive_write_set_format_7zip(writer);
    } else {
        archive_write_set_format_zip(writer);
    }

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

bool writeSrgbIccTaggedPng(const QString &sourcePath, const QString &targetPath)
{
    try {
        vips::VImage image = vips::VImage::new_from_file(sourcePath.toUtf8().constData());
        image = image.icc_transform(
            "srgb",
            vips::VImage::option()
                ->set("intent", VIPS_INTENT_RELATIVE)
                ->set("black_point_compensation", TRUE));
        image.write_to_file(targetPath.toUtf8().constData());
        return true;
    } catch (const vips::VError &) {
        return false;
    }
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

void CoreTests::supportedExtensionsUseExplicitAllowlist()
{
    QVERIFY(isSupportedImageExtension(QStringLiteral("jpg")));
    QVERIFY(isSupportedImageExtension(QStringLiteral(".JPEG")));
    QVERIFY(isSupportedImageExtension(QStringLiteral("png")));
    QVERIFY(isSupportedImageExtension(QStringLiteral("webp")));
    QVERIFY(isSupportedImageExtension(QStringLiteral("bmp")));
    QVERIFY(isSupportedImageExtension(QStringLiteral("dib")));
    QCOMPARE(imageMimeTypeFromPath(QStringLiteral("one.jpeg")), QStringLiteral("image/jpeg"));
    QCOMPARE(imageMimeTypeFromPath(QStringLiteral("two.PNG")), QStringLiteral("image/png"));
    QCOMPARE(imageMimeTypeFromPath(QStringLiteral("three.webp")), QStringLiteral("image/webp"));
    QCOMPARE(imageMimeTypeFromPath(QStringLiteral("four.dib")), QStringLiteral("image/bmp"));

    QVERIFY(isSupportedImageExtension(QStringLiteral(".GIF")));
    QCOMPARE(imageMimeTypeFromPath(QStringLiteral("one.GIF")), QStringLiteral("image/gif"));
    QVERIFY(supportedImageNameFilters().first().contains(QStringLiteral("*.gif")));
    QVERIFY(supportedOpenNameFilters().first().contains(QStringLiteral("*.gif")));
    QVERIFY(isSupportedImageExtension(QStringLiteral(".TIFF")));
    QCOMPARE(imageMimeTypeFromPath(QStringLiteral("one.TIF")), QStringLiteral("image/tiff"));
    QCOMPARE(imageMimeTypeFromPath(QStringLiteral("one.TIFF")), QStringLiteral("image/tiff"));
    QVERIFY(isSupportedImageExtension(QStringLiteral(".AVIF")));
    QVERIFY(isSupportedImageExtension(QStringLiteral("JXL")));
    QCOMPARE(imageMimeTypeFromPath(QStringLiteral("five.AVIF")), QStringLiteral("image/avif"));
    QCOMPARE(imageMimeTypeFromPath(QStringLiteral("six.jxl")), QStringLiteral("image/jxl"));
    QCOMPARE(formatHintFromSuffix(QStringLiteral(".AVIF")), QStringLiteral("AVIF"));
    QCOMPARE(formatHintFromSuffix(QStringLiteral("jxl")), QStringLiteral("JXL"));
    for (const QString &filter : { supportedImageNameFilters().first(), supportedOpenNameFilters().first() }) {
        QVERIFY(filter.contains(QStringLiteral("*.avif")));
        QVERIFY(filter.contains(QStringLiteral("*.jxl")));
    }
    for (const QString &suffix : { QStringLiteral("jp2"), QStringLiteral("j2k"), QStringLiteral("heic"), QStringLiteral("heif"), QStringLiteral("gif"), QStringLiteral("tif"), QStringLiteral("tiff") }) {
        QVERIFY(isSupportedImageExtension(suffix.toUpper()));
        QVERIFY(supportedOpenNameFilters().first().contains(QStringLiteral("*.") + suffix));
    }
    QCOMPARE(imageMimeTypeFromPath(QStringLiteral("one.jp2")), QStringLiteral("image/jp2"));
    QCOMPARE(imageMimeTypeFromPath(QStringLiteral("one.j2k")), QStringLiteral("image/x-jp2-codestream"));
    QCOMPARE(imageMimeTypeFromPath(QStringLiteral("one.HEIC")), QStringLiteral("image/heic"));
    QCOMPARE(imageMimeTypeFromPath(QStringLiteral("one.heif")), QStringLiteral("image/heif"));
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

void CoreTests::imageSortOrdersByNameTypeSizeAndDate()
{
    QVector<ImageEntry> entries;

    ImageEntry png10;
    png10.displayName = QStringLiteral("10.png");
    png10.formatHint = QStringLiteral("PNG");
    png10.fileSize = 10;
    png10.modifiedTime = QDateTime::fromSecsSinceEpoch(100);
    entries.append(png10);

    ImageEntry jpeg2;
    jpeg2.displayName = QStringLiteral("2.jpg");
    jpeg2.formatHint = QStringLiteral("JPEG");
    jpeg2.fileSize = 20;
    jpeg2.modifiedTime = QDateTime::fromSecsSinceEpoch(200);
    entries.append(jpeg2);

    ImageEntry webp1;
    webp1.displayName = QStringLiteral("1.webp");
    webp1.formatHint = QStringLiteral("WebP");
    webp1.fileSize = 20;
    webp1.modifiedTime = {};
    entries.append(webp1);

    auto names = [](const QVector<ImageEntry> &values) {
        QStringList result;
        for (const ImageEntry &entry : values) {
            result.append(entry.displayName);
        }
        return result;
    };

    QVector<ImageEntry> sorted = entries;
    sortImageEntries(&sorted, { ImageSortKey::Name, SortDirection::Ascending });
    QCOMPARE(names(sorted), QStringList({ QStringLiteral("1.webp"), QStringLiteral("2.jpg"), QStringLiteral("10.png") }));

    sorted = entries;
    sortImageEntries(&sorted, { ImageSortKey::Name, SortDirection::Descending });
    QCOMPARE(names(sorted), QStringList({ QStringLiteral("10.png"), QStringLiteral("2.jpg"), QStringLiteral("1.webp") }));

    sorted = entries;
    sortImageEntries(&sorted, { ImageSortKey::Type, SortDirection::Ascending });
    QCOMPARE(names(sorted), QStringList({ QStringLiteral("2.jpg"), QStringLiteral("10.png"), QStringLiteral("1.webp") }));

    sorted = entries;
    sortImageEntries(&sorted, { ImageSortKey::Type, SortDirection::Descending });
    QCOMPARE(names(sorted), QStringList({ QStringLiteral("1.webp"), QStringLiteral("10.png"), QStringLiteral("2.jpg") }));

    sorted = entries;
    sortImageEntries(&sorted, { ImageSortKey::Size, SortDirection::Ascending });
    QCOMPARE(names(sorted), QStringList({ QStringLiteral("10.png"), QStringLiteral("1.webp"), QStringLiteral("2.jpg") }));

    sorted = entries;
    sortImageEntries(&sorted, { ImageSortKey::Size, SortDirection::Descending });
    QCOMPARE(names(sorted), QStringList({ QStringLiteral("2.jpg"), QStringLiteral("1.webp"), QStringLiteral("10.png") }));

    sorted = entries;
    sortImageEntries(&sorted, { ImageSortKey::Date, SortDirection::Ascending });
    QCOMPARE(names(sorted), QStringList({ QStringLiteral("10.png"), QStringLiteral("2.jpg"), QStringLiteral("1.webp") }));

    sorted = entries;
    sortImageEntries(&sorted, { ImageSortKey::Date, SortDirection::Descending });
    QCOMPARE(names(sorted), QStringList({ QStringLiteral("2.jpg"), QStringLiteral("10.png"), QStringLiteral("1.webp") }));
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

void CoreTests::fileListSourceTreePreservesInputOrder()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir root(tempDir.path());

    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    const QString second = root.filePath(QStringLiteral("second.png"));
    const QString first = root.filePath(QStringLiteral("first.png"));
    QVERIFY(image.save(first));
    QVERIFY(image.save(second));

    const CollectionTreeResult tree = fileListSourceTree({ second, first });
    QVERIFY2(tree.ok(), qPrintable(tree.errorMessage));
    QVERIFY(tree.root != nullptr);
    QCOMPARE(tree.root->displayName, QStringLiteral("Selected files"));
    QVERIFY(tree.root->preserveImageOrder);
    QCOMPARE(tree.root->images.size(), 2);
    QCOMPARE(tree.root->images.at(0).displayName, QStringLiteral("second.png"));
    QCOMPARE(tree.root->images.at(1).displayName, QStringLiteral("first.png"));
    QCOMPARE(tree.selectedFolderPath, QString());
    QCOMPARE(tree.selectedImageIndex, 0);

    const CollectionTreeResult invalid = fileListSourceTree({ root.filePath(QStringLiteral("notes.txt")) });
    QVERIFY(!invalid.ok());
}

void CoreTests::folderListSourceTreePreservesTopLevelOrder()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir root(tempDir.path());
    QVERIFY(root.mkpath(QStringLiteral("one/nested")));
    QVERIFY(root.mkpath(QStringLiteral("two")));

    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(root.filePath(QStringLiteral("one/nested/child.png"))));
    QVERIFY(image.save(root.filePath(QStringLiteral("two/root.png"))));

    const QString one = root.filePath(QStringLiteral("one"));
    const QString two = root.filePath(QStringLiteral("two"));
    const CollectionTreeResult tree = folderListSourceTree({ two, one });
    QVERIFY2(tree.ok(), qPrintable(tree.errorMessage));
    QVERIFY(tree.root != nullptr);
    QCOMPARE(tree.root->displayName, QStringLiteral("Selected folders"));
    QCOMPARE(tree.root->children.size(), 2);
    QCOMPARE(tree.root->children.at(0)->displayName, QStringLiteral("two"));
    QCOMPARE(tree.root->children.at(0)->relativePath, QStringLiteral("folder-1"));
    QCOMPARE(tree.root->children.at(0)->loadRootPath, QFileInfo(two).absoluteFilePath());
    QCOMPARE(tree.root->children.at(0)->images.size(), 1);
    QCOMPARE(tree.root->children.at(0)->images.first().sourceFolderPath, QStringLiteral("folder-1"));
    QCOMPARE(tree.root->children.at(1)->displayName, QStringLiteral("one"));
    QCOMPARE(tree.root->children.at(1)->relativePath, QStringLiteral("folder-2"));
    QCOMPARE(tree.root->children.at(1)->children.first()->relativePath, QStringLiteral("folder-2/nested"));
    QCOMPARE(tree.root->children.at(1)->children.first()->loadFolderRelativePath, QStringLiteral("nested"));
    QCOMPARE(tree.selectedFolderPath, QStringLiteral("folder-1"));
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

void CoreTests::archiveListSourceTreePreservesArchiveRoots()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir root(tempDir.path());
    const QString first = root.filePath(QStringLiteral("first.zip"));
    const QString second = root.filePath(QStringLiteral("second.zip"));

    QVERIFY(writeZipArchive(first, {
        { QStringLiteral("root.png"), pngBytes() },
    }));
    QVERIFY(writeZipArchive(second, {
        { QStringLiteral("nested/2.png"), pngBytes() },
    }));

    const CollectionTreeResult tree = archiveListSourceTree({ second, first });
    QVERIFY2(tree.ok(), qPrintable(tree.errorMessage));
    QVERIFY(tree.root != nullptr);
    QCOMPARE(tree.root->displayName, QStringLiteral("Selected archives"));
    QCOMPARE(tree.root->children.size(), 2);
    QCOMPARE(tree.root->children.at(0)->displayName, QStringLiteral("second.zip"));
    QCOMPARE(tree.root->children.at(0)->relativePath, QStringLiteral("archive-1"));
    QCOMPARE(tree.root->children.at(0)->children.first()->relativePath, QStringLiteral("archive-1/nested"));
    QCOMPARE(tree.root->children.at(0)->children.first()->images.first().sourceFolderPath, QStringLiteral("archive-1/nested"));
    QCOMPARE(tree.root->children.at(0)->children.first()->images.first().archiveEntryPath, QStringLiteral("nested/2.png"));
    QCOMPARE(tree.root->children.at(0)->children.first()->images.first().sourceRelativePath, QStringLiteral("second.zip/nested/2.png"));
    QCOMPARE(tree.root->children.at(1)->displayName, QStringLiteral("first.zip"));
    QCOMPARE(tree.root->children.at(1)->relativePath, QStringLiteral("archive-2"));
}

void CoreTests::archiveListSourceTreeUsesSharedPassword()
{
    if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()) {
        QSKIP("zip command is not available");
    }

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QDir root(tempDir.path());
    const QString first = root.filePath(QStringLiteral("first-secret.zip"));
    const QString second = root.filePath(QStringLiteral("second-secret.zip"));
    QVERIFY(writeEncryptedZipArchive(first, QStringLiteral("one.png"), pngBytes(QSize(10, 5), Qt::blue), QStringLiteral("secret")));
    QVERIFY(writeEncryptedZipArchive(second, QStringLiteral("two.png"), pngBytes(QSize(10, 5), Qt::green), QStringLiteral("secret")));

    const CollectionTreeResult withoutPassword = archiveListSourceTree({ first, second });
    QVERIFY(!withoutPassword.ok());
    QCOMPARE(withoutPassword.errorReason, ScanErrorReason::PasswordRequired);

    const CollectionTreeResult wrongPassword = archiveListSourceTree({ first, second }, QStringLiteral("wrong"));
    QVERIFY(!wrongPassword.ok());
    QCOMPARE(wrongPassword.errorReason, ScanErrorReason::InvalidPassword);

    const CollectionTreeResult correctPassword = archiveListSourceTree({ first, second }, QStringLiteral("secret"));
    QVERIFY2(correctPassword.ok(), qPrintable(correctPassword.errorMessage));
    QVERIFY(correctPassword.root != nullptr);
    QCOMPARE(correctPassword.root->children.size(), 2);
    QCOMPARE(correctPassword.root->children.at(0)->images.size(), 1);
    QCOMPARE(correctPassword.root->children.at(1)->images.size(), 1);
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

void CoreTests::archiveErrorsKeepTheirReason_data()
{
    QTest::addColumn<QString>("message");
    QTest::addColumn<int>("reason");
    const auto add = [](const char *name, const char *message, ArchiveEntryReadErrorReason reason) {
        QTest::newRow(name) << QString::fromLatin1(message) << static_cast<int>(reason);
    };
    add("rar-headers", "Encryption is not supported", ArchiveEntryReadErrorReason::UnsupportedEncryption);
    add("rar-data", "Reading encrypted data is not currently supported", ArchiveEntryReadErrorReason::UnsupportedEncryption);
    add("rar4", "RAR encryption support unavailable", ArchiveEntryReadErrorReason::UnsupportedEncryption);
    add("7z-data", "The file content is encrypted, but currently not supported", ArchiveEntryReadErrorReason::UnsupportedEncryption);
    add("7z-headers", "The archive header is encrypted, but currently not supported", ArchiveEntryReadErrorReason::UnsupportedEncryption);
    add("missing-crypto", "Decryption is unsupported due to lack of crypto library", ArchiveEntryReadErrorReason::UnsupportedEncryption);
    add("7z-bcj2", "Unsupported form of encrypted BCJ2 streams", ArchiveEntryReadErrorReason::UnsupportedEncryption);
    add("required", "Passphrase required for this entry", ArchiveEntryReadErrorReason::InvalidPassword);
    add("7z-required", "Passphrase required for this 7-Zip encrypted archive", ArchiveEntryReadErrorReason::InvalidPassword);
    add("incorrect", "Incorrect passphrase", ArchiveEntryReadErrorReason::InvalidPassword);
    add("exhausted", "Too many incorrect passphrases", ArchiveEntryReadErrorReason::InvalidPassword);
    add("crc", "ZIP bad CRC", ArchiveEntryReadErrorReason::Generic);
    add("truncated", "Truncated 7-Zip encrypted stream", ArchiveEntryReadErrorReason::Generic);
    add("memory", "No memory for ZIP decryption", ArchiveEntryReadErrorReason::Generic);
    add("passphrase-processing", "Failed to process passphrase", ArchiveEntryReadErrorReason::Generic);
    add("crypto-failure", "7-Zip AES decryption failed", ArchiveEntryReadErrorReason::Generic);
    add("properties", "Malformed 7-Zip encryption properties", ArchiveEntryReadErrorReason::Generic);
    add("password-in-filename", "Could not open password.rar", ArchiveEntryReadErrorReason::Generic);
    add("fallback", "", ArchiveEntryReadErrorReason::Generic);
}

void CoreTests::archiveErrorsKeepTheirReason()
{
    QFETCH(QString, message);
    QFETCH(int, reason);
    const QString fallback = QStringLiteral("Could not read archive.");
    for (const QString &password : { QString(), QStringLiteral("secret") }) {
        archive *reader = archive_read_new();
        archive_set_error(reader, EIO, "%s", message.toUtf8().constData());
        const auto error = archiveReadError(reader, fallback, password);
        archive_read_free(reader);
        auto expected = static_cast<ArchiveEntryReadErrorReason>(reason);
        if (expected == ArchiveEntryReadErrorReason::InvalidPassword && password.isEmpty()) {
            expected = ArchiveEntryReadErrorReason::PasswordRequired;
        }
        QCOMPARE(error.reason, expected);
        if (expected == ArchiveEntryReadErrorReason::UnsupportedEncryption) {
            QVERIFY(error.message.contains(QStringLiteral("not supported")));
        } else {
            QCOMPARE(error.message, message.isEmpty() ? fallback : message);
        }
    }
}

void CoreTests::encryptedArchiveCapabilities_data()
{
    QTest::addColumn<QString>("fixture");
    QTest::addColumn<bool>("sevenZip");
    QTest::newRow("rar5-data") << QStringLiteral("fixtures/encrypted-data-rar5.rar") << false;
    QTest::newRow("rar5-headers") << QStringLiteral("fixtures/encrypted-headers-rar5.rar") << false;
    QTest::newRow("7z-data") << QStringLiteral("fixtures/encrypted-data.7z") << true;
    QTest::newRow("7z-headers") << QStringLiteral("fixtures/encrypted-headers.7z") << true;
}

void CoreTests::encryptedArchiveCapabilities()
{
    QFETCH(QString, fixture);
    QFETCH(bool, sevenZip);
    const QString path = QFINDTESTDATA(qPrintable(fixture));
    QVERIFY(!path.isEmpty());
    const ScanResult accepted = scanArchive(path, QStringLiteral("secret"));
    const bool decryptionSupported = sevenZip && accepted.ok();
    if (!decryptionSupported) {
        QCOMPARE(accepted.errorReason, ScanErrorReason::UnsupportedEncryption);
    }

    ImageEntry entry;
    entry.source = ImageEntrySource::Archive;
    entry.archivePath = path;
    entry.archiveEntryPath = QStringLiteral("sample.png");
    for (const QString &password : { QString(), QStringLiteral("wrong"), QStringLiteral("secret") }) {
        auto expectedScan = ScanErrorReason::UnsupportedEncryption;
        auto expectedRead = ArchiveEntryReadErrorReason::UnsupportedEncryption;
        if (decryptionSupported) {
            expectedScan = password.isEmpty() ? ScanErrorReason::PasswordRequired
                : password == QLatin1String("secret") ? ScanErrorReason::None : ScanErrorReason::InvalidPassword;
            expectedRead = password.isEmpty() ? ArchiveEntryReadErrorReason::PasswordRequired
                : password == QLatin1String("secret") ? ArchiveEntryReadErrorReason::None : ArchiveEntryReadErrorReason::InvalidPassword;
        }
        const auto scan = scanArchive(path, password);
        QCOMPARE(scan.errorReason, expectedScan);
        const auto tree = scanArchiveSourceTree(path, password);
        QCOMPARE(tree.errorReason, expectedScan);
        const auto multiple = archiveListSourceTree({ path }, password);
        QCOMPARE(multiple.errorReason, expectedScan);
        const auto bytes = readArchiveEntry(entry, password);
        QCOMPARE(bytes.errorReason, expectedRead);
        ArchiveEntryStreamReader stream(entry, password);
        QByteArray streamed;
        char buffer[17];
        qint64 count = -1;
        if (stream.open()) {
            while ((count = stream.read(buffer, sizeof(buffer))) > 0) {
                streamed.append(buffer, count);
            }
        }
        QCOMPARE(stream.errorReason(), expectedRead);
        DecodeRequest request;
        request.fitToWindow = false;
        const auto decoded = VipsImageDecoder().decode(entry, request, password);
        if (expectedRead == ArchiveEntryReadErrorReason::None) {
            QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
            QCOMPARE(decoded.image.size(), QSize(3, 2));
            QCOMPARE(decoded.image.pixelColor(1, 1), QColor(20, 120, 200));
            QCOMPARE(count, 0);
            QCOMPARE(streamed, bytes.data);
        } else {
            QVERIFY(!scan.ok());
            QVERIFY(!tree.ok());
            QVERIFY(!multiple.ok());
            QVERIFY(!bytes.ok());
            QVERIFY(bytes.data.isEmpty());
            QCOMPARE(count, -1);
            QCOMPARE(stream.errorMessage(), bytes.errorMessage);
            for (int retry = 0; retry < 2; ++retry) {
                QCOMPARE(stream.read(buffer, sizeof(buffer)), -1);
                QCOMPARE(stream.errorReason(), expectedRead);
                QCOMPARE(stream.errorMessage(), bytes.errorMessage);
            }
            QCOMPARE(decoded.errorMessage, bytes.errorMessage);
        }
    }
}

void CoreTests::encryptedArchiveCorruptionIsNotInvalidPassword()
{
    if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()) {
        QSKIP("zip command is not available");
    }
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("corrupt.zip"));
    const QByteArray contents = QByteArrayLiteral("This is not PNG data.");
    QVERIFY(writeEncryptedZipArchive(path, QStringLiteral("sample.png"), contents, QStringLiteral("secret")));
    const auto scan = scanArchive(path, QStringLiteral("secret"));
    QVERIFY2(scan.ok(), qPrintable(scan.errorMessage));
    QCOMPARE(readArchiveEntry(scan.entries.first(), QStringLiteral("secret")).data, contents);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QByteArray data = file.readAll();
    const qsizetype central = data.indexOf(QByteArrayLiteral("PK\x01\x02"));
    QVERIFY(central >= 0);
    const quint32 size = qFromLittleEndian<quint32>(data.constData() + central + 20);
    const qsizetype offset = 30 + qFromLittleEndian<quint16>(data.constData() + 26)
        + qFromLittleEndian<quint16>(data.constData() + 28);
    QVERIFY(size > 12 && offset + size <= data.size());
    data[offset + size - 1] = static_cast<char>(data.at(offset + size - 1) ^ 0x80);
    QVERIFY(file.seek(0));
    QCOMPARE(file.write(data), data.size());
    file.close();

    QCOMPARE(scanArchive(path, QStringLiteral("secret")).errorReason, ScanErrorReason::Generic);
    QCOMPARE(scanArchiveSourceTree(path, QStringLiteral("secret")).errorReason, ScanErrorReason::Generic);
    const auto bytes = readArchiveEntry(scan.entries.first(), QStringLiteral("secret"));
    QCOMPARE(bytes.errorReason, ArchiveEntryReadErrorReason::Generic);
    QVERIFY(!bytes.ok());
    QVERIFY(bytes.data.isEmpty());
    ArchiveEntryStreamReader stream(scan.entries.first(), QStringLiteral("secret"));
    QVERIFY(stream.open());
    char buffer[32];
    qint64 count;
    while ((count = stream.read(buffer, sizeof(buffer))) > 0) {
    }
    QCOMPARE(count, -1);
    QCOMPARE(stream.errorReason(), ArchiveEntryReadErrorReason::Generic);
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

void CoreTests::viewerRotationNormalizesAndResetsOnImageSwitch()
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
    controller.setPanOffset(QPointF(10.0, 20.0));

    controller.rotateRight();
    QCOMPARE(controller.rotationDegrees(), 90);
    QCOMPARE(controller.panOffset(), QPointF());

    controller.rotateRight();
    controller.rotateRight();
    controller.rotateRight();
    QCOMPARE(controller.rotationDegrees(), 0);

    controller.rotateLeft();
    QCOMPARE(controller.rotationDegrees(), 270);

    controller.setPanOffset(QPointF(3.0, 4.0));
    QVector<ImageEntry> reordered = entries;
    std::reverse(reordered.begin(), reordered.end());
    controller.setCollectionPreservingView(reordered, 0);
    QCOMPARE(controller.currentIndex(), 0);
    QCOMPARE(controller.rotationDegrees(), 270);
    QCOMPARE(controller.panOffset(), QPointF(3.0, 4.0));

    QVERIFY(controller.goNext());
    QCOMPARE(controller.currentIndex(), 1);
    QCOMPARE(controller.rotationDegrees(), 0);

    controller.rotateRight();
    QCOMPARE(controller.rotationDegrees(), 90);
    controller.setCollection(entries, 0);
    QCOMPARE(controller.rotationDegrees(), 0);
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
    QCOMPARE(MaximumZoomFactor, 256.0);
    controller.setCustomZoom(64.0);
    QCOMPARE(controller.zoomFactor(), 64.0);
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

void CoreTests::decoderColorManagementTagsSrgb()
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
    QCOMPARE(decoded.colorProfileState, ColorProfileState::SrgbAssumed);
    QCOMPARE(decoded.image.colorSpace(), QColorSpace(QColorSpace::SRgb));
}

void CoreTests::decoderColorManagementCanBeDisabled()
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
    request.colorManagementEnabled = false;

    VipsImageDecoder decoder;
    const DecodedImage decoded = decoder.decode(entry, request);
    QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
    QCOMPARE(decoded.colorProfileState, ColorProfileState::ColorManagementOff);
    QVERIFY(!decoded.image.colorSpace().isValid());
}

void CoreTests::decoderReportsEmbeddedIccConversion()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QDir root(tempDir.path());
    const QString sourcePath = root.filePath(QStringLiteral("source.png"));
    const QString taggedPath = root.filePath(QStringLiteral("tagged.png"));

    QImage source(16, 8, QImage::Format_RGB32);
    source.fill(Qt::blue);
    QVERIFY(source.save(sourcePath));
    QVERIFY(writeSrgbIccTaggedPng(sourcePath, taggedPath));

    QFile encoded(taggedPath);
    QVERIFY(encoded.open(QIODevice::ReadOnly));
    const QString zip = tempDir.filePath(QStringLiteral("profile.zip"));
    QVERIFY(writeZipArchive(zip, { { QFileInfo(taggedPath).fileName(), encoded.readAll() } }));
    const auto scan = scanArchive(zip);
    QVERIFY(scan.ok());
    QCOMPARE(scan.entries.size(), 1);
    for (const auto &entry : { imageEntryFromFileInfo(QFileInfo(taggedPath)), scan.entries.first() }) {
        DecodeRequest request;
        request.viewportSize = QSize(32, 20);
        request.rotationDegrees = 90;
        const auto decoded = VipsImageDecoder().decode(entry, request);
        QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
        QCOMPARE(decoded.image.colorSpace(), QColorSpace(QColorSpace::SRgb));
        QCOMPARE(decoded.colorProfileState, ColorProfileState::ConvertedToSrgb);
    }
}

void CoreTests::decoderKeepsInvalidIccImageViewable()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString taggedPath = QDir(tempDir.path()).filePath(QStringLiteral("invalid-profile.jpg"));
    QImage source(64, 40, QImage::Format_RGB32);
    source.fill(Qt::blue);
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(source.save(&buffer, "JPEG"));
    // Inject APP2 directly: an image writer may discard an invalid profile.
    QByteArray profile("ICC_PROFILE\0\1\1", 14);
    profile += QByteArray(128, '\0');
    const int segmentLength = profile.size() + 2;
    QByteArray segment = QByteArray::fromHex("ffe2");
    segment.append(static_cast<char>(segmentLength >> 8));
    segment.append(static_cast<char>(segmentLength & 0xff));
    segment += profile;
    jpeg.insert(2, segment);
    QFile file(taggedPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(jpeg), jpeg.size());
    file.close();
    QFile encoded(taggedPath);
    QVERIFY(encoded.open(QIODevice::ReadOnly));
    const QString zip = tempDir.filePath(QStringLiteral("profile.zip"));
    QVERIFY(writeZipArchive(zip, { { QFileInfo(taggedPath).fileName(), encoded.readAll() } }));
    const auto scan = scanArchive(zip);
    QVERIFY(scan.ok());
    QCOMPARE(scan.entries.size(), 1);
    for (const auto &entry : { imageEntryFromFileInfo(QFileInfo(taggedPath)), scan.entries.first() }) {
        DecodeRequest request;
        request.viewportSize = QSize(32, 20);
        request.rotationDegrees = 90;
        const auto decoded = VipsImageDecoder().decode(entry, request);
        QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
        QCOMPARE(decoded.image.colorSpace(), QColorSpace(QColorSpace::SRgb));
        const QColor pixel = decoded.image.pixelColor(decoded.image.width() / 2, decoded.image.height() / 2);
        QVERIFY(pixel.blue() > 245);
        QVERIFY(pixel.red() < 10);
    }
}

void CoreTests::rarImageRoundTrip_data()
{
    QTest::addColumn<QString>("fixture");
    QTest::newRow("rar4-stored") << QStringLiteral("fixtures/images-rar4.rar");
    QTest::newRow("rar5-stored") << QStringLiteral("fixtures/images-rar5.rar");
}

void CoreTests::rarImageRoundTrip()
{
    QFETCH(QString, fixture);
    const QString path = QFINDTESTDATA(qPrintable(fixture));
    QVERIFY(!path.isEmpty());
    const ScanResult scan = scanArchive(path);
    QVERIFY2(scan.ok(), qPrintable(scan.errorMessage));
    QCOMPARE(scan.entries.size(), 2);
    const CollectionTreeResult tree = scanArchiveSourceTree(path);
    QVERIFY2(tree.ok(), qPrintable(tree.errorMessage));
    QCOMPARE(tree.root->children.size(), 1);
    QCOMPARE(tree.root->children.first()->images.size(), 2);
    for (const ImageEntry &entry : scan.entries) {
        const ArchiveEntryReadResult bytes = readArchiveEntry(entry);
        QVERIFY2(bytes.ok(), qPrintable(bytes.errorMessage));
        QVERIFY(!readArchiveEntry(entry, {}, 1).ok());
        ArchiveEntryStreamReader stream(entry);
        QVERIFY(stream.open());
        QByteArray streamed;
        char buffer[7];
        qint64 count;
        while ((count = stream.read(buffer, sizeof(buffer))) > 0) {
            streamed.append(buffer, count);
        }
        QCOMPARE(count, 0);
        QCOMPARE(streamed, bytes.data);
        DecodeRequest request;
        request.fitToWindow = false;
        request.regionRendering = false;
        const DecodedImage decoded = VipsImageDecoder().decode(entry, request);
        QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
        QCOMPARE(decoded.image.size(), QSize(3, 2));
        const QColor expected = entry.archiveEntryPath.contains(QStringLiteral("green")) ? QColor(Qt::green) : QColor(Qt::red);
        QCOMPARE(decoded.image.pixelColor(1, 1), expected);
        QCOMPARE(QImage::fromData(bytes.data).pixelColor(1, 1), expected);
    }
}

void CoreTests::dibVariants_data()
{
    QTest::addColumn<bool>("headerless");
    QTest::addColumn<bool>("topDown");
    QTest::addColumn<bool>("archived");
    for (bool headerless : { false, true }) {
        for (bool topDown : { false, true }) {
            for (bool archived : { false, true }) {
                const QByteArray name = QByteArray::number(headerless) + '-' + QByteArray::number(topDown) + '-' + QByteArray::number(archived);
                QTest::newRow(name.constData()) << headerless << topDown << archived;
            }
        }
    }
}

void CoreTests::dibVariants()
{
    QFETCH(bool, headerless);
    QFETCH(bool, topDown);
    QFETCH(bool, archived);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QByteArray bitmap;
    QDataStream out(&bitmap, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    if (!headerless) {
        out.writeRawData("BM", 2);
        out << quint32(150) << quint32(0) << quint32(54);
    }
    out << quint32(40) << qint32(3) << qint32(topDown ? -8 : 8)
        << quint16(1) << quint16(24) << quint32(0) << quint32(96)
        << qint32(0) << qint32(0) << quint32(0) << quint32(0);
    const QByteArray redRow = QByteArray::fromHex("0000ff0000ff0000ff000000");
    const QByteArray blueRow = QByteArray::fromHex("ff0000ff0000ff0000000000");
    const QByteArray rows = topDown ? redRow.repeated(4) + blueRow.repeated(4) : blueRow.repeated(4) + redRow.repeated(4);
    out.writeRawData(rows.constData(), rows.size());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("sample.dib"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bitmap), bitmap.size());
    file.close();
    ImageEntry entry = imageEntryFromFileInfo(QFileInfo(path));
    if (archived) {
        const QString archivePath = QDir(dir.path()).filePath(QStringLiteral("sample.zip"));
        QVERIFY(writeZipArchive(archivePath, { { QStringLiteral("sample.dib"), bitmap } }));
        const ScanResult scan = scanArchive(archivePath);
        QVERIFY(scan.ok());
        QCOMPARE(scan.entries.size(), 1);
        entry = scan.entries.first();
    }
    DecodeRequest request;
    request.fitToWindow = false;
    request.regionRendering = false;
    const DecodedImage decoded = VipsImageDecoder().decode(entry, request);
    if (headerless && archived) {
        // The sequential source has no filename hint for a headerless DIB.
        QVERIFY(!decoded.ok());
        QVERIFY(!decoded.errorMessage.isEmpty());
    } else {
        QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
        QCOMPARE(decoded.image.size(), QSize(3, 8));
        QCOMPARE(decoded.image.pixelColor(1, 0), QColor(Qt::red));
        QCOMPARE(decoded.image.pixelColor(1, 7), QColor(Qt::blue));
    }
}

void CoreTests::decoderSupportedFormats_data()
{
    QTest::addColumn<QString>("extension");
    QTest::addColumn<int>("archiveType");
    for (const QString &extension : { QStringLiteral("jpg"), QStringLiteral("png"), QStringLiteral("webp"),
             QStringLiteral("bmp"), QStringLiteral("dib"), QStringLiteral("avif"), QStringLiteral("jxl"), QStringLiteral("jp2"),
             QStringLiteral("j2k"), QStringLiteral("heic"), QStringLiteral("heif"), QStringLiteral("gif"), QStringLiteral("tif"), QStringLiteral("tiff") }) {
        for (int archiveType = 0; archiveType < 3; ++archiveType) {
            const QByteArray name = extension.toLatin1() + '-' + QByteArray::number(archiveType);
            QTest::newRow(name.constData()) << extension << archiveType;
        }
    }
}

void CoreTests::decoderSupportedFormats()
{
    QFETCH(QString, extension);
    QFETCH(int, archiveType);
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QDir root(tempDir.path());
    const QString sourcePath = root.filePath(QStringLiteral("source.png"));
    const QString imagePath = root.filePath(QStringLiteral("sample.") + extension);
    QImage source(64, 40, QImage::Format_RGB32);
    source.fill(QColor(40, 120, 200));
    QVERIFY(source.save(sourcePath));
    if (extension == QStringLiteral("bmp") || extension == QStringLiteral("dib")) {
        // DIB extension coverage uses a BMP container, not a headerless DIB fixture.
        QVERIFY(source.save(imagePath, "BMP"));
    } else {
        try {
            vips::VImage::new_from_file(sourcePath.toUtf8().constData()).write_to_file(imagePath.toUtf8().constData());
        } catch (const vips::VError &error) {
            QFAIL(error.what());
        }
    }
    if (extension == QStringLiteral("j2k")) {
        // libvips saves a JP2 container even with .j2k; extract its codestream.
        QFile file(imagePath);
        QVERIFY(file.open(QIODevice::ReadWrite));
        const QByteArray container = file.readAll();
        const qsizetype marker = container.indexOf("jp2c");
        QVERIFY(marker >= 4 && marker + 4 <= container.size());
        const quint32 size = qFromBigEndian<quint32>(container.constData() + marker - 4);
        QVERIFY(size >= 8 && marker - 4 + size <= container.size());
        const QByteArray raw = container.mid(marker + 4, size - 8);
        QVERIFY(raw.startsWith(QByteArray::fromHex("ff4fff51")));
        const QString systemMime = QMimeDatabase().mimeTypeForData(raw).name();
        QCOMPARE(imageMimeTypeFromPath(imagePath), systemMime);
        QFile desktop(QFINDTESTDATA("../resources/io.github._0xf836.Insidious.desktop"));
        QVERIFY(desktop.open(QIODevice::ReadOnly));
        QVERIFY(desktop.readAll().contains(systemMime.toUtf8() + ';'));
        QVERIFY(file.resize(0));
        QVERIFY(file.seek(0));
        QCOMPARE(file.write(raw), raw.size());
    }
    ImageEntry entry = imageEntryFromFileInfo(QFileInfo(imagePath));
    if (archiveType != 0) {
        QFile file(imagePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString archivePath = root.filePath(archiveType == 1 ? QStringLiteral("sample.zip") : QStringLiteral("sample.7z"));
        QVERIFY(writeZipArchive(archivePath, { { QFileInfo(imagePath).fileName(), file.readAll() } }, archiveType == 2));
        const ScanResult scan = scanArchive(archivePath);
        QVERIFY2(scan.ok(), qPrintable(scan.errorMessage));
        QCOMPARE(scan.entries.size(), 1);
        entry = scan.entries.first();
    }
    DecodeRequest request;
    request.viewportSize = QSize(32, 20);
    const DecodedImage decoded = VipsImageDecoder().decode(entry, request);
    QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
    QCOMPARE(decoded.sourceSize, source.size());
    QCOMPARE(decoded.image.size(), QSize(32, 20));
    const QColor pixel = decoded.image.pixelColor(16, 10);
    QVERIFY(std::abs(pixel.red() - 40) <= 5);
    QVERIFY(std::abs(pixel.green() - 120) <= 5);
    QVERIFY(std::abs(pixel.blue() - 200) <= 5);
}

void CoreTests::decoderTiffLayouts_data()
{
    QTest::addColumn<int>("archiveType");
    QTest::addColumn<bool>("tiled");
    QTest::addColumn<bool>("bigTiff");
    QTest::addColumn<int>("compression");
    for (int archiveType = 0; archiveType < 3; ++archiveType) {
        for (bool tiled : { false, true }) {
            for (bool bigTiff : { false, true }) {
                for (int compression : { VIPS_FOREIGN_TIFF_COMPRESSION_NONE, VIPS_FOREIGN_TIFF_COMPRESSION_LZW,
                         VIPS_FOREIGN_TIFF_COMPRESSION_DEFLATE }) {
                    const QByteArray name = QByteArray::number(archiveType) + '-' + QByteArray::number(tiled)
                        + '-' + QByteArray::number(bigTiff) + '-' + QByteArray::number(compression);
                    QTest::newRow(name.constData()) << archiveType << tiled << bigTiff << compression;
                }
            }
        }
    }
}

void CoreTests::decoderTiffLayouts()
{
    QFETCH(int, archiveType);
    QFETCH(bool, tiled);
    QFETCH(bool, bigTiff);
    QFETCH(int, compression);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("pages.tiff"));
    try {
        auto first = vips::VImage::black(80, 60, vips::VImage::option()->set("bands", 3))
                         .new_from_image({ 40, 120, 200 })
                         .copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB));
        auto pages = first.join(first.new_from_image({ 180, 60, 30 }), VIPS_DIRECTION_VERTICAL);
        pages.set(VIPS_META_PAGE_HEIGHT, 60);
        pages.set(VIPS_META_ORIENTATION, 6);
        pages.write_to_file(path.toUtf8().constData(), vips::VImage::option()->set("tile", tiled)
            ->set("tile_width", 32)->set("tile_height", 32)->set("bigtiff", bigTiff)
            ->set("compression", compression));
        const auto all = vips::VImage::new_from_file(path.toUtf8().constData(), vips::VImage::option()->set("n", -1));
        QCOMPARE(all.height(), 120);
        QCOMPARE(all.get_int(VIPS_META_N_PAGES), 2);
    } catch (const vips::VError &error) {
        QFAIL(error.what());
    }
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    if (bigTiff) {
        QVERIFY(bytes.startsWith(QByteArray::fromHex("49492b00")) || bytes.startsWith(QByteArray::fromHex("4d4d002b")));
    } else {
        QCOMPARE(QMimeDatabase().mimeTypeForData(bytes).name(), QStringLiteral("image/tiff"));
    }
    ImageEntry entry = imageEntryFromFileInfo(QFileInfo(path));
    if (archiveType != 0) {
        const QString archivePath = temp.filePath(archiveType == 1 ? QStringLiteral("pages.zip") : QStringLiteral("pages.7z"));
        QVERIFY(writeZipArchive(archivePath, { { QStringLiteral("pages.tiff"), bytes } }, archiveType == 2));
        const auto scan = scanArchive(archivePath);
        QVERIFY2(scan.ok(), qPrintable(scan.errorMessage));
        QCOMPARE(scan.entries.size(), 1);
        entry = scan.entries.first();
    }
    for (bool autorotate : { false, true }) {
        DecodeRequest request;
        request.exifAutorotate = autorotate;
        request.viewportSize = autorotate ? QSize(30, 40) : QSize(40, 30);
        const auto decoded = VipsImageDecoder().decode(entry, request);
        QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
        QCOMPARE(decoded.image.size(), request.viewportSize);
        QCOMPARE(decoded.image.pixelColor(10, 10), QColor(40, 120, 200));
    }
}

void CoreTests::decoderGifFirstFrame_data()
{
    QTest::addColumn<int>("archiveType");
    QTest::addColumn<bool>("animated");
    QTest::addColumn<bool>("transparent");
    for (int archiveType = 0; archiveType < 3; ++archiveType) {
        for (bool animated : { false, true }) {
            for (bool transparent : { false, true }) {
                const QByteArray name = QByteArray::number(archiveType) + '-'
                    + QByteArray::number(animated) + '-' + QByteArray::number(transparent);
                QTest::newRow(name.constData()) << archiveType << animated << transparent;
            }
        }
    }
}

void CoreTests::decoderGifFirstFrame()
{
    QFETCH(int, archiveType);
    QFETCH(bool, animated);
    QFETCH(bool, transparent);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString pngPath = temp.filePath(QStringLiteral("source.png"));
    const QString gifPath = temp.filePath(QStringLiteral("sample.gif"));
    QImage expected(64, 40, QImage::Format_RGBA8888);
    expected.fill(Qt::red);
    if (transparent) {
        for (int y = 0; y < expected.height(); ++y) {
            for (int x = 0; x < expected.width() / 2; ++x) {
                expected.setPixelColor(x, y, Qt::transparent);
            }
        }
    }
    QVERIFY(expected.save(pngPath));
    try {
        auto frames = vips::VImage::new_from_file(pngPath.toUtf8().constData());
        if (animated) {
            frames = frames.join(frames.new_from_image({ 0, 0, 255, 255 }), VIPS_DIRECTION_VERTICAL);
            frames.set(VIPS_META_PAGE_HEIGHT, 40);
            frames.set("delay", std::vector<int> { 80, 120 });
            frames.set("loop", 0);
        }
        frames.write_to_file(gifPath.toUtf8().constData(), vips::VImage::option()->set("dither", 0.0));
        const auto all = vips::VImage::new_from_file(gifPath.toUtf8().constData(), vips::VImage::option()->set("n", -1));
        QCOMPARE(all.height(), animated ? 80 : 40);
    } catch (const vips::VError &error) {
        QFAIL(error.what());
    }
    ImageEntry entry = imageEntryFromFileInfo(QFileInfo(gifPath));
    if (archiveType != 0) {
        QFile file(gifPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString archivePath = temp.filePath(archiveType == 1 ? QStringLiteral("images.zip") : QStringLiteral("images.7z"));
        QVERIFY(writeZipArchive(archivePath, { { QStringLiteral("sample.gif"), file.readAll() } }, archiveType == 2));
        const auto scan = scanArchive(archivePath);
        QVERIFY2(scan.ok(), qPrintable(scan.errorMessage));
        QCOMPARE(scan.entries.size(), 1);
        entry = scan.entries.first();
    }
    for (int rotation : { 0, 90 }) {
        DecodeRequest request;
        request.viewportSize = rotation == 0 ? QSize(32, 20) : QSize(20, 32);
        request.rotationDegrees = rotation;
        const auto decoded = VipsImageDecoder().decode(entry, request);
        QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
        QCOMPARE(decoded.sourceSize, QSize(64, 40));
        QCOMPARE(decoded.image.size(), request.viewportSize);
        const QImage reference = expected.transformed(QTransform().rotate(rotation))
                                     .scaled(request.viewportSize);
        for (int y : { decoded.image.height() / 4, decoded.image.height() * 3 / 4 }) {
            for (int x : { decoded.image.width() / 4, decoded.image.width() * 3 / 4 }) {
                const QColor actual = decoded.image.pixelColor(x, y);
                const QColor wanted = reference.pixelColor(x, y);
                QCOMPARE(actual.alpha(), wanted.alpha());
                if (wanted.alpha() != 0) {
                    QCOMPARE(actual, wanted);
                }
            }
        }
    }
}

void CoreTests::decoderJxlRsErrors()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("sample.JXL"));
    try {
        auto image = vips::VImage::black(32, 24, vips::VImage::option()->set("bands", 3))
            .new_from_image({40, 120, 200}).copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB));
        image.jxlsave(path.toUtf8().constData(), vips::VImage::option()->set("lossless", TRUE)->set("effort", 1));
    } catch (const vips::VError &error) {
        QFAIL(error.what());
    }
    const auto entry = imageEntryFromFileInfo(QFileInfo(path));
    DecodeRequest request;
    request.viewportSize = QSize(16, 12);
    request.maxDecodedBytes = 1;
    const auto tooLarge = VipsImageDecoder().decode(entry, request);
    QVERIFY(!tooLarge.ok());
    QCOMPARE(tooLarge.errorReason, DecodeErrorReason::RenderTooLarge);

    request.maxDecodedBytes = 1024 * 1024;
    const auto valid = VipsImageDecoder().decode(entry, request);
    QVERIFY2(valid.ok(), qPrintable(valid.errorMessage));
    QVERIFY(!valid.animated);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    const QByteArray original = file.readAll();
    QVERIFY(file.resize(original.size() / 2));
    QVERIFY(file.flush());
    const auto truncated = VipsImageDecoder().decode(entry, request);
    QVERIFY(!truncated.ok());
    QCOMPARE(truncated.errorReason, DecodeErrorReason::Other);
    QVERIFY(truncated.errorMessage.startsWith(QStringLiteral("jxl-rs:")));

    QVERIFY(file.resize(0));
    file.close();
    // A PNG with a JXL suffix would open through libvips content detection: no silent fallback.
    QImage png(8, 8, QImage::Format_RGB32);
    png.fill(Qt::red);
    QVERIFY(png.save(path, "PNG"));
    const auto wrongFormat = VipsImageDecoder().decode(entry, request);
    QVERIFY(!wrongFormat.ok());
    QVERIFY(wrongFormat.errorMessage.startsWith(QStringLiteral("jxl-rs:")));
}

void CoreTests::decoderJxlAnimationLimitKeepsPoster()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    std::unique_ptr<JxlEncoder, decltype(&JxlEncoderDestroy)> encoder(JxlEncoderCreate(nullptr), JxlEncoderDestroy);
    QVERIFY(encoder);
    JxlBasicInfo info;
    JxlEncoderInitBasicInfo(&info);
    info.xsize = 1; info.ysize = 1; info.bits_per_sample = 8;
    info.uses_original_profile = JXL_TRUE;
    info.have_animation = JXL_TRUE;
    info.animation.tps_numerator = 1000;
    info.animation.tps_denominator = 1;
    QCOMPARE(JxlEncoderSetBasicInfo(encoder.get(), &info), JXL_ENC_SUCCESS);
    JxlColorEncoding color;
    JxlColorEncodingSetToSRGB(&color, JXL_FALSE);
    QCOMPARE(JxlEncoderSetColorEncoding(encoder.get(), &color), JXL_ENC_SUCCESS);
    auto *settings = JxlEncoderFrameSettingsCreate(encoder.get(), nullptr);
    QCOMPARE(JxlEncoderSetFrameLossless(settings, JXL_TRUE), JXL_ENC_SUCCESS);
    QCOMPARE(JxlEncoderFrameSettingsSetOption(settings, JXL_ENC_FRAME_SETTING_EFFORT, 1), JXL_ENC_SUCCESS);
    JxlFrameHeader header;
    JxlEncoderInitFrameHeader(&header);
    header.duration = 100;
    QCOMPARE(JxlEncoderSetFrameHeader(settings, &header), JXL_ENC_SUCCESS);
    const JxlPixelFormat format {3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    const uint8_t pixel[] = {240, 20, 10};
    for (int i = 0; i < 65537; ++i) {
        QCOMPARE(JxlEncoderAddImageFrame(settings, &format, pixel, sizeof(pixel)), JXL_ENC_SUCCESS);
    }
    JxlEncoderCloseInput(encoder.get());
    QByteArray encoded;
    JxlEncoderStatus status;
    do {
        const auto offset = encoded.size();
        encoded.resize(offset + 65536);
        auto *next = reinterpret_cast<uint8_t *>(encoded.data() + offset);
        size_t available = 65536;
        status = JxlEncoderProcessOutput(encoder.get(), &next, &available);
        QVERIFY(status == JXL_ENC_SUCCESS || status == JXL_ENC_NEED_MORE_OUTPUT);
        encoded.resize(encoded.size() - available);
    } while (status == JXL_ENC_NEED_MORE_OUTPUT);
    const auto path = temp.filePath(QStringLiteral("too-many-frames.jxl"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(encoded), encoded.size());
    file.close();
    ImageEntry entry;
    entry.absolutePath = path;
    DecodeRequest request;
    request.viewportSize = QSize(1, 1);
    const auto poster = VipsImageDecoder().decode(entry, request);
    QVERIFY2(poster.ok(), qPrintable(poster.errorMessage));
    QVERIFY(poster.animated);
    QCOMPARE(poster.image.pixelColor(0, 0), QColor(240, 20, 10));
    AnimationDecodeSession session;
    QVERIFY(!session.open(entry));
    QVERIFY(session.errorMessage().contains(QStringLiteral("too many frames")));
}

void CoreTests::decoderJxlStreamingAndMetadata()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("metadata.jxl"));
    vips::VImage::black(32, 24, vips::VImage::option()->set("bands", 3))
        .copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB))
        .jxlsave(path.toUtf8().constData(), vips::VImage::option()->set("lossless", true));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QByteArray encoded = file.readAll();
    auto box = [](const char *type, const QByteArray &payload) {
        QByteArray result(8, '\0');
        qToBigEndian<quint32>(payload.size() + 8, result.data());
        std::memcpy(result.data() + 4, type, 4);
        return result + payload;
    };
    if (encoded.startsWith(QByteArray::fromHex("ff0a"))) {
        encoded = QByteArray::fromHex("0000000c4a584c200d0a870a") + box("jxlc", encoded);
    }
    QByteArray exif = QByteArray::fromHex("0000000049492a0008000000010010010200080000001a00000000000000");
    exif.append("RustCam\0", 8);
    QByteArray withoutExif = encoded.left(12);
    for (qsizetype pos = 12; pos < encoded.size();) {
        const quint32 size = qFromBigEndian<quint32>(encoded.constData() + pos);
        QVERIFY(size >= 8 && pos + size <= encoded.size());
        const auto type = encoded.mid(pos + 4, 4);
        if (type != "Exif" && !(type == "brob" && encoded.mid(pos + 8, 4) == "Exif")) {
            withoutExif += encoded.mid(pos, size);
        }
        pos += size;
    }
    encoded = withoutExif + box("Exif", exif);
    QVERIFY(file.resize(0));
    QVERIFY(file.seek(0));
    QCOMPARE(file.write(encoded), encoded.size());
    QVERIFY(file.flush());
    DecodeRequest request;
    request.viewportSize = QSize(16, 12);
    auto entry = imageEntryFromFileInfo(QFileInfo(path));
    auto decoded = VipsImageDecoder().decode(entry, request);
    QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
    QVERIFY(std::any_of(decoded.metadataFields.begin(), decoded.metadataFields.end(), [](const auto &field) {
        return field.name == QStringLiteral("Camera") && field.value == QStringLiteral("RustCam");
    }));
    // A legal large trailing box must be streamed, rather than subject to the old source-buffer limit.
    QByteArray freeHeader(8, '\0');
    constexpr quint32 payload = 256U * 1024 * 1024 + 16;
    qToBigEndian<quint32>(payload + 8, freeHeader.data());
    std::memcpy(freeHeader.data() + 4, "free", 4);
    QCOMPARE(file.write(freeHeader), freeHeader.size());
    QVERIFY(file.resize(encoded.size() + 8 + payload));
    QVERIFY(file.flush());
    entry = imageEntryFromFileInfo(QFileInfo(path));
    decoded = VipsImageDecoder().decode(entry, request);
    QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
    QCOMPARE(decoded.image.size(), QSize(16, 12));

    JxlRasterDecoder limited;
    QVERIFY(limited.open(entry, {}));
    vips::VImage raster;
    double delay;
    bool more;
    QVERIFY(!limited.readFrame(&raster, &delay, &more, 1));
    QCOMPARE(limited.errorReason(), DecodeErrorReason::RenderTooLarge);
}

void CoreTests::decoderJxlThumbnailBudgetAndConcurrentSources()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString large = temp.filePath(QStringLiteral("large.jxl"));
    vips::VImage::black(3072, 3072, vips::VImage::option()->set("bands", 3))
        .new_from_image({40, 120, 200}).copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB))
        .jxlsave(large.toUtf8().constData(), vips::VImage::option()->set("lossless", true)->set("effort", 1));
    DecodeRequest thumbnail;
    thumbnail.viewportSize = QSize(128, 128);
    thumbnail.maxDecodedBytes = 32LL * 1024 * 1024;
    const auto decoded = VipsImageDecoder().decode(imageEntryFromFileInfo(QFileInfo(large)), thumbnail);
    QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
    QCOMPARE(decoded.image.size(), QSize(128, 128));
    QList<QFuture<DecodedImage>> jobs;
    for (int i = 0; i < 6; ++i) {
        const QString path = temp.filePath(QStringLiteral("source-%1.jxl").arg(i));
        vips::VImage::black(64, 48, vips::VImage::option()->set("bands", 3))
            .new_from_image({double(i * 30), 120, 200}).copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB))
            .jxlsave(path.toUtf8().constData(), vips::VImage::option()->set("lossless", true));
        const auto entry = imageEntryFromFileInfo(QFileInfo(path));
        jobs.append(QtConcurrent::run([entry, thumbnail]() { return VipsImageDecoder().decode(entry, thumbnail); }));
    }
    for (int i = 0; i < jobs.size(); ++i) {
        const auto result = jobs[i].result();
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QVERIFY(std::abs(result.image.pixelColor(10, 10).red() - i * 30) <= 1);
    }
}

void CoreTests::decoderJxlOrientationAndRegion()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QImage pixels(64, 48, QImage::Format_RGB888);
    for (int y = 0; y < pixels.height(); ++y) {
        for (int x = 0; x < pixels.width(); ++x) { pixels.setPixelColor(x, y, QColor(20 + 3*x, 30 + 4*y, 100)); }
    }
    for (int orientation = 1; orientation <= 8; ++orientation) {
        auto source = vips::VImage::new_from_memory(pixels.bits(), pixels.sizeInBytes(), 64, 48, 3, VIPS_FORMAT_UCHAR)
            .copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB));
        source.set(VIPS_META_ORIENTATION, orientation);
        const auto path = temp.filePath(QStringLiteral("orientation-%1.jxl").arg(orientation));
        std::unique_ptr<JxlEncoder, decltype(&JxlEncoderDestroy)> encoder(JxlEncoderCreate(nullptr), JxlEncoderDestroy);
        QVERIFY(encoder);
        JxlBasicInfo info;
        JxlEncoderInitBasicInfo(&info);
        info.xsize = 64; info.ysize = 48; info.bits_per_sample = 8;
        info.uses_original_profile = JXL_TRUE;
        info.orientation = static_cast<JxlOrientation>(orientation);
        QCOMPARE(JxlEncoderSetBasicInfo(encoder.get(), &info), JXL_ENC_SUCCESS);
        JxlColorEncoding color;
        JxlColorEncodingSetToSRGB(&color, JXL_FALSE);
        QCOMPARE(JxlEncoderSetColorEncoding(encoder.get(), &color), JXL_ENC_SUCCESS);
        auto *settings = JxlEncoderFrameSettingsCreate(encoder.get(), nullptr);
        QCOMPARE(JxlEncoderSetFrameLossless(settings, JXL_TRUE), JXL_ENC_SUCCESS);
        const JxlPixelFormat format {3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
        QCOMPARE(JxlEncoderAddImageFrame(settings, &format, pixels.constBits(), pixels.sizeInBytes()), JXL_ENC_SUCCESS);
        JxlEncoderCloseInput(encoder.get());
        QByteArray encoded(65536, '\0');
        auto *next = reinterpret_cast<uint8_t *>(encoded.data());
        size_t available = encoded.size();
        QCOMPARE(JxlEncoderProcessOutput(encoder.get(), &next, &available), JXL_ENC_SUCCESS);
        encoded.resize(encoded.size() - available);
        QFile output(path);
        QVERIFY(output.open(QIODevice::WriteOnly));
        QCOMPARE(output.write(encoded), encoded.size());
        output.close();
        for (bool autorotate : {false, true}) {
            for (int rotation : {0, 90, 180, 270}) {
                DecodeRequest request;
                request.fitToWindow = false;
                request.regionRendering = true;
                request.exifAutorotate = autorotate;
                request.rotationDegrees = rotation;
                request.devicePixelRatio = 2;
                request.scaleFactor = 15.75;
                request.viewportSize = QSize(32, 24);
                request.panOffset = QPointF(9, -5);
                const auto expected = renderVipsImage(source, request);
                const auto actual = VipsImageDecoder().decode(imageEntryFromFileInfo(QFileInfo(path)), request);
                QVERIFY2(actual.ok(), qPrintable(actual.errorMessage));
                QVERIFY(expected.ok());
                QCOMPARE(actual.sourceSize, expected.sourceSize);
                QCOMPARE(actual.sourceRegion, expected.sourceRegion);
                QCOMPARE(actual.image.size(), expected.image.size());
                const auto a = actual.image.pixelColor(actual.image.width()/2, actual.image.height()/2);
                const auto b = expected.image.pixelColor(expected.image.width()/2, expected.image.height()/2);
                QVERIFY2(std::abs(a.red() - b.red()) <= 2, qPrintable(QStringLiteral("orientation=%1 autorotate=%2 rotation=%3 actual=%4 expected=%5").arg(orientation).arg(autorotate).arg(rotation).arg(a.red()).arg(b.red())));
                QVERIFY(std::abs(a.green() - b.green()) <= 2);
            }
        }
    }
}

void CoreTests::decoderModernFormats_data()
{
    QTest::addColumn<QString>("extension");
    QTest::addColumn<int>("archiveType");
    QTest::addColumn<int>("bands");
    QTest::addColumn<bool>("highBitDepth");
    QTest::addColumn<bool>("colorManagement");
    for (const QString &extension : { QStringLiteral("avif"), QStringLiteral("jxl"), QStringLiteral("jp2"), QStringLiteral("heic"), QStringLiteral("tiff") }) {
        for (int archiveType = 0; archiveType < 3; ++archiveType) {
            for (int bands : { 1, 2, 3, 4 }) {
                for (bool highBitDepth : { false, true }) {
                    for (bool colorManagement : { false, true }) {
                        const QByteArray name = extension.toLatin1() + '-' + QByteArray::number(archiveType)
                            + '-' + QByteArray::number(bands) + '-' + QByteArray::number(highBitDepth)
                            + '-' + QByteArray::number(colorManagement);
                        QTest::newRow(name.constData()) << extension << archiveType << bands << highBitDepth << colorManagement;
                    }
                }
            }
        }
    }
}

void CoreTests::decoderModernFormats()
{
    QFETCH(QString, extension);
    QFETCH(int, archiveType);
    QFETCH(int, bands);
    QFETCH(bool, highBitDepth);
    QFETCH(bool, colorManagement);
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QDir root(tempDir.path());
    const QString path = root.filePath(QStringLiteral("sample.") + extension);
    const bool gray = bands <= 2;
    const bool alpha = bands == 2 || bands == 4;
    QImage expected(80, 60, QImage::Format_RGBA8888);
    std::vector<quint16> samples(80 * 60 * bands);
    for (int y = 0; y < 60; ++y) {
        for (int x = 0; x < 80; ++x) {
            const int red = x < 40 ? 40 : 180;
            const int green = gray ? red : (y < 30 ? 60 : 160);
            const int blue = gray ? red : 100;
            const int opacity = alpha ? (x < 40 ? 96 : 192) : 255;
            expected.setPixelColor(x, y, QColor(red, green, blue, opacity));
            const int offset = (y * 80 + x) * bands;
            samples[offset] = red * 257;
            if (!gray) {
                samples[offset + 1] = green * 257;
                samples[offset + 2] = blue * 257;
            }
            if (alpha) {
                samples[offset + bands - 1] = opacity * 257;
            }
        }
    }
    try {
        auto image = vips::VImage::new_from_memory(samples.data(), samples.size() * sizeof(quint16),
            80, 60, bands, VIPS_FORMAT_USHORT).copy(vips::VImage::option()->set("interpretation",
                gray ? VIPS_INTERPRETATION_GREY16 : VIPS_INTERPRETATION_RGB16));
        if (!highBitDepth) {
            image = image.colourspace(gray ? VIPS_INTERPRETATION_B_W : VIPS_INTERPRETATION_sRGB);
        }
        if (!gray) {
            const QByteArray profile = QColorSpace(QColorSpace::SRgb).iccProfile();
            vips_image_set_blob_copy(image.get_image(), VIPS_META_ICC_NAME, profile.constData(), profile.size());
        }
        auto *options = vips::VImage::option();
        if (extension != QStringLiteral("tiff")) {
            options->set("lossless", TRUE);
        }
        if (extension != QStringLiteral("jp2") && extension != QStringLiteral("tiff")) {
            options->set("bitdepth", highBitDepth ? (extension == QStringLiteral("jxl") ? 16 : 12) : 8)
                ->set("effort", 1);
        }
        if (extension == QStringLiteral("avif") || extension == QStringLiteral("heic")) {
            options->set("subsample_mode", VIPS_FOREIGN_SUBSAMPLE_OFF);
        }
        image.write_to_file(path.toUtf8().constData(), options);
        const auto stored = vips::VImage::new_from_file(path.toUtf8().constData());
        QCOMPARE(stored.format(), highBitDepth ? VIPS_FORMAT_USHORT : VIPS_FORMAT_UCHAR);
        if (!gray && extension != QStringLiteral("jp2")) {
            QVERIFY(stored.get_typeof(VIPS_META_ICC_NAME) != 0);
        }
    } catch (const vips::VError &error) {
        QFAIL(error.what());
    }
    ImageEntry entry = imageEntryFromFileInfo(QFileInfo(path));
    if (archiveType != 0) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString archivePath = root.filePath(archiveType == 1 ? QStringLiteral("sample.zip") : QStringLiteral("sample.7z"));
        QVERIFY(writeZipArchive(archivePath, { { QFileInfo(path).fileName(), file.readAll() } }, archiveType == 2));
        const auto scan = scanArchive(archivePath);
        QVERIFY2(scan.ok(), qPrintable(scan.errorMessage));
        QCOMPARE(scan.entries.size(), 1);
        entry = scan.entries.first();
    } else {
        const auto scan = scanFolder(root.path());
        QVERIFY2(scan.ok(), qPrintable(scan.errorMessage));
        QCOMPARE(scan.entries.size(), 1);
    }
    for (int mode = 0; mode < 3; ++mode) {
        DecodeRequest request;
        request.colorManagementEnabled = colorManagement;
        request.viewportSize = mode == 2 ? QSize(16, 12) : QSize(40, 30);
        request.fitToWindow = mode == 0;
        request.regionRendering = mode == 2;
        request.rotationDegrees = mode == 2 ? 90 : 0;
        request.panOffset = QPointF(-6, -8);
        const auto decoded = VipsImageDecoder().decode(entry, request);
        QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
        QCOMPARE(decoded.sourceSize, QSize(80, 60));
        QCOMPARE(decoded.regionBased, mode == 2);
        QImage reference = mode == 2 ? expected.transformed(QTransform().rotate(90)) : expected;
        reference = reference.copy(decoded.sourceRegion).scaled(decoded.image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        for (int y : { reference.height() / 4, reference.height() * 3 / 4 }) {
            for (int x : { reference.width() / 4, reference.width() * 3 / 4 }) {
                const QColor actual = decoded.image.pixelColor(x, y);
                const QColor wanted = reference.pixelColor(x, y);
                QVERIFY2(std::abs(actual.red() - wanted.red()) <= 4, qPrintable(QStringLiteral("mode %1 red: %2 != %3").arg(mode).arg(actual.red()).arg(wanted.red())));
                QVERIFY(std::abs(actual.green() - wanted.green()) <= 4);
                QVERIFY(std::abs(actual.blue() - wanted.blue()) <= 4);
                QVERIFY2(std::abs(actual.alpha() - wanted.alpha()) <= 2, qPrintable(QStringLiteral("mode %1 alpha: %2 != %3").arg(mode).arg(actual.alpha()).arg(wanted.alpha())));
            }
        }
        request.maxDecodedBytes = 1;
        const auto tooLarge = VipsImageDecoder().decode(entry, request);
        QVERIFY(!tooLarge.ok());
        QCOMPARE(tooLarge.errorReason, DecodeErrorReason::RenderTooLarge);
    }
}

void CoreTests::decoderModernFormatFrames_data()
{
    QTest::addColumn<QString>("extension");
    QTest::addColumn<bool>("archive");
    for (const QString &extension : { QStringLiteral("avif"), QStringLiteral("jxl"), QStringLiteral("heic") }) {
        QTest::newRow(qPrintable(extension + QStringLiteral("-file"))) << extension << false;
        QTest::newRow(qPrintable(extension + QStringLiteral("-zip"))) << extension << true;
    }
}

void CoreTests::decoderModernFormatFrames()
{
    QFETCH(QString, extension);
    QFETCH(bool, archive);
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QDir root(tempDir.path());
    const QString path = root.filePath(QStringLiteral("frames.") + extension);
    try {
        auto first = vips::VImage::black(32, 24, vips::VImage::option()->set("bands", 3))
                         .new_from_image({ 40, 120, 200 }).copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB));
        auto second = first.new_from_image({ 180, 60, 30 });
        auto frames = first.join(second, VIPS_DIRECTION_VERTICAL);
        frames.set(VIPS_META_PAGE_HEIGHT, 24);
        frames.set("delay", std::vector<int> { 100, 100 });
        frames.write_to_file(path.toUtf8().constData(), vips::VImage::option()->set("lossless", TRUE)->set("effort", 1));
        const auto all = vips::VImage::new_from_file(path.toUtf8().constData(), vips::VImage::option()->set("n", -1));
        QCOMPARE(all.height(), 48);
        QCOMPARE(all.get_int(VIPS_META_N_PAGES), 2);
    } catch (const vips::VError &error) {
        QFAIL(error.what());
    }
    if (extension != QStringLiteral("jxl")) {
        // The generated HEIF fixture has two image items. Select the second as
        // primary to catch accidentally forcing page=0 or n=1 in the loader.
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QByteArray bytes = file.readAll();
        QList<quint16> imageIds;
        for (qsizetype pos = bytes.indexOf("infe"); pos >= 0; pos = bytes.indexOf("infe", pos + 4)) {
            QVERIFY(pos + 16 <= bytes.size());
            QCOMPARE(static_cast<unsigned char>(bytes[pos + 4]), 2);
            if (bytes.mid(pos + 12, 4) == (extension == QStringLiteral("avif") ? "av01" : "hvc1")) {
                imageIds.append(qFromBigEndian<quint16>(bytes.constData() + pos + 8));
            }
        }
        QCOMPARE(imageIds.size(), 2);
        const qsizetype primary = bytes.indexOf("pitm");
        QVERIFY(primary >= 0 && primary + 10 <= bytes.size());
        QCOMPARE(static_cast<unsigned char>(bytes[primary + 4]), 0);
        qToBigEndian<quint16>(imageIds.last(), bytes.data() + primary + 8);
        QVERIFY(file.seek(0));
        QCOMPARE(file.write(bytes), bytes.size());
    }
    ImageEntry entry = imageEntryFromFileInfo(QFileInfo(path));
    if (archive) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString archivePath = root.filePath(QStringLiteral("frames.zip"));
        QVERIFY(writeZipArchive(archivePath, { { QFileInfo(path).fileName(), file.readAll() } }));
        const auto scan = scanArchive(archivePath);
        QVERIFY2(scan.ok(), qPrintable(scan.errorMessage));
        QCOMPARE(scan.entries.size(), 1);
        entry = scan.entries.first();
    }
    DecodeRequest request;
    request.viewportSize = QSize(32, 24);
    const auto decoded = VipsImageDecoder().decode(entry, request);
    QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
    QCOMPARE(decoded.sourceSize, QSize(32, 24));
    if (extension == QStringLiteral("jxl")) {
        QVERIFY(decoded.animated);
    }
    const QColor pixel = decoded.image.pixelColor(16, 12);
    const QColor wanted = extension != QStringLiteral("jxl") ? QColor(180, 60, 30) : QColor(40, 120, 200);
    QVERIFY(std::abs(pixel.red() - wanted.red()) <= 4);
    QVERIFY(std::abs(pixel.green() - wanted.green()) <= 4);
    QVERIFY(std::abs(pixel.blue() - wanted.blue()) <= 4);
}

void CoreTests::decoderExifRotationAndRegion_data()
{
    QTest::addColumn<bool>("autorotate");
    QTest::addColumn<int>("rotation");
    for (bool autorotate : { false, true }) {
        for (int rotation : { 0, 90, 180, 270 }) {
            const QByteArray name = QByteArray::number(autorotate) + '-' + QByteArray::number(rotation);
            QTest::newRow(name.constData()) << autorotate << rotation;
        }
    }
}

void CoreTests::decoderExifRotationAndRegion()
{
    QFETCH(bool, autorotate);
    QFETCH(int, rotation);
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString sourcePath = QDir(tempDir.path()).filePath(QStringLiteral("source.png"));
    const QString orientedPath = QDir(tempDir.path()).filePath(QStringLiteral("oriented.jpg"));
    QImage source(1280, 960, QImage::Format_RGB32);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            source.setPixelColor(x, y, QColor(20 + x * 200 / source.width(),
                20 + y * 200 / source.height(), 30 + (x + 2 * y) * 150 / 3200));
        }
    }
    QVERIFY(source.save(sourcePath));
    try {
        vips::VImage image = vips::VImage::new_from_file(sourcePath.toUtf8().constData());
        image.set(VIPS_META_ORIENTATION, 6);
        image.write_to_file(orientedPath.toUtf8().constData());
    } catch (const vips::VError &error) {
        QFAIL(error.what());
    }
    DecodeRequest request;
    request.viewportSize = QSize(100, 80);
    request.devicePixelRatio = 2.0;
    request.fitToWindow = false;
    request.regionRendering = true;
    request.exifAutorotate = autorotate;
    request.rotationDegrees = rotation;
    request.panOffset = QPointF(37, -23);
    const DecodedImage decoded = VipsImageDecoder().decode(imageEntryFromFileInfo(QFileInfo(orientedPath)), request);
    const int totalRotation = (rotation + (autorotate ? 90 : 0)) % 360;
    QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
    const QSize orientedSize = autorotate ? QSize(960, 1280) : QSize(1280, 960);
    const QSize rotatedSize = rotation % 180 == 0 ? orientedSize : orientedSize.transposed();
    QCOMPARE(decoded.sourceSize, orientedSize);
    QCOMPARE(decoded.fullDisplaySize, rotatedSize / 2);
    QCOMPARE(decoded.image.devicePixelRatio(), 2.0);
    QVERIFY(decoded.regionBased);
    QVERIFY(QRect(QPoint(), rotatedSize).contains(decoded.sourceRegion));
    QCOMPARE(decoded.image.size(), decoded.sourceRegion.size());
    // Independently derive the crop from the logical viewport, pan and 128px overscan.
    const QRect expectedRegion = QRect(rotatedSize.width() / 2 - 430,
        rotatedSize.height() / 2 - 290, 712, 672).intersected(QRect(QPoint(), rotatedSize));
    QCOMPARE(decoded.sourceRegion, expectedRegion);
    QCOMPARE(decoded.displayRegionOffset, expectedRegion.topLeft() / 2);
    for (double fy : { 0.1, 0.5, 0.9 }) {
        for (double fx : { 0.1, 0.5, 0.9 }) {
            const QPoint local(qRound((decoded.image.width() - 1) * fx),
                qRound((decoded.image.height() - 1) * fy));
            const QPoint rotated = expectedRegion.topLeft() + local;
            QPoint original;
            switch (totalRotation) {
            case 90: original = QPoint(rotated.y(), source.height() - 1 - rotated.x()); break;
            case 180: original = QPoint(source.width() - 1 - rotated.x(), source.height() - 1 - rotated.y()); break;
            case 270: original = QPoint(source.width() - 1 - rotated.y(), rotated.x()); break;
            default: original = rotated; break;
            }
            const QColor expected = source.pixelColor(original);
            const QColor actual = decoded.image.pixelColor(local);
            // JPEG quantization and chroma subsampling may perturb this smooth fixture.
            QVERIFY(std::abs(actual.red() - expected.red()) <= 5);
            QVERIFY(std::abs(actual.green() - expected.green()) <= 5);
            QVERIFY(std::abs(actual.blue() - expected.blue()) <= 5);
        }
    }
}

void CoreTests::decoderTransformedSampling_data()
{
    QTest::addColumn<int>("orientation");
    QTest::addColumn<int>("rotation");
    QTest::addColumn<double>("scale");
    QTest::addColumn<bool>("archived");
    for (int orientation : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 }) {
        for (int rotation : { 0, 90, 180, 270 }) {
            for (double scale : { 0.375, 0.625, 1.3, 2.25 }) {
                for (bool archived : { false, true }) {
                    const QByteArray name = QByteArray::number(orientation) + '-'
                        + QByteArray::number(rotation) + '-' + QByteArray::number(scale)
                        + (archived ? "-zip" : "-file");
                    QTest::newRow(name.constData()) << orientation << rotation << scale << archived;
                }
            }
        }
    }
    for (int orientation : { 1, 6 }) {
        for (int rotation : { 0, 90 }) {
            for (double scale : { 64.0, 255.5, 256.0 }) {
                for (bool archived : { false, true }) {
                    const QByteArray name = QByteArray::number(orientation) + '-'
                        + QByteArray::number(rotation) + '-' + QByteArray::number(scale)
                        + (archived ? "-zip" : "-file");
                    QTest::newRow(name.constData()) << orientation << rotation << scale << archived;
                }
            }
        }
    }
}

void CoreTests::decoderTransformedSampling()
{
    QFETCH(int, orientation);
    QFETCH(int, rotation);
    QFETCH(double, scale);
    QFETCH(bool, archived);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString input = temp.filePath(QStringLiteral("input.png"));
    const QString path = temp.filePath(QStringLiteral("oriented.jpg"));
    QImage fixture(401, 303, QImage::Format_RGB32);
    for (int y = 0; y < fixture.height(); ++y) {
        for (int x = 0; x < fixture.width(); ++x) {
            fixture.setPixelColor(x, y, QColor((x * 37 + y * 13) % 256,
                (x * 7 + y * 43) % 256, (x * 19 + y * 3) % 256));
        }
    }
    QVERIFY(fixture.save(input));
    auto tagged = vips::VImage::new_from_file(input.toUtf8().constData());
    if (orientation != 0) tagged.set(VIPS_META_ORIENTATION, orientation);
    tagged.write_to_file(path.toUtf8().constData());
    ImageEntry entry = imageEntryFromFileInfo(QFileInfo(path));
    if (archived) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString zip = temp.filePath(QStringLiteral("images.zip"));
        QVERIFY(writeZipArchive(zip, { { QStringLiteral("oriented.jpg"), file.readAll() } }));
        const auto scan = scanArchive(zip);
        QVERIFY2(scan.ok(), qPrintable(scan.errorMessage));
        QCOMPARE(scan.entries.size(), 1);
        entry = scan.entries.first();
    }
    // A small fully materialized fixture is the independent reference. Production
    // is required to keep its intermediate bounded to the requested region/scale.
    const auto rawReference = vips::VImage::new_from_file(path.toUtf8().constData()).copy_memory();
    for (bool autorotate : { false, true }) {
        for (int mode : { 0, 1, 2 }) { // fit, region, full non-fit
            DecodeRequest request;
            request.exifAutorotate = autorotate;
            request.rotationDegrees = rotation;
            request.viewportSize = QSize(51, 37);
            request.devicePixelRatio = 1.5;
            if (mode != 0 && scale > 16.0) {
                request.devicePixelRatio = orientation == 1 ? 1.0 : 2.0;
            }
            request.fitToWindow = mode == 0;
            request.regionRendering = mode == 1;
            request.scaleFactor = scale;
            request.panOffset = QPointF(83, -71);
            request.colorManagementEnabled = false;
            request.scalingQuality = static_cast<ScalingQuality>(orientation % 4);
            const auto decoded = VipsImageDecoder().decode(entry, request);
            QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
            if (mode != 0 && scale > 16.0) {
                QVERIFY(decoded.regionBased);
                QVERIFY(decoded.image.sizeInBytes() < 4 * 1024 * 1024);
            }
            auto reference = autorotate ? rawReference.autorot() : rawReference;
            QCOMPARE(decoded.sourceSize, QSize(reference.width(), reference.height()));
            if (rotation == 90) reference = reference.rot90();
            if (rotation == 180) reference = reference.rot180();
            if (rotation == 270) reference = reference.rot270();
            const double expectedScale = mode == 0
                ? std::max(0.01, std::min(77.0 / reference.width(), 56.0 / reference.height()))
                : scale;
            QCOMPARE(decoded.effectiveScale, expectedScale);
            const QRect crop = decoded.sourceRegion;
            QVERIFY(QRect(0, 0, reference.width(), reference.height()).contains(crop));
            if (std::abs(expectedScale - 1.0) > 0.001) {
                const VipsKernel kernels[] = { VIPS_KERNEL_LINEAR, VIPS_KERNEL_MITCHELL,
                    VIPS_KERNEL_LANCZOS3, VIPS_KERNEL_MKS2021 };
                reference = reference.resize(expectedScale,
                    vips::VImage::option()->set("kernel", kernels[orientation % 4]));
            }
            if (decoded.regionBased) {
                const int left = static_cast<int>(std::floor(crop.x() * expectedScale));
                const int top = static_cast<int>(std::floor(crop.y() * expectedScale));
                reference = reference.extract_area(left, top,
                    decoded.image.width(), decoded.image.height());
            }
            QCOMPARE(decoded.image.size(), QSize(reference.width(), reference.height()));
            size_t bytes = 0;
            auto *pixels = static_cast<unsigned char *>(reference.write_to_memory(&bytes));
            QVERIFY(pixels != nullptr);
            const QImage expected(pixels, reference.width(), reference.height(),
                reference.width() * 3, QImage::Format_RGB888);
            int maxDifference = 0;
            for (int y = 0; y < expected.height(); ++y) {
                const auto *actualRow = decoded.image.constScanLine(y);
                const auto *expectedRow = expected.constScanLine(y);
                for (int x = 0; x < expected.width() * 3; ++x) {
                    maxDifference = std::max(maxDifference, std::abs(actualRow[x] - expectedRow[x]));
                }
            }
            g_free(pixels);
            // Scalar/strip and SIMD tile evaluation can round a channel by one.
            QVERIFY2(maxDifference <= 1,
                qPrintable(QStringLiteral("Pixel mismatch %1, mode %2, autorotate %3")
                    .arg(maxDifference).arg(mode).arg(autorotate)));
        }
    }
}

void CoreTests::decoderRegionFilteringWithoutOverscan()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("pattern.png"));
    QImage fixture(16, 16, QImage::Format_RGB888);
    for (int y = 0; y < fixture.height(); ++y) {
        for (int x = 0; x < fixture.width(); ++x) {
            fixture.setPixelColor(x, y, QColor((x * 37 + y * 13) % 256,
                (x * 7 + y * 43) % 256, (x * 19 + y * 3) % 256));
        }
    }
    QVERIFY(fixture.save(path));
    DecodeRequest request;
    request.fitToWindow = false;
    request.viewportSize = QSize(320, 240);
    request.scaleFactor = 256;
    request.colorManagementEnabled = false;
    request.scalingQuality = ScalingQuality::HighLanczos3;
    const auto full = vips::VImage::new_from_file(path.toUtf8().constData()).copy_memory()
        .resize(request.scaleFactor, vips::VImage::option()->set("kernel", VIPS_KERNEL_LANCZOS3));
    for (QPointF pan : { QPointF(), QPointF(137, -79), QPointF(-211, 149) }) {
        request.panOffset = pan;
        request.maxDecodedBytes = (pan.isNull() ? 1 : 2) * 1024 * 1024;
        const auto decoded = VipsImageDecoder().decode(imageEntryFromFileInfo(QFileInfo(path)), request);
        QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
        QVERIFY(decoded.regionBased);
        QVERIFY(decoded.sourceRegion.width() < 4);
        QVERIFY(decoded.sourceRegion.height() < 4);
        QVERIFY(decoded.image.sizeInBytes() <= request.maxDecodedBytes);
        const QPoint origin = decoded.displayRegionOffset;
        const auto reference = full.extract_area(origin.x(), origin.y(),
            decoded.image.width(), decoded.image.height());
        size_t bytes = 0;
        auto *pixels = static_cast<unsigned char *>(reference.write_to_memory(&bytes));
        QVERIFY(pixels != nullptr);
        const QImage expected(pixels, reference.width(), reference.height(),
            reference.width() * 3, QImage::Format_RGB888);
        const bool equal = decoded.image == expected;
        g_free(pixels);
        QVERIFY(equal);
    }
}

void CoreTests::decoderNativeIntermediateBudget()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("sixteen-bit.png"));
    // Native 16-bit RGBA storage exceeds the final 8-bit output estimate.
    std::vector<quint16> samples(129 * 97 * 4);
    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<quint16>((i * 197 + i / 7) % 65536);
    }
    auto image = vips::VImage::new_from_memory(samples.data(), samples.size() * sizeof(quint16),
        129, 97, 4, VIPS_FORMAT_USHORT).copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_RGB16));
    image.write_to_file(path.toUtf8().constData(), vips::VImage::option()->set("bitdepth", 16));
    QCOMPARE(vips::VImage::new_from_file(path.toUtf8().constData()).format(), VIPS_FORMAT_USHORT);
    const auto entry = imageEntryFromFileInfo(QFileInfo(path));
    DecodeRequest request;
    request.fitToWindow = false;
    request.rotationDegrees = 90;
    request.colorManagementEnabled = false;
    const auto native = estimateRenderMemory(QSize(129, 97), 4, 1.0, 2);
    QCOMPARE(native.bytes, 129LL * 97LL * 8LL);
    QVERIFY(estimateRenderMemory(QSize(129, 97), 4, 1.0, 0).overflowed);
    QVERIFY(estimateRenderMemory(QSize(129, 97), std::numeric_limits<int>::max(), 1.0, 2).overflowed);
    request.maxDecodedBytes = native.bytes - 1;
    const auto rejected = VipsImageDecoder().decode(entry, request);
    QCOMPARE(rejected.errorReason, DecodeErrorReason::RenderTooLarge);
    request.maxDecodedBytes = native.bytes;
    const auto accepted = VipsImageDecoder().decode(entry, request);
    QVERIFY2(accepted.ok(), qPrintable(accepted.errorMessage));
    QCOMPARE(accepted.image.size(), QSize(97, 129));
    const auto reference = vips::VImage::new_from_file(path.toUtf8().constData()).copy_memory()
        .rot90().colourspace(VIPS_INTERPRETATION_sRGB).cast(VIPS_FORMAT_UCHAR);
    size_t size = 0;
    auto *pixels = static_cast<unsigned char *>(reference.write_to_memory(&size));
    QVERIFY(pixels != nullptr);
    const QImage expected(pixels, 97, 129, 97 * 4, QImage::Format_RGBA8888);
    const QImage ownedReference = expected.copy();
    g_free(pixels);
    QCOMPARE(accepted.image, ownedReference);

    request.viewportSize = QSize(16, 12);
    request.maxDecodedBytes = native.bytes - 1;
    for (bool region : { false, true }) {
        request.regionRendering = region;
        for (QPointF pan : { QPointF(), QPointF(-48.5, -64.5), QPointF(48.5, 64.5) }) {
            request.panOffset = pan;
            const auto visible = VipsImageDecoder().decode(entry, request);
            QVERIFY2(visible.ok(), qPrintable(visible.errorMessage));
            QVERIFY(visible.regionBased);
            QVERIFY(visible.image.width() <= request.viewportSize.width() + 1);
            QVERIFY(visible.image.height() <= request.viewportSize.height() + 1);
            QCOMPARE(visible.image, ownedReference.copy(visible.sourceRegion));
        }
    }
    request.maxDecodedBytes = 1;
    QCOMPARE(VipsImageDecoder().decode(entry, request).errorReason, DecodeErrorReason::RenderTooLarge);
}

void CoreTests::decoderFitHonorsDevicePixelRatio()
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
    request.devicePixelRatio = 2.0;
    request.fitToWindow = true;

    VipsImageDecoder decoder;
    const DecodedImage decoded = decoder.decode(entry, request);
    QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
    QCOMPARE(decoded.sourceSize, QSize(16, 8));
    QCOMPARE(decoded.image.size(), QSize(16, 8));
    QCOMPARE(decoded.image.devicePixelRatio(), 2.0);
    QCOMPARE(decoded.fullDisplaySize, QSize(8, 4));
    QCOMPARE(decoded.effectiveScale, 1.0);
    QCOMPARE(decoded.devicePixelRatio, 2.0);
}

void CoreTests::decoderActualSizeHonorsDevicePixelRatio()
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
    request.devicePixelRatio = 2.0;
    request.fitToWindow = false;
    request.scaleFactor = 1.0;

    VipsImageDecoder decoder;
    const DecodedImage decoded = decoder.decode(entry, request);
    QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
    QCOMPARE(decoded.sourceSize, QSize(16, 8));
    QCOMPARE(decoded.image.size(), QSize(16, 8));
    QCOMPARE(decoded.image.devicePixelRatio(), 2.0);
    QCOMPARE(decoded.fullDisplaySize, QSize(8, 4));
    QCOMPARE(decoded.effectiveScale, 1.0);
}

void CoreTests::decoderAppliesViewRotation()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("sample.png"));

    QImage source(16, 8, QImage::Format_RGB32);
    source.fill(Qt::blue);
    QVERIFY(source.save(path));

    const ImageEntry entry = imageEntryFromFileInfo(QFileInfo(path));
    DecodeRequest request;
    request.viewportSize = QSize(32, 32);
    request.fitToWindow = false;
    request.scaleFactor = 1.0;
    request.exifAutorotate = false;

    VipsImageDecoder decoder;
    const DecodedImage normal = decoder.decode(entry, request);
    QVERIFY2(normal.ok(), qPrintable(normal.errorMessage));
    QCOMPARE(normal.sourceSize, QSize(16, 8));
    QCOMPARE(normal.displaySize, QSize(16, 8));

    request.rotationDegrees = 90;
    const DecodedImage rotated = decoder.decode(entry, request);
    QVERIFY2(rotated.ok(), qPrintable(rotated.errorMessage));
    QCOMPARE(rotated.sourceSize, QSize(16, 8));
    QCOMPARE(rotated.displaySize, QSize(8, 16));
    QCOMPARE(rotated.image.size(), QSize(8, 16));
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

void CoreTests::archiveEntryStreamReadsSequentially()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("sample.zip"));
    const QByteArray expected = pngBytes(QSize(12, 6), Qt::blue);
    QVERIFY(writeZipArchive(path, {
        { QStringLiteral("sample.png"), expected },
    }));

    const ScanResult scan = scanArchive(path);
    QVERIFY2(scan.ok(), qPrintable(scan.errorMessage));
    QCOMPARE(scan.entries.size(), 1);

    ArchiveEntryStreamReader reader(scan.entries.first());
    QVERIFY2(reader.open(), qPrintable(reader.errorMessage()));

    QByteArray actual;
    std::array<char, 5> buffer;
    for (;;) {
        const qint64 bytesRead = reader.read(buffer.data(), buffer.size());
        QVERIFY(bytesRead >= 0);
        if (bytesRead == 0) {
            break;
        }
        actual.append(buffer.data(), static_cast<qsizetype>(bytesRead));
    }

    QCOMPARE(actual, expected);
}

void CoreTests::decoderLoadsArchiveEntryWhenBoundedReadWouldFail()
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
    QVERIFY(!readArchiveEntry(scan.entries.first(), {}, 1).ok());

    DecodeRequest request;
    request.viewportSize = QSize(6, 6);
    request.fitToWindow = true;

    VipsImageDecoder decoder;
    const DecodedImage decoded = decoder.decode(scan.entries.first(), request);
    QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
    QCOMPARE(decoded.sourceSize, QSize(12, 6));
}

void CoreTests::decoderReportsMissingArchiveEntry()
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

    ImageEntry missing = scan.entries.first();
    missing.archiveEntryPath = QStringLiteral("missing.png");

    DecodeRequest request;
    request.viewportSize = QSize(6, 6);
    request.fitToWindow = true;

    VipsImageDecoder decoder;
    const DecodedImage decoded = decoder.decode(missing, request);
    QVERIFY(!decoded.ok());
    QVERIFY(decoded.errorMessage.contains(QStringLiteral("not found"), Qt::CaseInsensitive));
    QCOMPARE(decoded.errorReason, DecodeErrorReason::Other);
}

void CoreTests::decoderRegionRenderingLimitsZoomOutput()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("sample.png"));

    QImage source(400, 300, QImage::Format_RGB32);
    source.fill(Qt::blue);
    QVERIFY(source.save(path));

    const ImageEntry entry = imageEntryFromFileInfo(QFileInfo(path));

    DecodeRequest fullRequest;
    fullRequest.viewportSize = QSize(100, 80);
    fullRequest.fitToWindow = false;
    fullRequest.regionRendering = false;
    fullRequest.scaleFactor = 4.0;
    fullRequest.maxDecodedBytes = 512 * 1024;

    VipsImageDecoder decoder;
    const DecodedImage fullDecoded = decoder.decode(entry, fullRequest);
    QVERIFY2(fullDecoded.ok(), qPrintable(fullDecoded.errorMessage));
    QVERIFY(fullDecoded.regionBased);

    DecodeRequest regionRequest = fullRequest;
    regionRequest.regionRendering = true;
    const DecodedImage regionDecoded = decoder.decode(entry, regionRequest);
    QVERIFY2(regionDecoded.ok(), qPrintable(regionDecoded.errorMessage));
    QVERIFY(regionDecoded.regionBased);
    QCOMPARE(regionDecoded.sourceSize, QSize(400, 300));
    QCOMPARE(regionDecoded.fullDisplaySize, QSize(1600, 1200));
    QVERIFY(regionDecoded.image.width() < regionDecoded.fullDisplaySize.width());
    QVERIFY(regionDecoded.image.height() < regionDecoded.fullDisplaySize.height());
    QVERIFY(regionDecoded.image.sizeInBytes() <= regionRequest.maxDecodedBytes);
    QVERIFY(!regionDecoded.sourceRegion.isEmpty());
    QCOMPARE(fullDecoded.image, regionDecoded.image);
    QCOMPARE(fullDecoded.sourceRegion, regionDecoded.sourceRegion);

    fullRequest.scaleFactor = 1.0;
    const auto smallFull = decoder.decode(entry, fullRequest);
    QVERIFY2(smallFull.ok(), qPrintable(smallFull.errorMessage));
    QVERIFY(!smallFull.regionBased);
    QCOMPARE(smallFull.image.size(), source.size());

    regionRequest.maxDecodedBytes = 64 * 1024;
    const auto visible = decoder.decode(entry, regionRequest);
    QVERIFY2(visible.ok(), qPrintable(visible.errorMessage));
    QVERIFY(visible.image.width() < regionDecoded.image.width());
    QVERIFY(visible.image.height() < regionDecoded.image.height());
    QVERIFY(visible.image.sizeInBytes() <= regionRequest.maxDecodedBytes);

    regionRequest.maxDecodedBytes = 1;
    QCOMPARE(decoder.decode(entry, regionRequest).errorReason, DecodeErrorReason::RenderTooLarge);
}

void CoreTests::decoderRegionRenderingHonorsDevicePixelRatio()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("sample.png"));

    QImage source(400, 300, QImage::Format_RGB32);
    source.fill(Qt::blue);
    QVERIFY(source.save(path));

    const ImageEntry entry = imageEntryFromFileInfo(QFileInfo(path));
    DecodeRequest request;
    request.viewportSize = QSize(100, 80);
    request.devicePixelRatio = 2.0;
    request.fitToWindow = false;
    request.regionRendering = true;
    request.scaleFactor = 4.0;
    request.maxDecodedBytes = 2 * 1024 * 1024;

    VipsImageDecoder decoder;
    const DecodedImage decoded = decoder.decode(entry, request);
    QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
    QVERIFY(decoded.regionBased);
    QCOMPARE(decoded.sourceSize, QSize(400, 300));
    QCOMPARE(decoded.fullDisplaySize, QSize(800, 600));
    QCOMPARE(decoded.image.devicePixelRatio(), 2.0);
    QCOMPARE(decoded.effectiveScale, 4.0);
    QCOMPARE(decoded.devicePixelRatio, 2.0);
    QVERIFY(decoded.image.width() < decoded.fullDisplaySize.width() * decoded.devicePixelRatio);
    QVERIFY(decoded.image.height() < decoded.fullDisplaySize.height() * decoded.devicePixelRatio);
    QCOMPARE(decoded.displayRegionOffset, QPoint(decoded.sourceRegion.left() * 2, decoded.sourceRegion.top() * 2));

    request.maxDecodedBytes = 256 * 1024;
    const auto visible = decoder.decode(entry, request);
    QVERIFY2(visible.ok(), qPrintable(visible.errorMessage));
    QCOMPARE(visible.image.devicePixelRatio(), 2.0);
    QCOMPARE(visible.fullDisplaySize, decoded.fullDisplaySize);
    QVERIFY(visible.image.width() < decoded.image.width());
    QVERIFY(visible.image.height() < decoded.image.height());
    QVERIFY(visible.image.sizeInBytes() <= request.maxDecodedBytes);
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

    request.maxDecodedBytes = 384LL * 1024 * 1024;
    for (double scale : { 1e20, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() }) {
        request.scaleFactor = scale;
        QCOMPARE(decoder.decode(entry, request).errorReason, DecodeErrorReason::RenderTooLarge);
    }
    request.scaleFactor = 64.0;
    request.regionRendering = true;
    request.panOffset = QPointF(1e100, -1e100);
    const auto offscreen = decoder.decode(entry, request);
    QVERIFY2(offscreen.ok(), qPrintable(offscreen.errorMessage));
    QVERIFY(QRect(QPoint(), source.size()).contains(offscreen.sourceRegion));
}

void CoreTests::decoderUsesConfiguredScalingQuality_data()
{
    QTest::addColumn<int>("quality");
    QTest::addColumn<double>("scale");
    for (ScalingQuality quality : { ScalingQuality::FastLinear, ScalingQuality::BalancedMitchell,
             ScalingQuality::HighLanczos3, ScalingQuality::SharpestMks2021 }) {
        for (double scale : { 0.125, 0.625, 1.5, 3.0 }) {
            const QByteArray name = QByteArray::number(static_cast<int>(quality)) + '-' + QByteArray::number(scale);
            QTest::newRow(name.constData()) << static_cast<int>(quality) << scale;
        }
    }
}

void CoreTests::decoderUsesConfiguredScalingQuality()
{
    QFETCH(int, quality);
    QFETCH(double, scale);
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("sample.png"));

    QImage source(64, 40, QImage::Format_RGB32);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            source.setPixelColor(x, y, QColor((x * 17) % 255, (y * 31) % 255, ((x + y) * 11) % 255));
        }
    }
    QVERIFY(source.save(path));

    const ImageEntry entry = imageEntryFromFileInfo(QFileInfo(path));
    DecodeRequest request;
    request.viewportSize = QSize(24, 12);
    request.fitToWindow = false;
    request.regionRendering = false;
    request.colorManagementEnabled = false;
    request.scaleFactor = scale;
    request.scalingQuality = static_cast<ScalingQuality>(quality);

    VipsImageDecoder decoder;
    const DecodedImage decoded = decoder.decode(entry, request);
    QVERIFY2(decoded.ok(), qPrintable(decoded.errorMessage));
    QCOMPARE(decoded.image.size(), QSize(qRound(64 * scale), qRound(40 * scale)));
    QCOMPARE(decoded.fullDisplaySize, decoded.image.size());
    QCOMPARE(decoded.sourceSize, source.size());
    QCOMPARE(decoded.effectiveScale, scale);
    // Independent backend oracle checks the preset-to-kernel contract, not just geometry.
    const std::array<VipsKernel, 4> kernels = { VIPS_KERNEL_LINEAR, VIPS_KERNEL_MITCHELL,
        VIPS_KERNEL_LANCZOS3, VIPS_KERNEL_MKS2021 };
    try {
        const vips::VImage input = vips::VImage::new_from_file(path.toUtf8().constData());
        const vips::VImage reference = input.resize(scale,
            vips::VImage::option()->set("kernel", kernels.at(quality))).cast(VIPS_FORMAT_UCHAR);
        size_t length = 0;
        void *memory = reference.write_to_memory(&length);
        const QImage expected(static_cast<const uchar *>(memory), reference.width(), reference.height(),
            reference.width() * 3, QImage::Format_RGB888);
        const bool matches = decoded.image.convertToFormat(QImage::Format_RGB888) == expected;
        g_free(memory);
        QVERIFY2(matches, "Rendered pixels do not match the selected kernel");
    } catch (const vips::VError &error) {
        QFAIL(error.what());
    }

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

void CoreTests::decodeRequestCacheKeyIncludesRotation()
{
    DecodeRequest unrotated;
    unrotated.viewportSize = QSize(800, 600);
    unrotated.scaleFactor = 1.5;
    unrotated.fitToWindow = false;
    unrotated.exifAutorotate = true;

    DecodeRequest rotated = unrotated;
    rotated.rotationDegrees = 90;

    QVERIFY(unrotated.cacheKeySuffix() != rotated.cacheKeySuffix());
}

void CoreTests::decodeRequestCacheKeyIncludesRegionAndPan()
{
    DecodeRequest full;
    full.viewportSize = QSize(800, 600);
    full.scaleFactor = 2.0;
    full.fitToWindow = false;
    full.regionRendering = false;

    DecodeRequest fullPanned = full;
    fullPanned.panOffset = QPointF(12.0, -8.0);
    QCOMPARE(full.cacheKeySuffix(), fullPanned.cacheKeySuffix());

    DecodeRequest region = full;
    region.regionRendering = true;
    QVERIFY(full.cacheKeySuffix() != region.cacheKeySuffix());

    DecodeRequest panned = region;
    panned.panOffset = QPointF(12.0, -8.0);
    QVERIFY(region.cacheKeySuffix() != panned.cacheKeySuffix());
}

void CoreTests::decodeRequestCacheKeyIncludesScalingQuality()
{
    DecodeRequest highQuality;
    highQuality.viewportSize = QSize(800, 600);
    highQuality.scaleFactor = 1.5;
    highQuality.fitToWindow = false;
    highQuality.scalingQuality = ScalingQuality::HighLanczos3;

    DecodeRequest sharpest = highQuality;
    sharpest.scalingQuality = ScalingQuality::SharpestMks2021;

    QVERIFY(highQuality.cacheKeySuffix() != sharpest.cacheKeySuffix());
}

void CoreTests::decodeRequestCacheKeyIncludesColorManagement()
{
    DecodeRequest enabled;
    enabled.viewportSize = QSize(800, 600);
    enabled.scaleFactor = 1.5;
    enabled.fitToWindow = false;
    enabled.colorManagementEnabled = true;

    DecodeRequest disabled = enabled;
    disabled.colorManagementEnabled = false;

    QVERIFY(enabled.cacheKeySuffix() != disabled.cacheKeySuffix());
}

void CoreTests::decodeRequestCacheKeyIncludesDevicePixelRatio()
{
    DecodeRequest normalDpr;
    normalDpr.viewportSize = QSize(800, 600);
    normalDpr.scaleFactor = 1.5;
    normalDpr.fitToWindow = false;
    normalDpr.devicePixelRatio = 1.0;

    DecodeRequest highDpr = normalDpr;
    highDpr.devicePixelRatio = 2.0;

    QVERIFY(normalDpr.cacheKeySuffix() != highDpr.cacheKeySuffix());
}

void CoreTests::prefetchPolicyAllowsOnlyFitSizedRequests()
{
    DecodeRequest fit;
    fit.viewportSize = QSize(800, 600);
    fit.fitToWindow = true;
    QVERIFY(shouldPrefetchDecodeRequest(fit));

    DecodeRequest emptyViewport = fit;
    emptyViewport.viewportSize = QSize();
    QVERIFY(!shouldPrefetchDecodeRequest(emptyViewport));

    DecodeRequest actualSize = fit;
    actualSize.fitToWindow = false;
    actualSize.scaleFactor = 1.0;
    QVERIFY(!shouldPrefetchDecodeRequest(actualSize));

    DecodeRequest customZoom = fit;
    customZoom.fitToWindow = false;
    customZoom.scaleFactor = 2.0;
    QVERIFY(!shouldPrefetchDecodeRequest(customZoom));
}

void CoreTests::shortcutSettingsDefaultsAndCustomization()
{
    AppSettings values = AppSettings::defaults();

    QCOMPARE(shortcutSequences(values.shortcuts, ShortcutCommand::PreviousImage), defaultShortcutSequences(ShortcutCommand::PreviousImage));
    QCOMPARE(shortcutSequences(values.shortcuts, ShortcutCommand::PreviousFolder), QStringList({ QStringLiteral("MouseBack") }));
    QCOMPARE(shortcutSequences(values.shortcuts, ShortcutCommand::NextFolder), QStringList({ QStringLiteral("MouseForward") }));
    QCOMPARE(shortcutSequences(values.shortcuts, ShortcutCommand::CopyOriginalImage), QStringList({ QStringLiteral("Ctrl+C") }));
    QCOMPARE(shortcutSequences(values.shortcuts, ShortcutCommand::ExifAutorotate), QStringList());
    QVERIFY(values.viewer.regionRenderingEnabled);
    QCOMPARE(values.viewer.scalingQuality, ScalingQuality::HighLanczos3);
    QVERIFY(values.viewer.colorManagementEnabled);
    QVERIFY(values.fullscreen.infoLineEnabled);
    QCOMPARE(values.fullscreen.infoLineFontFamily, QStringLiteral("DejaVu Sans"));
    QCOMPARE(values.fullscreen.infoLineFontPointSize, 10);
    QCOMPARE(values.fullscreen.infoLineColor, QStringLiteral("#00ff00"));
    QCOMPARE(values.interface.theme, UiTheme::Light);

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
    values.viewer.scalingQuality = ScalingQuality::HighLanczos3;
    values.viewer.colorManagementEnabled = false;
    values.viewer.regionRenderingEnabled = false;
    values.fullscreen.sideTriggerRatio = 0.12;
    values.fullscreen.verticalTriggerRatio = 0.08;
    values.fullscreen.infoLineEnabled = false;
    values.fullscreen.infoLineFontFamily = QStringLiteral("Serif");
    values.fullscreen.infoLineFontPointSize = 18;
    values.fullscreen.infoLineColor = QStringLiteral("#aabbcc");
    values.interface.theme = UiTheme::Dark;
    values.interface.defaultComparisonMode = ComparisonMode::Carousel;
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
    QCOMPARE(loaded.viewer.scalingQuality, ScalingQuality::HighLanczos3);
    QVERIFY(!loaded.viewer.colorManagementEnabled);
    QVERIFY(!loaded.viewer.regionRenderingEnabled);
    QCOMPARE(loaded.fullscreen.sideTriggerRatio, 0.12);
    QCOMPARE(loaded.fullscreen.verticalTriggerRatio, 0.08);
    QVERIFY(!loaded.fullscreen.infoLineEnabled);
    QCOMPARE(loaded.fullscreen.infoLineFontFamily, QStringLiteral("Serif"));
    QCOMPARE(loaded.fullscreen.infoLineFontPointSize, 18);
    QCOMPARE(loaded.fullscreen.infoLineColor, QStringLiteral("#aabbcc"));
    QCOMPARE(loaded.interface.theme, UiTheme::Dark);
    QCOMPARE(loaded.interface.defaultComparisonMode, ComparisonMode::Carousel);
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
    writeSettings.setValue(QStringLiteral("scalingQuality"), QStringLiteral("invalid"));
    writeSettings.setValue(QStringLiteral("colorManagementEnabled"), true);
    writeSettings.setValue(QStringLiteral("regionRenderingEnabled"), QStringLiteral("invalid"));
    writeSettings.endGroup();
    writeSettings.beginGroup(QStringLiteral("fullscreen"));
    writeSettings.setValue(QStringLiteral("sideTriggerRatio"), -1.0);
    writeSettings.setValue(QStringLiteral("verticalTriggerRatio"), 99.0);
    writeSettings.setValue(QStringLiteral("infoLineFontPointSize"), 99);
    writeSettings.setValue(QStringLiteral("infoLineColor"), QStringLiteral("not-a-color"));
    writeSettings.endGroup();
    writeSettings.beginGroup(QStringLiteral("interface"));
    writeSettings.setValue(QStringLiteral("theme"), QStringLiteral("invalid"));
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
    QCOMPARE(loaded.viewer.scalingQuality, ScalingQuality::HighLanczos3);
    QVERIFY(loaded.viewer.colorManagementEnabled);
    QVERIFY(loaded.viewer.regionRenderingEnabled);
    QCOMPARE(loaded.fullscreen.sideTriggerRatio, 0.01);
    QCOMPARE(loaded.fullscreen.verticalTriggerRatio, 0.30);
    QVERIFY(loaded.fullscreen.infoLineEnabled);
    QCOMPARE(loaded.fullscreen.infoLineFontPointSize, 32);
    QCOMPARE(loaded.fullscreen.infoLineColor, AppSettings::defaults().fullscreen.infoLineColor);
    QCOMPARE(loaded.interface.theme, UiTheme::Light);
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
