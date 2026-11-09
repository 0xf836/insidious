#include "ui/main_window.h"
#include "ui/about_dialog.h"
#include "build_version.h"
#include "core/vips_image_decoder.h"
#include "core/render_memory_budget.h"

#include <QAction>
#include <QClipboard>
#include <QMimeData>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QInputDialog>
#include <QPushButton>
#include <QUrl>
#include <QMenu>
#include <QMenuBar>
#include <QToolButton>

#include <QPromise>
#include <QSettings>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtTest>
#include <vips/vips8>
#include <archive.h>
#include <archive_entry.h>

namespace insidious::ui {

class MainWindowAsyncTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void rotateThenZoomAndPan();
    void highZoomWithAutomaticRegionRendering();
    void regionPanGesture_data();
    void regionPanGesture();
    void regionPanPending_data();
    void regionPanPending();
    void animatedViewer_data();
    void animatedViewer();
    void animatedHoldZoom_data();
    void animatedHoldZoom();
    void animationStopsForFolders();
    void staticAnimationFormats_data();
    void staticAnimationFormats();
    void modernFormatsOpenCopyAndNavigate_data();
    void modernFormatsOpenCopyAndNavigate();
    void aboutUsesApplicationVersionAndSharedAction();
    void wrongModalTypeIsClosed_data();
    void wrongModalTypeIsClosed();
    void aboutLinkWorksFromKeyboard();
    void decodeCompletion_data();
    void decodeCompletion();
    void staleScanDoesNotReplaceSource_data();
    void staleScanDoesNotReplaceSource();
    void archiveScanErrorsDoNotPrompt_data();
    void archiveScanErrorsDoNotPrompt();
    void staleFolderLoadDoesNotReplaceBranch_data();
    void staleFolderLoadDoesNotReplaceBranch();
public slots:
    void captureUrl(const QUrl &url) { m_openedUrls.append(url); }

private:
    QList<QUrl> m_openedUrls;
    QTemporaryDir m_settingsDirectory;
};

namespace {

void sendPanMove(ImageViewWidget *view, QPoint position, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
    QMouseEvent event(QEvent::MouseMove, QPointF(position), QPointF(view->mapToGlobal(position)),
        Qt::NoButton, button, modifiers);
    QApplication::sendEvent(view, &event);
}

// Own both timers so an action that returns without a dialog leaves no callbacks.
bool triggerAboutAndCloseModal(QAction *action, bool *timedOut)
{
    bool correctType = false;
    *timedOut = false;
    auto closeModal = [&correctType]() {
        QWidget *modal = QApplication::activeModalWidget();
        correctType = qobject_cast<AboutDialog *>(modal) != nullptr;
        if (auto *dialog = qobject_cast<QDialog *>(modal)) {
            dialog->reject();
        } else if (modal != nullptr) {
            modal->close();
        }
    };
    QTimer closer;
    closer.setSingleShot(true);
    QObject::connect(&closer, &QTimer::timeout, &closer, closeModal);
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &watchdog, [&]() {
        *timedOut = true;
        closeModal();
    });
    closer.start(0);
    watchdog.start(2000);
    action->trigger();
    return correctType;
}

} // namespace

void MainWindowAsyncTests::initTestCase()
{
    QVERIFY(VIPS_INIT("insidious_ui_rotation_tests") == 0);
    vips_concurrency_set(2);
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("InsidiousTests"));
    QCoreApplication::setApplicationName(QStringLiteral("AsyncResults"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(build::Version));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
}

void MainWindowAsyncTests::cleanupTestCase()
{
    QThreadPool::globalInstance()->waitForDone();
    vips_shutdown();
}

void MainWindowAsyncTests::rotateThenZoomAndPan()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("large.jpg"));
    QImage fixture(1280, 960, QImage::Format_RGB32);
    fixture.fill(Qt::blue);
    QVERIFY(fixture.save(path));
    MainWindow window;
    window.m_settings.viewer.regionRenderingEnabled = true;
    window.setFixedSize(800, 600);
    window.show();
    window.openPath(path);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_currentDecoded.ok() && !window.m_imageRenderPending, 5000);
    window.rotateRight();
    QTRY_VERIFY_WITH_TIMEOUT(!window.m_imageRenderPending, 5000);
    QVERIFY(window.m_currentDecoded.ok());
    QCOMPARE(window.m_controller.rotationDegrees(), 90);
    window.setActualSize();
    QTRY_VERIFY_WITH_TIMEOUT(!window.m_imageRenderPending, 5000);
    QVERIFY(window.m_currentDecoded.ok());
    QVERIFY(window.m_currentDecoded.regionBased);
    QVERIFY(window.m_currentDecoded.fullDisplaySize.height() > window.m_currentDecoded.fullDisplaySize.width());
    window.zoomIn();
    QTRY_VERIFY_WITH_TIMEOUT(!window.m_imageRenderPending, 5000);
    QVERIFY(window.m_currentDecoded.ok());
    QVERIFY(window.m_currentDecoded.effectiveScale > 1.0);
    const QRect previousRegion = window.m_currentDecoded.sourceRegion;
    window.m_controller.setPanOffset(QPointF(71, -37));
    window.loadCurrentImage(false);
    QTRY_VERIFY_WITH_TIMEOUT(!window.m_imageRenderPending, 5000);
    QVERIFY(window.m_currentDecoded.ok());
    QVERIFY(window.m_currentDecoded.sourceRegion != previousRegion);
    window.rotateLeft();
    QTRY_VERIFY_WITH_TIMEOUT(!window.m_imageRenderPending, 5000);
    QVERIFY(window.m_currentDecoded.ok());
    QCOMPARE(window.m_controller.rotationDegrees(), 0);
    QVERIFY(window.m_currentDecoded.fullDisplaySize.width() > window.m_currentDecoded.fullDisplaySize.height());
    QVERIFY(window.m_imageView->hasImage());
}

void MainWindowAsyncTests::highZoomWithAutomaticRegionRendering()
{
    QTemporaryDir temp;
    const QString path = temp.filePath(QStringLiteral("large.jpg"));
    QImage fixture(1280, 960, QImage::Format_RGB32);
    fixture.fill(Qt::blue);
    QVERIFY(fixture.save(path));
    MainWindow window;
    window.m_settings.viewer.regionRenderingEnabled = false;
    window.setFixedSize(800, 600);
    window.show();
    window.openPath(path);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_currentDecoded.ok() && !window.m_imageRenderPending, 5000);
    window.applyCustomZoomWithFallback(32.0, QPointF());
    QTRY_VERIFY_WITH_TIMEOUT(!window.m_imageRenderPending, 5000);
    QCOMPARE(window.m_currentDecoded.effectiveScale, 32.0);
    QVERIFY(window.m_currentDecoded.regionBased);
    window.applyCustomZoomWithFallback(core::MaximumZoomFactor, QPointF());
    QTRY_VERIFY_WITH_TIMEOUT(!window.m_imageRenderPending, 5000);
    QCOMPARE(window.m_currentDecoded.effectiveScale, 256.0);
    QVERIFY(window.m_currentDecoded.regionBased);
    QVERIFY(!window.m_cache->contains(window.currentCacheKey(window.currentDecodeRequest())));
    const QRect previousRegion = window.m_currentDecoded.sourceRegion;
    window.m_controller.setPanOffset(QPointF(512, -256));
    window.m_imageView->setPanOffset(window.m_controller.panOffset());
    window.scheduleRegionReload();
    QTRY_VERIFY_WITH_TIMEOUT(window.m_currentDecoded.sourceRegion != previousRegion && !window.m_imageRenderPending, 5000);
    const auto previousView = window.captureViewRenderState();
    auto limitedRequest = window.currentDecodeRequest();
    limitedRequest.maxDecodedBytes = 1;
    const auto generation = ++window.m_decodeGeneration;
    window.m_pendingZoomRenderFallback = MainWindow::PendingZoomRenderFallback { generation, previousView };
    window.m_imageRenderPending = true;
    window.m_controller.setCustomZoom(64.0);
    window.m_controller.resetPan();
    window.startDecodeJob({ generation, window.currentCacheKey(limitedRequest),
        *window.m_controller.currentEntry(), limitedRequest, {} });
    QTRY_VERIFY_WITH_TIMEOUT(!window.m_imageRenderPending, 5000);
    QVERIFY(!window.m_pendingZoomRenderFallback.has_value());
    QCOMPARE(window.m_controller.zoomFactor(), previousView.zoomFactor);
    QCOMPARE(window.m_controller.panOffset(), previousView.panOffset);
    QCOMPARE(window.m_currentDecoded.image, previousView.decoded.image);
    window.zoomOut();
    QTRY_VERIFY_WITH_TIMEOUT(!window.m_imageRenderPending, 5000);
    QVERIFY(window.m_currentDecoded.effectiveScale < 256.0);
    QVERIFY(window.m_currentDecoded.effectiveScale > 16.0);
    window.setActualSize();
    QTRY_VERIFY_WITH_TIMEOUT(!window.m_imageRenderPending, 5000);
    QCOMPARE(window.m_currentDecoded.effectiveScale, 1.0);
    QVERIFY(!window.m_currentDecoded.regionBased);
    window.setFitToWindow();
    QTRY_VERIFY_WITH_TIMEOUT(!window.m_imageRenderPending, 5000);
    QCOMPARE(window.m_controller.zoomMode(), core::ZoomMode::FitToWindow);
    QVERIFY(window.m_imageView->hasImage());
    QVERIFY(!window.m_settings.viewer.regionRenderingEnabled);
}

void MainWindowAsyncTests::regionPanGesture_data()
{
    QTest::addColumn<int>("gesture");
    QTest::addColumn<double>("scale");
    QTest::newRow("left") << 0 << 64.0;
    QTest::newRow("middle") << 1 << 64.0;
    QTest::newRow("ctrl-left") << 2 << 64.0;
    QTest::newRow("hold") << 3 << 64.0;
    QTest::newRow("hold-at-limit") << 3 << 256.0;
    QTest::newRow("actual-size") << 0 << 1.0;
    QTest::newRow("zip-jpeg") << 4 << 64.0;
}

void MainWindowAsyncTests::regionPanGesture()
{
    QFETCH(int, gesture);
    QFETCH(double, scale);
    QTemporaryDir temp;
    QString path = temp.filePath(QStringLiteral("large.jpg"));
    QImage fixture(1280, 960, QImage::Format_RGB32);
    fixture.fill(Qt::blue);
    QVERIFY(fixture.save(path));
    if (gesture == 4) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        path = temp.filePath(QStringLiteral("images.zip"));
        std::unique_ptr<struct archive, decltype(&archive_write_free)> writer(archive_write_new(), &archive_write_free);
        QCOMPARE(archive_write_set_format_zip(writer.get()), ARCHIVE_OK);
        QCOMPARE(archive_write_open_filename(writer.get(), path.toUtf8().constData()), ARCHIVE_OK);
        std::unique_ptr<struct archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(), &archive_entry_free);
        archive_entry_set_pathname(entry.get(), "large.jpg");
        archive_entry_set_filetype(entry.get(), AE_IFREG);
        archive_entry_set_perm(entry.get(), 0644);
        archive_entry_set_size(entry.get(), bytes.size());
        QCOMPARE(archive_write_header(writer.get(), entry.get()), ARCHIVE_OK);
        QCOMPARE(archive_write_data(writer.get(), bytes.constData(), bytes.size()), bytes.size());
        QCOMPARE(archive_write_close(writer.get()), ARCHIVE_OK);
    }
    MainWindow window;
    auto settings = window.m_settings;
    settings.viewer.regionRenderingEnabled = scale == 1.0;
    settings.viewer.clickZoomBehavior = gesture == 3
        ? core::ClickZoomBehavior::HoldToZoomPan : core::ClickZoomBehavior::ToggleOnClick;
    window.applySettings(settings, false);
    window.setFixedSize(800, 600);
    window.show();
    window.openPath(path);
    QTRY_VERIFY(window.m_currentDecoded.ok() && !window.m_imageRenderPending);
    if (scale == 1.0) { window.setActualSize(); }
    else { window.applyCustomZoomWithFallback(scale, QPointF()); }
    QTRY_VERIFY(!window.m_imageRenderPending);
    QVERIFY(window.m_currentDecoded.regionBased);
    const auto before = window.captureViewRenderState();
    const auto button = gesture == 1 ? Qt::MiddleButton : Qt::LeftButton;
    const auto modifiers = gesture == 2 ? Qt::ControlModifier : Qt::NoModifier;
    const QPoint point = window.m_imageView->rect().center();
    QTest::mousePress(window.m_imageView, button, modifiers, point);
    QTRY_VERIFY(!window.m_imageRenderPending);
    const QPointF initialPan = window.m_controller.panOffset();
    const QRect initialRegion = window.m_currentDecoded.sourceRegion;
    const double heldScale = window.m_currentDecoded.effectiveScale;
    bool refreshedDuringDrag = false;
    for (int i = 1; i <= 24; ++i) {
        const QPoint offset(20 * i, 4 * i);
        sendPanMove(window.m_imageView, point + offset, button, modifiers);
        QCOMPARE(window.m_controller.panOffset(), initialPan + offset);
        QTest::qWait(20);
        refreshedDuringDrag |= window.m_currentDecoded.sourceRegion != initialRegion;
    }
    QVERIFY(refreshedDuringDrag);
    QTRY_VERIFY(!window.m_imageRenderPending && !window.m_regionReloadTimer.isActive());
    QCOMPARE(window.m_currentDecoded.effectiveScale, heldScale);
    QCOMPARE(window.m_imageView->panOffset(), initialPan + QPointF(480, 96));
    if (scale > 1.0) {
        const QRectF tile(window.m_imageView->imageRect().topLeft() + window.m_currentDecoded.displayRegionOffset,
            window.m_currentDecoded.image.deviceIndependentSize());
        QVERIFY(tile.contains(QRectF(window.m_imageView->rect())));
    }
    QTest::mouseRelease(window.m_imageView, button, modifiers, point + QPoint(480, 96));
    if (gesture == 3 && scale < core::MaximumZoomFactor) {
        QCOMPARE(window.m_controller.panOffset(), before.panOffset);
        QCOMPARE(window.m_currentDecoded.image, before.decoded.image);
    } else {
        QCOMPARE(window.m_controller.panOffset(), initialPan + QPointF(480, 96));
    }
}

void MainWindowAsyncTests::regionPanPending_data()
{
    QTest::addColumn<int>("scenario");
    QTest::newRow("release-and-final-position") << 0;
    QTest::newRow("release-hold-restores-view") << 1;
    QTest::newRow("source-switch") << 2;
    QTest::newRow("render-error") << 3;
}

void MainWindowAsyncTests::regionPanPending()
{
    QFETCH(int, scenario);
    QTemporaryDir temp;
    const QString path = temp.filePath(QStringLiteral("large.jpg"));
    QImage fixture(1280, 960, QImage::Format_RGB32);
    fixture.fill(Qt::blue);
    QVERIFY(fixture.save(path));
    MainWindow window;
    auto settings = window.m_settings;
    settings.viewer.clickZoomBehavior = scenario == 1
        ? core::ClickZoomBehavior::HoldToZoomPan : core::ClickZoomBehavior::ToggleOnClick;
    window.applySettings(settings, false);
    window.setFixedSize(800, 600);
    window.show();
    window.openPath(path);
    QTRY_VERIFY(window.m_currentDecoded.ok() && !window.m_imageRenderPending);
    window.applyCustomZoomWithFallback(64.0, QPointF());
    QTRY_VERIFY(!window.m_imageRenderPending);
    const auto before = window.captureViewRenderState();
    const QPoint point = window.m_imageView->rect().center();
    QTest::mousePress(window.m_imageView, Qt::LeftButton, Qt::NoModifier, point);
    QTRY_VERIFY(!window.m_imageRenderPending);
    const QPointF initialPan = window.m_controller.panOffset();
    auto request = window.currentDecodeRequest();
    // Fit visible pixels while forcing overscan reduction, including on HiDPI.
    request.maxDecodedBytes = core::estimateRenderMemory(request.viewportSize, 4, request.devicePixelRatio).bytes * 2;
    MainWindow::DecodeJobResult job;
    job.generation = window.m_decodeGeneration;
    job.entry = *window.m_controller.currentEntry();
    job.decoded = core::VipsImageDecoder().decode(job.entry, request);
    QVERIFY2(job.decoded.ok(), qPrintable(job.decoded.errorMessage));
    const auto fallback = window.captureViewRenderState();
    window.m_pendingZoomRenderFallback = MainWindow::PendingZoomRenderFallback { job.generation, fallback };
    window.m_regionRenderPending = window.m_imageRenderPending = true;
    window.m_imageView->setImageInteractionEnabled(false, true);
    window.updateInteractionState();
    QPromise<MainWindow::DecodeJobResult> promise;
    promise.start();
    window.m_decodeWatcher.setFuture(promise.future());
    sendPanMove(window.m_imageView, point + QPoint(1500, 0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(window.m_controller.panOffset(), initialPan + QPointF(1500, 0));
    QCOMPARE(window.m_imageView->panOffset(), window.m_controller.panOffset());
    QTest::qWait(160);
    QVERIFY(window.m_imageRenderPending);
    QVERIFY(window.m_regionReloadTimer.isActive());
    QVERIFY(!window.m_pendingDecodeRequest.has_value());
    QTest::mouseRelease(window.m_imageView, Qt::LeftButton, Qt::NoModifier, point + QPoint(1500, 0));
    if (scenario == 2) {
        QVERIFY(QDir(temp.path()).mkdir(QStringLiteral("empty")));
        window.openFolder(temp.filePath(QStringLiteral("empty")));
    }
    if (scenario == 3) {
        job.decoded = {};
        job.decoded.errorReason = core::DecodeErrorReason::RenderTooLarge;
        job.decoded.errorMessage = QStringLiteral("Too large to render");
    }
    promise.addResult(job);
    promise.finish();
    QTRY_VERIFY(!window.m_imageRenderPending && !window.m_regionReloadTimer.isActive());
    if (scenario == 0) {
        QCOMPARE(window.m_controller.panOffset(), initialPan + QPointF(1500, 0));
        const QRectF tile(window.m_imageView->imageRect().topLeft() + window.m_currentDecoded.displayRegionOffset,
            window.m_currentDecoded.image.deviceIndependentSize());
        QVERIFY(tile.contains(QRectF(window.m_imageView->rect())));
        sendPanMove(window.m_imageView, point + QPoint(1600, 0), Qt::LeftButton, Qt::NoModifier);
        QCOMPARE(window.m_controller.panOffset(), initialPan + QPointF(1500, 0));
    } else if (scenario == 1) {
        QCOMPARE(window.m_controller.panOffset(), before.panOffset);
        QCOMPARE(window.m_currentDecoded.image, before.decoded.image);
        QVERIFY(!window.m_activeHoldZoom.has_value());
    } else if (scenario == 2) {
        QTRY_VERIFY(!window.m_controller.hasCollection());
        QVERIFY(!window.m_imageView->hasImage());
    } else {
        QCOMPARE(window.m_controller.panOffset(), before.panOffset);
        QCOMPARE(window.m_currentDecoded.image, before.decoded.image);
        QVERIFY(window.m_imageView->hasImage());
    }
}

void MainWindowAsyncTests::animatedViewer_data()
{
    QTest::addColumn<QString>("extension");
    QTest::newRow("webp") << QStringLiteral("webp");
    QTest::newRow("jxl") << QStringLiteral("jxl");
}

void MainWindowAsyncTests::animatedHoldZoom_data() { animatedViewer_data(); }

void MainWindowAsyncTests::animatedViewer()
{
    QFETCH(QString, extension);
    QTemporaryDir temp;
    const QString path = temp.filePath((QStringLiteral("animated.") + extension));
    auto first = vips::VImage::black(80, 60, vips::VImage::option()->set("bands", 3))
        .new_from_image({ 255, 0, 0 }).copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB));
    auto frames = first.join(first.new_from_image({ 0, 0, 255 }), VIPS_DIRECTION_VERTICAL);
    frames.set(VIPS_META_PAGE_HEIGHT, 60);
    frames.set("delay", std::vector<int> { 100, 100 });
    frames.set("loop", 0);
    frames.write_to_file(path.toUtf8().constData());
    MainWindow window;
    window.show();
    window.openPath(path);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_animationPlayer->available(), 5000);
    QVERIFY(window.m_animationAction->isEnabled());
    QSignalSpy presented(window.m_animationPlayer, &AnimationPlayer::frameReady);
    QTRY_VERIFY(presented.size() >= 3);
    window.m_animationAction->trigger();
    QVERIFY(!window.m_animationPlayer->playing());
    const int index = window.m_animationPlayer->frameIndex();
    window.rotateRight();
    QTRY_VERIFY(!window.m_imageRenderPending);
    QCOMPARE(window.m_animationPlayer->frameIndex(), index);
    QVERIFY(window.m_currentDecoded.fullDisplaySize.height() > window.m_currentDecoded.fullDisplaySize.width());
    QVERIFY(!window.m_animationPlayer->playing());
    const auto previousView = window.captureViewRenderState();
    window.m_pendingZoomRenderFallback = MainWindow::PendingZoomRenderFallback { window.m_decodeGeneration, previousView };
    window.m_imageRenderPending = true;
    auto oversized = window.currentDecodeRequest();
    oversized.maxDecodedBytes = 1;
    window.m_animationPlayer->setView(oversized);
    QTRY_VERIFY(!window.m_imageRenderPending);
    QVERIFY(!window.m_pendingZoomRenderFallback.has_value());
    QVERIFY(window.m_animationPlayer->available());
    QVERIFY(!window.m_animationPlayer->playing());
    QCOMPARE(window.m_currentDecoded.image, previousView.decoded.image);
    window.hide();
    window.show();
    QVERIFY(!window.m_animationPlayer->playing());
    window.m_animationAction->trigger();
    QTRY_VERIFY(presented.size() >= 5);
    window.copyOriginalImageToClipboard();
    QTRY_VERIFY(QApplication::clipboard()->mimeData() != nullptr && QApplication::clipboard()->mimeData()->hasFormat((QStringLiteral("image/") + extension)));
    QFile original(path);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const auto originalBytes = original.readAll();
    QTRY_COMPARE_WITH_TIMEOUT(QApplication::clipboard()->mimeData()->data((QStringLiteral("image/") + extension)), originalBytes, 5000);
    window.m_animationPlayer->setView(oversized);
    QTRY_VERIFY(!window.m_animationPlayer->available());
    QVERIFY(window.m_imageView->hasImage());
    QVERIFY(!window.m_animationAction->isEnabled());
    window.close();
    QVERIFY(!window.m_animationPlayer->available());
}

static QString writeLoopingWebp(const QTemporaryDir &temp, const QString &extension = QStringLiteral("webp"))
{
    const QString path = temp.filePath(QStringLiteral("animated.") + extension);
    auto image = vips::VImage::black(80, 60, vips::VImage::option()->set("bands", 3))
        .new_from_image({255, 0, 0}).copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB));
    auto frames = image.join(image.new_from_image({0, 0, 255}), VIPS_DIRECTION_VERTICAL);
    frames.set(VIPS_META_PAGE_HEIGHT, 60);
    frames.set("delay", std::vector<int>{100, 100});
    frames.set("loop", 0);
    frames.write_to_file(path.toUtf8().constData());
    return path;
}

void MainWindowAsyncTests::animatedHoldZoom()
{
    QFETCH(QString, extension);
    QTemporaryDir temp;
    const auto path = writeLoopingWebp(temp, extension);
    MainWindow window;
    window.setFixedSize(800, 600);
    window.show();
    window.openPath(path);
    QTRY_VERIFY(window.m_animationPlayer->available() && !window.m_imageRenderPending);
    window.m_animationAction->trigger();
    auto settings = window.m_settings;
    settings.viewer.clickZoomBehavior = core::ClickZoomBehavior::HoldToZoomPan;
    window.applySettings(settings, false);
    window.applyCustomZoomWithFallback(64.0, QPointF());
    QTRY_VERIFY(!window.m_imageRenderPending);
    const auto before = window.captureViewRenderState();
    const auto point = window.m_imageView->rect().center();
    QTest::mousePress(window.m_imageView, Qt::LeftButton, Qt::NoModifier, point);
    QVERIFY(window.m_activeHoldZoom.has_value());
    QTRY_VERIFY(!window.m_imageRenderPending);
    const auto initialPan = window.m_controller.panOffset();
    const auto initialRegion = window.m_currentDecoded.sourceRegion;
    bool refreshedDuringDrag = false;
    for (int i = 1; i <= 24; ++i) {
        sendPanMove(window.m_imageView, point + QPoint(20 * i, 4 * i), Qt::LeftButton, Qt::NoModifier);
        QCOMPARE(window.m_controller.panOffset(), initialPan + QPointF(20 * i, 4 * i));
        QTest::qWait(20);
        refreshedDuringDrag |= window.m_currentDecoded.sourceRegion != initialRegion;
    }
    QVERIFY(refreshedDuringDrag);
    QTest::mouseRelease(window.m_imageView, Qt::LeftButton, Qt::NoModifier, point + QPoint(480, 96));
    QVERIFY(!window.m_activeHoldZoom.has_value());
    QTRY_VERIFY(!window.m_imageRenderPending);
    QCOMPARE(window.m_controller.zoomMode(), before.zoomMode);
    QCOMPARE(window.m_controller.panOffset(), before.panOffset);
    QCOMPARE(window.m_currentDecoded.fullDisplaySize, before.decoded.fullDisplaySize);
    QVERIFY(window.m_zoomInAction->isEnabled());
    QVERIFY(!window.m_animationPlayer->playing());
    window.m_animationAction->trigger();
    QSignalSpy frames(window.m_animationPlayer, &AnimationPlayer::frameReady);
    QTRY_VERIFY(frames.size() >= 2);
    QCOMPARE(window.m_currentDecoded.fullDisplaySize, before.decoded.fullDisplaySize);
}

void MainWindowAsyncTests::animationStopsForFolders()
{
    QTemporaryDir temp;
    const auto path = writeLoopingWebp(temp);
    QVERIFY(QDir(temp.path()).mkdir(QStringLiteral("empty")));
    MainWindow window;
    window.show();
    auto open = [&] { window.openPath(path); };
    open();
    QTRY_VERIFY(window.m_animationPlayer->available() && window.m_sourceRoot != nullptr);
    QTRY_VERIFY(window.folderNodeForPath(QStringLiteral("empty")) != nullptr);
    window.activateFolder(QStringLiteral("empty"), 0, true);
    QVERIFY(!window.m_animationPlayer->available());
    QVERIFY(window.m_animationPlayer->sourceKey().isEmpty());
    QTRY_VERIFY(!window.m_controller.hasCollection() && window.m_loadingFolderPath.isEmpty());
    open();
    QTRY_VERIFY(window.m_animationPlayer->available() && window.folderNodeForPath(QStringLiteral("empty")) != nullptr);
    window.folderNodeForPath(QStringLiteral("empty"))->loaded = true;
    window.activateFolder(QStringLiteral("empty"), 0, true);
    QVERIFY(!window.m_animationPlayer->available());
    QVERIFY(!window.m_animationAction->isEnabled());
    open();
    QTRY_VERIFY(window.m_animationPlayer->available());
    window.openFolder(temp.filePath(QStringLiteral("empty"))); // Dialog's direct entry point.
    QVERIFY(!window.m_animationPlayer->available());
    QVERIFY(window.m_animationPlayer->sourceKey().isEmpty());
}

void MainWindowAsyncTests::staticAnimationFormats_data()
{
    QTest::addColumn<QString>("suffix");
    QTest::addColumn<bool>("large");
    for (const auto suffix : {"gif", "webp", "jxl"}) {
        QTest::newRow(suffix) << QString::fromLatin1(suffix) << false;
    }
    QTest::newRow("large-webp") << QStringLiteral("webp") << true;
}

void MainWindowAsyncTests::staticAnimationFormats()
{
    QFETCH(QString, suffix);
    QFETCH(bool, large);
    QTemporaryDir temp;
    const auto path = temp.filePath(QStringLiteral("still.") + suffix);
    vips::VImage::black(32, 24).write_to_file(path.toUtf8().constData());
    if (large) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.resize(core::AnimationDecodeSession::SourceByteLimit + 1));
    }
    MainWindow window;
    window.show();
    QSignalSpy failures(window.m_animationPlayer, &AnimationPlayer::failed);
    window.openPath(path);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_currentDecoded.ok() && !window.m_imageRenderPending, 10000);
    QVERIFY(!window.m_currentDecoded.animated);
    QVERIFY(window.m_animationPlayer->sourceKey().isEmpty());
    QVERIFY(!window.m_animationAction->isEnabled());
    QCOMPARE(failures.size(), 0);
}

void MainWindowAsyncTests::modernFormatsOpenCopyAndNavigate_data()
{
    QTest::addColumn<QString>("extension");
    QTest::newRow("avif") << QStringLiteral("avif");
    QTest::newRow("jxl") << QStringLiteral("jxl");
    QTest::newRow("jp2") << QStringLiteral("jp2");
    QTest::newRow("heic") << QStringLiteral("heic");
    QTest::newRow("heif") << QStringLiteral("heif");
    QTest::newRow("gif") << QStringLiteral("gif");
    QTest::newRow("tif") << QStringLiteral("tif");
    QTest::newRow("tiff") << QStringLiteral("tiff");
}

void MainWindowAsyncTests::modernFormatsOpenCopyAndNavigate()
{
    QFETCH(QString, extension);
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("a.") + extension);
    try {
        auto *options = vips::VImage::option();
        if (extension != QStringLiteral("gif") && extension != QStringLiteral("tif") && extension != QStringLiteral("tiff")) {
            options->set("lossless", TRUE);
        }
        if (extension != QStringLiteral("jp2") && extension != QStringLiteral("tif") && extension != QStringLiteral("tiff")) {
            options->set("effort", 1);
        }
        vips::VImage::black(80, 60, vips::VImage::option()->set("bands", 3))
            .new_from_image({ 40, 120, 200 })
            .copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB))
            .write_to_file(path.toUtf8().constData(), options);
    } catch (const vips::VError &error) {
        QFAIL(error.what());
    }
    QFile original(path);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const QByteArray bytes = original.readAll();
    QFile broken(temp.filePath(QStringLiteral("b.") + extension));
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write(bytes.left((extension == QStringLiteral("gif") || extension == QStringLiteral("tif") || extension == QStringLiteral("tiff")) ? 6 : bytes.size() / 2));
    broken.close();
    QVERIFY(QFile::copy(path, temp.filePath(QStringLiteral("c.") + extension)));
    MainWindow window;
    window.resize(800, 600);
    window.show();
    window.openPath(path);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_currentDecoded.ok() && !window.m_imageRenderPending
            && window.m_controller.count() == 3, 5000);
    window.rotateRight();
    QTRY_VERIFY_WITH_TIMEOUT(!window.m_imageRenderPending, 5000);
    QVERIFY(window.m_currentDecoded.ok());
    QCOMPARE(window.m_controller.rotationDegrees(), 90);
    if (extension == QStringLiteral("jxl")) {
        window.setActualSize();
        QTRY_VERIFY_WITH_TIMEOUT(!window.m_imageRenderPending, 5000);
        QCOMPARE(window.m_currentDecoded.effectiveScale, 1.0);
        window.applyCustomZoomWithFallback(32.0, QPointF());
        QTRY_VERIFY_WITH_TIMEOUT(!window.m_imageRenderPending, 5000);
        QVERIFY(window.m_currentDecoded.ok());
        QVERIFY(window.m_currentDecoded.regionBased);
        const QRect previousRegion = window.m_currentDecoded.sourceRegion;
        window.m_controller.setPanOffset(QPointF(200, -100));
        window.m_imageView->setPanOffset(window.m_controller.panOffset());
        window.scheduleRegionReload();
        QTRY_VERIFY_WITH_TIMEOUT(!window.m_imageRenderPending && window.m_currentDecoded.sourceRegion != previousRegion, 5000);
        window.setFitToWindow();
        QTRY_VERIFY_WITH_TIMEOUT(!window.m_imageRenderPending, 5000);
        QVERIFY(window.m_currentDecoded.ok());
    }
    QApplication::clipboard()->clear();
    window.copyOriginalImageToClipboard();
    const QString mimeType = extension == QStringLiteral("tif") ? QStringLiteral("image/tiff")
                                                               : QStringLiteral("image/") + extension;
    QTRY_VERIFY_WITH_TIMEOUT(QApplication::clipboard()->mimeData() != nullptr
            && QApplication::clipboard()->mimeData()->hasFormat(mimeType), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(QApplication::clipboard()->mimeData()->data(mimeType), bytes, 5000);
    window.showNextImage();
    QTRY_VERIFY_WITH_TIMEOUT(window.m_controller.currentIndex() == 1 && !window.m_imageRenderPending, 5000);
    QVERIFY(!window.m_currentDecoded.ok());
    QVERIFY(window.m_nextAction->isEnabled());
    QVERIFY(window.m_previousAction->isEnabled());
    window.showNextImage();
    QTRY_VERIFY_WITH_TIMEOUT(window.m_controller.currentIndex() == 2 && window.m_currentDecoded.ok()
            && !window.m_imageRenderPending, 5000);
    QVERIFY(window.m_imageView->hasImage());
    QApplication::clipboard()->clear();
}

void MainWindowAsyncTests::aboutUsesApplicationVersionAndSharedAction()
{
    MainWindow window;
    QVERIFY(window.m_aboutAction != nullptr);
    QVERIFY(window.m_appMenuButton->menu()->actions().contains(window.m_aboutAction));
    QVERIFY(window.m_fullscreenAppMenuButton->menu()->actions().contains(window.m_aboutAction));
    bool inMenuBar = false;
    for (QAction *action : window.menuBar()->actions()) {
        if (action->menu() && action->menu()->actions().contains(window.m_aboutAction)) {
            inMenuBar = true;
        }
    }
    QVERIFY(inMenuBar);
    AboutDialog dialog;
    QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("aboutVersion"))->text(),
        QStringLiteral("Insidious ") + QCoreApplication::applicationVersion());
    QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("aboutLicense"))->text().contains(QStringLiteral("MIT")));
    QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("aboutProjectLink"))->text().contains(QStringLiteral("https://github.com/0xf836/insidious")));
    bool timedOut = false;
    const bool opened = triggerAboutAndCloseModal(window.m_aboutAction, &timedOut);
    QVERIFY(!timedOut);
    QVERIFY(opened);
}

void MainWindowAsyncTests::wrongModalTypeIsClosed_data()
{
    QTest::addColumn<bool>("messageBox");
    QTest::newRow("generic-dialog") << false;
    QTest::newRow("message-box") << true;
}

void MainWindowAsyncTests::wrongModalTypeIsClosed()
{
    QFETCH(bool, messageBox);
    QAction action;
    connect(&action, &QAction::triggered, this, [messageBox]() {
        if (messageBox) {
            QMessageBox dialog;
            dialog.exec();
        } else {
            QDialog dialog;
            dialog.exec();
        }
    });
    bool timedOut = false;
    QVERIFY(!triggerAboutAndCloseModal(&action, &timedOut));
    QVERIFY(!timedOut);
}

void MainWindowAsyncTests::aboutLinkWorksFromKeyboard()
{
    // RAII unregisters the handler even if a QVERIFY returns early.
    struct UrlHandlerReset {
        ~UrlHandlerReset() { QDesktopServices::unsetUrlHandler("https"); }
    } reset;
    m_openedUrls.clear();
    QDesktopServices::setUrlHandler("https", this, "captureUrl");
    AboutDialog dialog;
    auto *link = dialog.findChild<QLabel *>(QStringLiteral("aboutProjectLink"));
    auto *version = dialog.findChild<QLabel *>(QStringLiteral("aboutVersion"));
    auto *buttons = dialog.findChild<QDialogButtonBox *>();
    QVERIFY(link != nullptr);
    QVERIFY(version != nullptr);
    QVERIFY(buttons != nullptr);
    QVERIFY(link->textInteractionFlags().testFlag(Qt::LinksAccessibleByKeyboard));
    QVERIFY(version->textInteractionFlags().testFlag(Qt::TextSelectableByKeyboard));
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    dialog.activateWindow();
    QTRY_VERIFY(dialog.isActiveWindow());
    QVERIFY(m_openedUrls.isEmpty());
    // Start at Close, then traverse the actual tab chain to the link.
    buttons->button(QDialogButtonBox::Close)->setFocus();
    for (int i = 0; i < 6 && !(link->hasFocus() && link->hasSelectedText()); ++i) {
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
    }
    QVERIFY(link->hasFocus());
    QVERIFY(link->hasSelectedText());
    QTest::keyClick(link, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(m_openedUrls.size(), 1, 1000);
    QCOMPARE(m_openedUrls.first(), QUrl(QStringLiteral("https://github.com/0xf836/insidious")));
    version->setFocus(Qt::TabFocusReason);
    QTest::keyClick(version, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(version->selectedText(), version->text());
}

void MainWindowAsyncTests::decodeCompletion_data()
{
    QTest::addColumn<int>("scenario");
    QTest::newRow("current-success") << 0;
    QTest::newRow("image-switch") << 1;
    QTest::newRow("source-switch") << 2;
    QTest::newRow("cancelled") << 3;
    QTest::newRow("stale-error") << 4;
}

void MainWindowAsyncTests::decodeCompletion()
{
    QFETCH(int, scenario);
    MainWindow window;
    core::ImageEntry entry;
    entry.absolutePath = QStringLiteral("/test/current.png");
    entry.displayName = QStringLiteral("current.png");
    window.m_controller.setCollection({ entry }, 0);
    core::DecodedImage initial;
    initial.image = QImage(4, 4, QImage::Format_RGB32);
    initial.image.fill(Qt::green);
    initial.sourceSize = initial.displaySize = initial.fullDisplaySize = QSize(4, 4);
    window.displayDecodedImage(initial);
    window.m_decodeGeneration = 10;
    window.m_imageRenderPending = true;
    MainWindow::DecodeJobResult job;
    job.generation = 10;
    job.entry = entry;
    job.cacheKey = QStringLiteral("completed-job");
    job.decoded = initial;
    job.decoded.image.fill(Qt::blue);
    QPromise<MainWindow::DecodeJobResult> promise;
    promise.start();
    QSignalSpy finished(&window.m_decodeWatcher, &QFutureWatcherBase::finished);
    window.m_decodeWatcher.setFuture(promise.future());
    // Complete explicitly after the state transition; no sleep/decode race is needed.
    if (scenario == 1 || scenario == 2 || scenario == 4) {
        ++window.m_decodeGeneration;
        entry.absolutePath = QStringLiteral("/test/new.png");
        window.m_controller.setCollection({ entry }, 0);
    }
    if (scenario == 2) {
        ++window.m_sourceGeneration;
        window.m_cache->clear();
    }
    if (scenario == 4) {
        job.decoded.errorMessage = QStringLiteral("old decode failure");
    }
    if (scenario == 3) {
        window.m_decodeWatcher.cancel();
    } else {
        promise.addResult(job);
    }
    promise.finish();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(window.m_currentDecoded.image.pixelColor(0, 0), scenario == 0 ? QColor(Qt::blue) : QColor(Qt::green));
    QCOMPARE(window.m_cache->contains(job.cacheKey), scenario == 0);
    QVERIFY(!window.m_imageRenderPending);
    QVERIFY(window.m_imageView->hasImage());
    QCOMPARE(window.m_controller.currentEntry()->absolutePath, entry.absolutePath);
}

void MainWindowAsyncTests::staleScanDoesNotReplaceSource_data()
{
    QTest::addColumn<bool>("stale");
    QTest::newRow("current") << false;
    QTest::newRow("stale") << true;
}

void MainWindowAsyncTests::staleScanDoesNotReplaceSource()
{
    QFETCH(bool, stale);
    MainWindow window;
    const auto currentRoot = std::make_shared<core::CollectionFolderNode>();
    window.m_sourceRoot = currentRoot;
    window.m_sourceRootPath = QStringLiteral("current-source");
    window.m_fileTreeModel->setRoot(currentRoot);
    window.m_sourceGeneration = 1;
    MainWindow::ScanJobResult job;
    job.generation = 1;
    job.result.root = std::make_shared<core::CollectionFolderNode>();
    job.result.rootPath = QStringLiteral("completed-source");
    job.result.root->loaded = true;
    QPromise<MainWindow::ScanJobResult> promise;
    promise.start();
    QSignalSpy finished(&window.m_scanWatcher, &QFutureWatcherBase::finished);
    window.m_scanWatcher.setFuture(promise.future());
    if (stale) {
        ++window.m_sourceGeneration;
    }
    promise.addResult(job);
    promise.finish();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(window.m_sourceRoot, stale ? currentRoot : job.result.root);
    QCOMPARE(window.m_sourceRootPath, stale ? QStringLiteral("current-source") : job.result.rootPath);
}

void MainWindowAsyncTests::archiveScanErrorsDoNotPrompt_data()
{
    QTest::addColumn<bool>("hasCollection");
    QTest::addColumn<bool>("unsupported");
    QTest::newRow("unsupported-empty") << false << true;
    QTest::newRow("unsupported-current") << true << true;
    QTest::newRow("corrupt-empty") << false << false;
    QTest::newRow("corrupt-current") << true << false;
}

void MainWindowAsyncTests::archiveScanErrorsDoNotPrompt()
{
    QFETCH(bool, hasCollection);
    QFETCH(bool, unsupported);
    MainWindow window;
    core::ImageEntry entry;
    entry.absolutePath = QStringLiteral("current.png");
    if (hasCollection) {
        window.m_controller.setCollection({ entry }, 0);
        core::DecodedImage decoded;
        decoded.image = QImage(3, 2, QImage::Format_RGB32);
        decoded.image.fill(Qt::green);
        decoded.sourceSize = decoded.image.size();
        window.m_currentDecoded = decoded;
        window.m_imageView->setImage(decoded.image);
    }
    MainWindow::ScanJobResult job;
    job.generation = window.m_sourceGeneration;
    const QString path = QFINDTESTDATA("fixtures/encrypted-data-rar5.rar");
    QVERIFY(!path.isEmpty());
    job.selectedPaths = { path };
    job.result.errorMessage = unsupported ? QStringLiteral("Archive encryption is not supported.") : QStringLiteral("ZIP bad CRC");
    job.result.errorReason = unsupported ? core::ScanErrorReason::UnsupportedEncryption : core::ScanErrorReason::Generic;
    window.rememberArchivePassword(job.selectedPaths.first(), QStringLiteral("secret"));

    bool prompted = false;
    QTimer closer;
    connect(&closer, &QTimer::timeout, &closer, [&]() {
        if (auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
            prompted = true;
            dialog->reject();
        }
    });
    closer.start(0);
    window.applyScanResult(job);
    closer.stop();
    QVERIFY(!prompted);
    QCOMPARE(window.m_statusLabel->text(), job.result.errorMessage);
    QCOMPARE(window.m_controller.hasCollection(), hasCollection);
    QCOMPARE(window.m_imageView->hasImage(), hasCollection);
    if (hasCollection) {
        QCOMPARE(window.m_controller.currentEntry()->absolutePath, entry.absolutePath);
        QCOMPARE(window.m_currentDecoded.image.pixelColor(0, 0), QColor(Qt::green));
    } else {
        QVERIFY(window.m_archivePasswords.isEmpty());
    }
}

void MainWindowAsyncTests::staleFolderLoadDoesNotReplaceBranch_data()
{
    staleScanDoesNotReplaceSource_data();
}

void MainWindowAsyncTests::staleFolderLoadDoesNotReplaceBranch()
{
    QFETCH(bool, stale);
    MainWindow window;
    const auto root = std::make_shared<core::CollectionFolderNode>();
    window.m_sourceRoot = root;
    window.m_fileTreeModel->setRoot(root);
    window.m_loadingFolderPath = QStringLiteral("pending-folder");
    window.m_activeFolderPath = QStringLiteral("another-folder");
    window.m_sourceGeneration = 1;
    MainWindow::FolderLoadJobResult job;
    job.generation = 1;
    job.result.images.append(core::ImageEntry {});
    QPromise<MainWindow::FolderLoadJobResult> promise;
    promise.start();
    QSignalSpy finished(&window.m_folderLoadWatcher, &QFutureWatcherBase::finished);
    window.m_folderLoadWatcher.setFuture(promise.future());
    if (stale) {
        ++window.m_sourceGeneration;
    }
    promise.addResult(job);
    promise.finish();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(root->images.size(), stale ? 0 : 1);
    QCOMPARE(root->loaded, !stale);
    QCOMPARE(window.m_loadingFolderPath, QStringLiteral("pending-folder"));
}

} // namespace insidious::ui

QTEST_MAIN(insidious::ui::MainWindowAsyncTests)
#include "main_window_async_tests.moc"
