#include "ui/main_window.h"

#include "core/archive_collection_source.h"
#include "core/image_entry_validation.h"
#include "core/image_formats.h"
#include "core/vips_image_decoder.h"
#include "ui/quick_jump_dialog.h"
#include "ui/settings_dialog.h"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFont>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QInputDialog>
#include <QListView>
#include <QLineEdit>
#include <QLocale>
#include <QMenuBar>
#include <QMimeData>
#include <QScopedValueRollback>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace insidious::ui {
namespace {

QString formattedFileSize(qint64 bytes)
{
    return QLocale::system().formattedDataSize(bytes);
}

QPointF clampedPointToRect(const QPointF &point, const QRectF &rect)
{
    return QPointF(
        std::clamp(point.x(), rect.left(), rect.right()),
        std::clamp(point.y(), rect.top(), rect.bottom()));
}

void appendFoldersInPreorder(const core::CollectionFolderNode *node, QVector<const core::CollectionFolderNode *> *folders)
{
    if (node == nullptr || folders == nullptr) {
        return;
    }

    folders->append(node);
    for (const auto &child : node->children) {
        appendFoldersInPreorder(child.get(), folders);
    }
}

bool isNavigableFolder(const core::CollectionFolderNode *node)
{
    return node != nullptr
        && (!node->images.isEmpty() || (node->canLoadChildren && !node->loaded));
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_cache(std::make_shared<core::MemoryImageCache>())
{
    setWindowTitle(tr("Insidious"));
    setAcceptDrops(true);

    m_imageView = new ImageViewWidget(this);
    m_imageView->setMessage(tr("Open an image or folder."));
    setCentralWidget(m_imageView);

    m_statusLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusLabel, 1);

    setupFileListPanel();
    setupInfoPanel();
    setupActions();
    setupFullscreenOverlayPanels();
    setupConnections();
    refreshFileList();
    updateInfoPanelForCurrentImage();
    loadSettings();

    m_resizeReloadTimer.setSingleShot(true);
    m_resizeReloadTimer.setInterval(120);
    connect(&m_resizeReloadTimer, &QTimer::timeout, this, [this]() {
        loadCurrentImage();
    });

    resize(1100, 760);
    restoreLayoutSettings();
    updateStatus();
    updateActionState();
    updateInteractionState();
}

void MainWindow::openPath(const QString &path)
{
    const QFileInfo info(path);
    if (info.isDir()) {
        openFolder(info.absoluteFilePath());
    } else if (core::isSupportedArchiveFile(info)) {
        openArchive(info.absoluteFilePath());
    } else {
        openFile(info.absoluteFilePath());
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (mimeData == nullptr || !mimeData->hasUrls() || mimeData->urls().size() != 1) {
        return;
    }

    const QFileInfo info(mimeData->urls().first().toLocalFile());
    if (info.isDir() || core::isSupportedImageFile(info) || core::isSupportedArchiveFile(info)) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.size() != 1) {
        showError(tr("Drop one image file or one folder."));
        return;
    }

    openPath(urls.first().toLocalFile());
    event->acceptProposedAction();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched)

    const bool relevantEvent = event->type() == QEvent::MouseMove
        || event->type() == QEvent::Enter
        || event->type() == QEvent::Leave;
    if (m_fullscreenUiActive && relevantEvent) {
        updateFullscreenOverlayVisibility();
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateFullscreenOverlayGeometry();
    updateFullscreenOverlayVisibility();
    scheduleFitReload();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        exitFullscreenMode();
        event->accept();
        return;
    }

    QMainWindow::keyPressEvent(event);
}

void MainWindow::openFileDialog()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open image or archive"),
        QString(),
        imageDialogFilter(),
        nullptr,
        QFileDialog::DontUseNativeDialog);
    if (!filePath.isEmpty()) {
        openPath(filePath);
    }
}

void MainWindow::openFolderDialog()
{
    const QString folderPath = QFileDialog::getExistingDirectory(
        this,
        tr("Open folder"),
        QString(),
        QFileDialog::DontUseNativeDialog | QFileDialog::ShowDirsOnly);
    if (!folderPath.isEmpty()) {
        openFolder(folderPath);
    }
}

void MainWindow::loadCurrentImage()
{
    loadCurrentImage(true);
}

void MainWindow::loadCurrentImage(bool showTransientLoading)
{
    loadCurrentImage(showTransientLoading, std::nullopt);
}

void MainWindow::loadCurrentImage(bool showTransientLoading, std::optional<ViewRenderState> zoomFallback)
{
    if (!zoomFallback.has_value()) {
        m_activeHoldZoom.reset();
    }

    const core::ImageEntry *entry = m_controller.currentEntry();
    if (entry == nullptr) {
        m_imageRenderPending = false;
        m_pendingZoomRenderFallback.reset();
        updateInteractionState();
        updateActionState();
        updateInfoPanelForCurrentImage();
        return;
    }

    const core::EntryValidationResult validation = core::validateImageEntryLoadable(*entry);
    if (!validation.ok()) {
        ++m_decodeGeneration;
        m_imageRenderPending = false;
        m_pendingZoomRenderFallback.reset();
        if (m_decodeWatcher.isRunning()) {
            m_decodeWatcher.cancel();
        }
        showImageLoadError(*entry, validation.errorMessage);
        return;
    }

    const core::DecodeRequest request = currentDecodeRequest();
    const QString cacheKey = currentCacheKey(request);

    core::DecodedImage cached;
    if (m_cache->tryGet(cacheKey, &cached)) {
        m_imageRenderPending = false;
        m_pendingZoomRenderFallback.reset();
        displayDecodedImage(cached);
        prefetchNeighbors(request);
        return;
    }

    const quint64 generation = ++m_decodeGeneration;
    if (zoomFallback.has_value()) {
        m_pendingZoomRenderFallback = PendingZoomRenderFallback { generation, *zoomFallback };
    } else {
        m_pendingZoomRenderFallback.reset();
    }
    const bool hasDisplayedImage = m_imageView->hasImage();
    if (!zoomFallback.has_value()) {
        m_currentDecoded = {};
    }
    m_imageRenderPending = true;
    updateInfoPanelForCurrentImage(tr("Loading..."));
    const bool keepHoldZoomInteraction = zoomFallback.has_value() && m_activeHoldZoom.has_value();
    if (!keepHoldZoomInteraction) {
        m_imageView->setImageInteractionEnabled(false);
    }
    if (hasDisplayedImage && showTransientLoading) {
        m_imageView->setOverlayMessage(tr("Loading image..."));
        updateStatus(tr("Loading image..."));
    } else if (hasDisplayedImage) {
        m_imageView->setOverlayMessage(QString());
    } else if (!hasDisplayedImage) {
        m_imageView->setMessage(tr("Loading image..."));
        updateStatus(tr("Loading image..."));
    }
    updateActionState();
    updateInteractionState();

    if (m_decodeWatcher.isRunning()) {
        m_decodeWatcher.cancel();
    }

    const core::ImageEntry entryCopy = *entry;
    const QString archivePassword = entryCopy.source == core::ImageEntrySource::Archive
        && entryCopy.archivePath == m_currentArchivePath
        ? m_currentArchivePassword
        : QString();
    m_decodeWatcher.setFuture(QtConcurrent::run([entryCopy, request, generation, cacheKey, archivePassword]() {
        core::VipsImageDecoder decoder;
        DecodeJobResult job;
        job.generation = generation;
        job.cacheKey = cacheKey;
        job.entry = entryCopy;
        job.decoded = decoder.decode(entryCopy, request, archivePassword);
        return job;
    }));
}

void MainWindow::showNextImage()
{
    if (!m_controller.hasCollection() && m_sourceRoot == nullptr) {
        return;
    }

    if (m_controller.goNext()) {
        reloadAfterImageSwitch();
        return;
    }
    if (navigateToAdjacentFolder(true)) {
        return;
    }
    if (!m_controller.hasCollection()) {
        updateActionState();
        return;
    }
    reloadAfterImageSwitch();
}

void MainWindow::showPreviousImage()
{
    if (!m_controller.hasCollection() && m_sourceRoot == nullptr) {
        return;
    }

    if (m_controller.goPrevious()) {
        reloadAfterImageSwitch();
        return;
    }
    if (navigateToAdjacentFolder(false)) {
        return;
    }
    if (!m_controller.hasCollection()) {
        updateActionState();
        return;
    }
    reloadAfterImageSwitch();
}

void MainWindow::showFirstImage()
{
    if (m_controller.goFirst()) {
        reloadAfterImageSwitch();
    }
}

void MainWindow::showLastImage()
{
    if (m_controller.goLast()) {
        reloadAfterImageSwitch();
    }
}

void MainWindow::zoomIn()
{
    zoomInAt(QPointF(m_imageView->width() / 2.0, m_imageView->height() / 2.0));
}

void MainWindow::zoomOut()
{
    zoomOutAt(QPointF(m_imageView->width() / 2.0, m_imageView->height() / 2.0));
}

void MainWindow::setFitToWindow()
{
    m_temporaryZoomReturnMode.reset();
    m_baseZoomMode = core::ZoomMode::FitToWindow;
    m_controller.setFitToWindow();
    updateInteractionState();
    loadCurrentImage(false);
}

void MainWindow::setActualSize()
{
    m_temporaryZoomReturnMode.reset();
    m_baseZoomMode = core::ZoomMode::ActualSize;
    m_controller.setActualSize();
    updateInteractionState();
    loadCurrentImage(false);
}

void MainWindow::handleImageClick(const QPoint &position)
{
    if (m_imageRenderPending
        || m_activeHoldZoom.has_value()
        || !m_controller.hasCollection()
        || !m_currentDecoded.ok()
        || m_currentDecoded.effectiveScale <= 0.0) {
        return;
    }

    if (m_controller.zoomMode() == core::ZoomMode::Custom && restoreTemporaryZoomReturnMode()) {
        return;
    }

    rememberTemporaryZoomReturnMode();

    double newScale = 1.0;
    QPointF newPanOffset;
    if (customZoomForAnchor(QPointF(position), m_clickZoomMultiplier, &newScale, &newPanOffset)) {
        applyCustomZoomWithFallback(newScale, newPanOffset);
    }
}

void MainWindow::zoomToRect(const QRect &selectionRect)
{
    if (m_imageRenderPending
        || !m_controller.hasCollection()
        || !m_currentDecoded.ok()
        || m_currentDecoded.effectiveScale <= 0.0) {
        return;
    }

    const QRectF imageRect = m_imageView->imageRect();
    const QRectF selected = QRectF(selectionRect).intersected(imageRect);
    if (selected.width() < 8.0 || selected.height() < 8.0) {
        return;
    }

    rememberTemporaryZoomReturnMode();

    const double currentScale = m_currentDecoded.effectiveScale;
    const double scaleMultiplier = std::min(
        static_cast<double>(m_imageView->width()) / selected.width(),
        static_cast<double>(m_imageView->height()) / selected.height());
    const double newScale = std::clamp(currentScale * scaleMultiplier, core::MinimumZoomFactor, core::MaximumZoomFactor);

    const QPointF selectedCenterInImage = selected.center() - imageRect.topLeft();
    const QPointF sourceCenter(selectedCenterInImage.x() / currentScale, selectedCenterInImage.y() / currentScale);
    const QSizeF newDisplaySize(
        m_currentDecoded.image.width() * newScale / currentScale,
        m_currentDecoded.image.height() * newScale / currentScale);
    const QPointF centeredTopLeft(
        (m_imageView->width() - newDisplaySize.width()) / 2.0,
        (m_imageView->height() - newDisplaySize.height()) / 2.0);
    const QPointF viewportCenter(m_imageView->width() / 2.0, m_imageView->height() / 2.0);
    const QPointF selectedCenterAtNewScale(sourceCenter.x() * newScale, sourceCenter.y() * newScale);
    const QPointF newPanOffset = viewportCenter - centeredTopLeft - selectedCenterAtNewScale;

    applyCustomZoomWithFallback(newScale, newPanOffset);
}

void MainWindow::rotateLeft()
{
    m_controller.rotateLeft();
    m_imageView->resetPan();
    loadCurrentImage();
}

void MainWindow::rotateRight()
{
    m_controller.rotateRight();
    m_imageView->resetPan();
    loadCurrentImage();
}

void MainWindow::setExifAutorotateEnabled(bool enabled)
{
    if (m_settings.viewer.exifAutorotate == enabled) {
        return;
    }

    core::AppSettings settings = m_settings;
    settings.viewer.exifAutorotate = enabled;
    applySettings(settings, true);
    saveSettings();
}

void MainWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        exitFullscreenMode();
    } else {
        enterFullscreenMode();
    }
}

void MainWindow::showQuickJumpDialog()
{
    if (!m_controller.hasCollection() || m_quickJumpDialogOpen) {
        return;
    }

    const QScopedValueRollback<bool> guard(m_quickJumpDialogOpen, true);
    hideFullscreenOverlayPanels();
    int selectedRow = -1;

    if (!isFullScreen()) {
        QuickJumpDialog dialog(m_controller.entries(), this);
        if (dialog.exec() == QDialog::Accepted) {
            selectedRow = dialog.selectedSourceRow();
        }
    } else {
        auto *overlay = new QWidget(this);
        overlay->setObjectName(QStringLiteral("quickJumpOverlay"));
        overlay->setAttribute(Qt::WA_DeleteOnClose);
        overlay->setAttribute(Qt::WA_StyledBackground);
        overlay->setStyleSheet(QStringLiteral("#quickJumpOverlay { background-color: rgba(0, 0, 0, 96); }"));
        overlay->setGeometry(rect());

        auto *layout = new QVBoxLayout(overlay);
        layout->setContentsMargins(24, 24, 24, 24);
        layout->setAlignment(Qt::AlignCenter);

        auto *dialog = new QuickJumpDialog(m_controller.entries(), overlay);
        dialog->setWindowFlags(Qt::Widget);
        dialog->setStyleSheet(QStringLiteral("QuickJumpDialog { background: palette(window); border: 1px solid palette(mid); }"));
        dialog->setMinimumSize(360, 260);
        dialog->resize(520, 360);
        layout->addWidget(dialog);

        QEventLoop loop;
        int dialogResult = QDialog::Rejected;
        connect(dialog, &QDialog::accepted, &loop, [&]() {
            dialogResult = QDialog::Accepted;
            loop.quit();
        });
        connect(dialog, &QDialog::rejected, &loop, [&]() {
            dialogResult = QDialog::Rejected;
            loop.quit();
        });

        overlay->show();
        overlay->raise();
        dialog->show();
        dialog->focusSearch();
        loop.exec(QEventLoop::DialogExec);

        if (dialogResult == QDialog::Accepted) {
            selectedRow = dialog->selectedSourceRow();
        }
        overlay->close();
    }

    if (selectedRow < 0) {
        return;
    }

    if (m_controller.setCurrentIndex(selectedRow)) {
        reloadAfterImageSwitch();
    }
}

void MainWindow::showSettingsDialog()
{
    SettingsDialog dialog(this);
    dialog.setSettings(m_settings);
    connect(&dialog, &SettingsDialog::settingsApplied, this, [this](const core::AppSettings &settings) {
        applySettings(settings, true);
        saveSettings();
    });
    dialog.exec();
}

void MainWindow::handleWheelAction(Qt::KeyboardModifiers modifiers, int angleDeltaY, QPoint position)
{
    applyWheelAction(wheelActionForModifiers(modifiers), angleDeltaY, position);
}

void MainWindow::setupActions()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    auto *navigateMenu = menuBar()->addMenu(tr("&Navigate"));
    m_mainToolBar = addToolBar(tr("Main"));
    m_mainToolBar->setMovable(false);

    m_openFileAction = new QAction(tr("Open File"), this);
    m_openFileAction->setShortcut(QKeySequence::Open);
    connect(m_openFileAction, &QAction::triggered, this, &MainWindow::openFileDialog);
    fileMenu->addAction(m_openFileAction);
    m_mainToolBar->addAction(m_openFileAction);

    m_openFolderAction = new QAction(tr("Open Folder"), this);
    m_openFolderAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(m_openFolderAction, &QAction::triggered, this, &MainWindow::openFolderDialog);
    fileMenu->addAction(m_openFolderAction);
    m_mainToolBar->addAction(m_openFolderAction);

    fileMenu->addSeparator();
    auto *quitAction = new QAction(tr("Quit"), this);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
    fileMenu->addAction(quitAction);

    m_mainToolBar->addSeparator();

    m_previousAction = new QAction(tr("Previous"), this);
    m_previousAction->setShortcuts(QList<QKeySequence> { QKeySequence(Qt::Key_Left), QKeySequence(Qt::Key_PageUp) });
    connect(m_previousAction, &QAction::triggered, this, &MainWindow::showPreviousImage);
    navigateMenu->addAction(m_previousAction);
    m_mainToolBar->addAction(m_previousAction);

    m_nextAction = new QAction(tr("Next"), this);
    m_nextAction->setShortcuts(QList<QKeySequence> { QKeySequence(Qt::Key_Right), QKeySequence(Qt::Key_PageDown) });
    connect(m_nextAction, &QAction::triggered, this, &MainWindow::showNextImage);
    navigateMenu->addAction(m_nextAction);
    m_mainToolBar->addAction(m_nextAction);

    m_firstAction = new QAction(tr("First"), this);
    m_firstAction->setShortcut(QKeySequence(Qt::Key_Home));
    connect(m_firstAction, &QAction::triggered, this, &MainWindow::showFirstImage);
    navigateMenu->addAction(m_firstAction);

    m_lastAction = new QAction(tr("Last"), this);
    m_lastAction->setShortcut(QKeySequence(Qt::Key_End));
    connect(m_lastAction, &QAction::triggered, this, &MainWindow::showLastImage);
    navigateMenu->addAction(m_lastAction);

    m_quickJumpAction = new QAction(tr("Quick Jump"), this);
    m_quickJumpAction->setShortcut(QKeySequence::Find);
    m_quickJumpAction->setShortcutContext(Qt::WindowShortcut);
    connect(m_quickJumpAction, &QAction::triggered, this, &MainWindow::showQuickJumpDialog);
    navigateMenu->addAction(m_quickJumpAction);

    m_preferencesAction = new QAction(tr("Preferences"), this);
    m_preferencesAction->setShortcut(QKeySequence::Preferences);
    connect(m_preferencesAction, &QAction::triggered, this, &MainWindow::showSettingsDialog);
    editMenu->addAction(m_preferencesAction);

    m_mainToolBar->addSeparator();

    m_zoomInAction = new QAction(tr("Zoom In"), this);
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(m_zoomInAction, &QAction::triggered, this, &MainWindow::zoomIn);
    viewMenu->addAction(m_zoomInAction);
    m_mainToolBar->addAction(m_zoomInAction);

    m_zoomOutAction = new QAction(tr("Zoom Out"), this);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(m_zoomOutAction, &QAction::triggered, this, &MainWindow::zoomOut);
    viewMenu->addAction(m_zoomOutAction);
    m_mainToolBar->addAction(m_zoomOutAction);

    m_fitAction = new QAction(tr("Fit"), this);
    m_fitAction->setShortcut(QKeySequence(Qt::Key_0));
    connect(m_fitAction, &QAction::triggered, this, &MainWindow::setFitToWindow);
    viewMenu->addAction(m_fitAction);
    m_mainToolBar->addAction(m_fitAction);

    m_actualSizeAction = new QAction(tr("Actual Size"), this);
    m_actualSizeAction->setShortcut(QKeySequence(Qt::Key_1));
    connect(m_actualSizeAction, &QAction::triggered, this, &MainWindow::setActualSize);
    viewMenu->addAction(m_actualSizeAction);
    m_mainToolBar->addAction(m_actualSizeAction);

    m_rotateLeftAction = new QAction(tr("Rotate Left"), this);
    m_rotateLeftAction->setShortcut(QKeySequence(Qt::Key_BracketLeft));
    connect(m_rotateLeftAction, &QAction::triggered, this, &MainWindow::rotateLeft);
    viewMenu->addAction(m_rotateLeftAction);

    m_rotateRightAction = new QAction(tr("Rotate Right"), this);
    m_rotateRightAction->setShortcut(QKeySequence(Qt::Key_BracketRight));
    connect(m_rotateRightAction, &QAction::triggered, this, &MainWindow::rotateRight);
    viewMenu->addAction(m_rotateRightAction);

    m_exifAutorotateAction = new QAction(tr("Use EXIF Orientation"), this);
    m_exifAutorotateAction->setCheckable(true);
    connect(m_exifAutorotateAction, &QAction::toggled, this, &MainWindow::setExifAutorotateEnabled);
    viewMenu->addAction(m_exifAutorotateAction);
    m_mainToolBar->addAction(m_exifAutorotateAction);

    m_fullscreenAction = new QAction(tr("Fullscreen"), this);
    m_fullscreenAction->setShortcut(QKeySequence(Qt::Key_F11));
    connect(m_fullscreenAction, &QAction::triggered, this, &MainWindow::toggleFullscreen);
    viewMenu->addAction(m_fullscreenAction);
    m_mainToolBar->addAction(m_fullscreenAction);

    viewMenu->addSeparator();
    QAction *fileListAction = m_fileListDock->toggleViewAction();
    fileListAction->setText(tr("Files"));
    viewMenu->addAction(fileListAction);

    QAction *infoAction = m_infoDock->toggleViewAction();
    infoAction->setText(tr("Info"));
    viewMenu->addAction(infoAction);
}

void MainWindow::setupConnections()
{
    connect(m_imageView, &ImageViewWidget::navigatePreviousRequested, this, &MainWindow::showPreviousImage);
    connect(m_imageView, &ImageViewWidget::navigateNextRequested, this, &MainWindow::showNextImage);
    connect(m_imageView, &ImageViewWidget::zoomInRequested, this, &MainWindow::zoomIn);
    connect(m_imageView, &ImageViewWidget::zoomOutRequested, this, &MainWindow::zoomOut);
    connect(m_imageView, &ImageViewWidget::wheelActionRequested, this, &MainWindow::handleWheelAction);
    connect(m_imageView, &ImageViewWidget::imageClicked, this, &MainWindow::handleImageClick);
    connect(m_imageView, &ImageViewWidget::holdZoomStarted, this, &MainWindow::handleHoldZoomStarted);
    connect(m_imageView, &ImageViewWidget::holdZoomMoved, this, &MainWindow::handleHoldZoomMoved);
    connect(m_imageView, &ImageViewWidget::holdZoomEnded, this, &MainWindow::handleHoldZoomEnded);
    connect(m_imageView, &ImageViewWidget::zoomToRectRequested, this, &MainWindow::zoomToRect);
    connect(m_imageView, &ImageViewWidget::toggleFullscreenRequested, this, &MainWindow::toggleFullscreen);
    connect(m_imageView, &ImageViewWidget::panChanged, this, [this](QPointF offset) {
        m_controller.setPanOffset(offset);
    });
    connect(m_fileTreeView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current, const QModelIndex &) {
        if (m_syncingFolderTreeSelection) {
            return;
        }
        selectFolderFromTree(current);
    });
    connect(m_fileTreeView, &QTreeView::expanded, this, [this](const QModelIndex &index) {
        const core::CollectionFolderNode *node = m_fileTreeModel->nodeForIndex(index);
        if (node != nullptr && node->canLoadChildren && !node->loaded) {
            startFolderLoad(node->relativePath);
        }
    });
    connect(m_fileListView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current, const QModelIndex &) {
        if (m_syncingFileListSelection) {
            return;
        }
        selectImageFromFileList(current);
    });
    connect(m_fileListView, &QListView::activated, this, &MainWindow::selectImageFromFileList);

    connect(&m_decodeWatcher, &QFutureWatcher<DecodeJobResult>::finished, this, [this]() {
        const QFuture<DecodeJobResult> future = m_decodeWatcher.future();
        if (future.isCanceled() || future.resultCount() == 0 || !future.isResultReadyAt(0)) {
            if (future.isCanceled()) {
                m_imageRenderPending = false;
                m_pendingZoomRenderFallback.reset();
                updateActionState();
            }
            return;
        }

        const DecodeJobResult job = future.result();
        if (job.generation != m_decodeGeneration) {
            if (m_pendingZoomRenderFallback.has_value()
                && m_pendingZoomRenderFallback->generation == job.generation) {
                m_pendingZoomRenderFallback.reset();
            }
            return;
        }
        m_imageRenderPending = false;
        if (!job.decoded.ok()) {
            if (handleZoomRenderError(job)) {
                return;
            }
            m_pendingZoomRenderFallback.reset();
            showImageLoadError(job.entry, tr("Could not decode image."), job.decoded.errorMessage);
            return;
        }
        m_pendingZoomRenderFallback.reset();
        m_cache->insert(job.cacheKey, job.decoded);
        displayDecodedImage(job.decoded);
        prefetchNeighbors(currentDecodeRequest());
    });

    connect(&m_scanWatcher, &QFutureWatcher<ScanJobResult>::finished, this, [this]() {
        applyScanResult(m_scanWatcher.result());
    });
    connect(&m_folderLoadWatcher, &QFutureWatcher<FolderLoadJobResult>::finished, this, [this]() {
        applyFolderLoadResult(m_folderLoadWatcher.result());
    });
}

void MainWindow::setupFileListPanel()
{
    m_fileTreeModel = new CollectionTreeModel(this);
    m_fileListModel = new ImageListModel(this);

    m_fileTreeView = new QTreeView(this);
    m_fileTreeView->setModel(m_fileTreeModel);
    m_fileTreeView->setHeaderHidden(true);
    m_fileTreeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileTreeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileTreeView->setAnimated(false);
    m_fileTreeView->setUniformRowHeights(true);

    m_fileListView = new QListView(this);
    m_fileListView->setModel(m_fileListModel);
    m_fileListView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileListView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileListView->setAlternatingRowColors(true);
    m_fileListView->setUniformItemSizes(true);

    m_filePanelSplitter = new QSplitter(Qt::Vertical, this);
    m_filePanelSplitter->addWidget(m_fileTreeView);
    m_filePanelSplitter->addWidget(m_fileListView);
    m_filePanelSplitter->setChildrenCollapsible(false);
    m_filePanelSplitter->setStretchFactor(0, 2);
    m_filePanelSplitter->setStretchFactor(1, 3);
    m_filePanelSplitter->setSizes(QList<int> { 240, 360 });

    m_fileListDock = new QDockWidget(tr("Files"), this);
    m_fileListDock->setObjectName(QStringLiteral("fileListDock"));
    m_fileListDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_fileListDock->setWidget(m_filePanelSplitter);
    addDockWidget(Qt::LeftDockWidgetArea, m_fileListDock);
}

void MainWindow::setupInfoPanel()
{
    m_infoPanel = new ImageInfoPanel(this);

    m_infoDock = new QDockWidget(tr("Info"), this);
    m_infoDock->setObjectName(QStringLiteral("imageInfoDock"));
    m_infoDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_infoDock->setWidget(m_infoPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_infoDock);
    m_infoDock->hide();
}

void MainWindow::setupFullscreenOverlayPanels()
{
    setMouseTracking(true);
    installEventFilter(this);
    m_imageView->setMouseTracking(true);
    m_imageView->installEventFilter(this);

    auto configureOverlayWidget = [this](QWidget *widget) {
        widget->setAttribute(Qt::WA_StyledBackground);
        widget->setMouseTracking(true);
        widget->installEventFilter(this);
        widget->hide();
    };

    m_fullscreenLeftPanel = new QFrame(this);
    m_fullscreenLeftPanel->setObjectName(QStringLiteral("fullscreenLeftPanel"));
    configureOverlayWidget(m_fullscreenLeftPanel);
    m_fullscreenLeftPanel->setStyleSheet(QStringLiteral(
        "#fullscreenLeftPanel { background-color: rgba(24, 24, 24, 224); border-right: 1px solid rgba(255, 255, 255, 48); }"
        "#fullscreenLeftPanel QLabel { color: rgba(245, 245, 245, 230); }"
        "#fullscreenLeftPanel QSplitter::handle { background-color: rgba(255, 255, 255, 32); height: 1px; }"
        "#fullscreenLeftPanel QTreeView { background: transparent; border: none; outline: 0; color: rgba(245, 245, 245, 230); padding: 2px; }"
        "#fullscreenLeftPanel QTreeView::item { padding: 5px 6px; margin: 1px 0; border-radius: 4px; }"
        "#fullscreenLeftPanel QTreeView::item:hover { background-color: rgba(255, 255, 255, 28); }"
        "#fullscreenLeftPanel QTreeView::item:selected { background-color: rgba(80, 150, 255, 96); color: rgba(255, 255, 255, 245); }"
        "#fullscreenLeftPanel QListView { background: transparent; border: none; outline: 0; color: rgba(245, 245, 245, 230); padding: 2px; }"
        "#fullscreenLeftPanel QListView::item { padding: 6px 8px; margin: 1px 0; border-radius: 4px; }"
        "#fullscreenLeftPanel QListView::item:hover { background-color: rgba(255, 255, 255, 28); }"
        "#fullscreenLeftPanel QListView::item:selected { background-color: rgba(80, 150, 255, 96); color: rgba(255, 255, 255, 245); }"
        "#fullscreenLeftPanel QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }"
        "#fullscreenLeftPanel QScrollBar::handle:vertical { background: rgba(255, 255, 255, 70); border-radius: 4px; min-height: 24px; }"
        "#fullscreenLeftPanel QScrollBar::handle:vertical:hover { background: rgba(255, 255, 255, 105); }"
        "#fullscreenLeftPanel QScrollBar::add-line:vertical, #fullscreenLeftPanel QScrollBar::sub-line:vertical { height: 0; }"
        "#fullscreenLeftPanel QScrollBar::add-page:vertical, #fullscreenLeftPanel QScrollBar::sub-page:vertical { background: transparent; }"));

    auto *leftLayout = new QVBoxLayout(m_fullscreenLeftPanel);
    leftLayout->setContentsMargins(8, 8, 8, 8);
    leftLayout->setSpacing(6);

    auto *filesHeader = new QLabel(tr("Files"), m_fullscreenLeftPanel);
    QFont filesHeaderFont = filesHeader->font();
    filesHeaderFont.setBold(true);
    filesHeader->setFont(filesHeaderFont);
    leftLayout->addWidget(filesHeader);

    m_fullscreenFilePanelSplitter = new QSplitter(Qt::Vertical, m_fullscreenLeftPanel);
    m_fullscreenFilePanelSplitter->setChildrenCollapsible(false);
    m_fullscreenFilePanelSplitter->setStretchFactor(0, 2);
    m_fullscreenFilePanelSplitter->setStretchFactor(1, 3);

    m_fullscreenFileTreeView = new QTreeView(m_fullscreenFilePanelSplitter);
    m_fullscreenFileTreeView->setModel(m_fileTreeModel);
    m_fullscreenFileTreeView->setHeaderHidden(true);
    m_fullscreenFileTreeView->setFrameShape(QFrame::NoFrame);
    m_fullscreenFileTreeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fullscreenFileTreeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fullscreenFileTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fullscreenFileTreeView->setAnimated(false);
    m_fullscreenFileTreeView->setUniformRowHeights(true);
    m_fullscreenFileTreeView->setAutoFillBackground(false);
    m_fullscreenFileTreeView->setMouseTracking(true);
    m_fullscreenFileTreeView->installEventFilter(this);
    m_fullscreenFileTreeView->viewport()->setAutoFillBackground(false);
    m_fullscreenFileTreeView->viewport()->setMouseTracking(true);
    m_fullscreenFileTreeView->viewport()->installEventFilter(this);
    m_fullscreenFilePanelSplitter->addWidget(m_fullscreenFileTreeView);

    m_fullscreenFileListView = new QListView(m_fullscreenLeftPanel);
    m_fullscreenFileListView->setModel(m_fileListModel);
    m_fullscreenFileListView->setFrameShape(QFrame::NoFrame);
    m_fullscreenFileListView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fullscreenFileListView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fullscreenFileListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fullscreenFileListView->setAlternatingRowColors(false);
    m_fullscreenFileListView->setUniformItemSizes(true);
    m_fullscreenFileListView->setSpacing(1);
    m_fullscreenFileListView->setAutoFillBackground(false);
    m_fullscreenFileListView->setMouseTracking(true);
    m_fullscreenFileListView->installEventFilter(this);
    m_fullscreenFileListView->viewport()->setAutoFillBackground(false);
    m_fullscreenFileListView->viewport()->setMouseTracking(true);
    m_fullscreenFileListView->viewport()->installEventFilter(this);
    m_fullscreenFilePanelSplitter->addWidget(m_fullscreenFileListView);
    m_fullscreenFilePanelSplitter->setSizes(QList<int> { 220, 340 });
    leftLayout->addWidget(m_fullscreenFilePanelSplitter, 1);

    connect(m_fullscreenFileTreeView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current, const QModelIndex &) {
        if (m_syncingFolderTreeSelection) {
            return;
        }
        selectFolderFromTree(current);
    });
    connect(m_fullscreenFileTreeView, &QTreeView::expanded, this, [this](const QModelIndex &index) {
        const core::CollectionFolderNode *node = m_fileTreeModel->nodeForIndex(index);
        if (node != nullptr && node->canLoadChildren && !node->loaded) {
            startFolderLoad(node->relativePath);
        }
    });

    connect(m_fullscreenFileListView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current, const QModelIndex &) {
        if (m_syncingFileListSelection) {
            return;
        }
        selectImageFromFileList(current);
    });
    connect(m_fullscreenFileListView, &QListView::activated, this, &MainWindow::selectImageFromFileList);

    m_fullscreenRightPanel = new QFrame(this);
    m_fullscreenRightPanel->setObjectName(QStringLiteral("fullscreenRightPanel"));
    configureOverlayWidget(m_fullscreenRightPanel);
    m_fullscreenRightPanel->setStyleSheet(QStringLiteral(
        "#fullscreenRightPanel { background-color: rgba(24, 24, 24, 224); border-left: 1px solid rgba(255, 255, 255, 48); }"
        "#fullscreenRightPanel QLabel { color: rgba(245, 245, 245, 230); }"));

    auto *rightLayout = new QVBoxLayout(m_fullscreenRightPanel);
    rightLayout->setContentsMargins(8, 8, 8, 8);
    rightLayout->setSpacing(0);
    m_fullscreenInfoPanel = new ImageInfoPanel(m_fullscreenRightPanel);
    m_fullscreenInfoPanel->setMouseTracking(true);
    m_fullscreenInfoPanel->installEventFilter(this);
    rightLayout->addWidget(m_fullscreenInfoPanel, 1);

    m_fullscreenTopPanel = new QToolBar(tr("Fullscreen controls"), this);
    m_fullscreenTopPanel->setObjectName(QStringLiteral("fullscreenTopPanel"));
    m_fullscreenTopPanel->setMovable(false);
    m_fullscreenTopPanel->setFloatable(false);
    m_fullscreenTopPanel->setMouseTracking(true);
    m_fullscreenTopPanel->installEventFilter(this);
    m_fullscreenTopPanel->setStyleSheet(QStringLiteral(
        "#fullscreenTopPanel { background-color: rgba(24, 24, 24, 224); border-bottom: 1px solid rgba(255, 255, 255, 48); spacing: 4px; }"));
    m_fullscreenTopPanel->addAction(m_openFileAction);
    m_fullscreenTopPanel->addAction(m_openFolderAction);
    m_fullscreenTopPanel->addSeparator();
    m_fullscreenTopPanel->addAction(m_previousAction);
    m_fullscreenTopPanel->addAction(m_nextAction);
    m_fullscreenTopPanel->addAction(m_firstAction);
    m_fullscreenTopPanel->addAction(m_lastAction);
    m_fullscreenTopPanel->addAction(m_quickJumpAction);
    m_fullscreenTopPanel->addSeparator();
    m_fullscreenTopPanel->addAction(m_zoomInAction);
    m_fullscreenTopPanel->addAction(m_zoomOutAction);
    m_fullscreenTopPanel->addAction(m_fitAction);
    m_fullscreenTopPanel->addAction(m_actualSizeAction);
    m_fullscreenTopPanel->addAction(m_rotateLeftAction);
    m_fullscreenTopPanel->addAction(m_rotateRightAction);
    m_fullscreenTopPanel->addAction(m_exifAutorotateAction);
    m_fullscreenTopPanel->addAction(m_fullscreenAction);
    m_fullscreenTopPanel->hide();

    m_fullscreenBottomPanel = new QFrame(this);
    m_fullscreenBottomPanel->setObjectName(QStringLiteral("fullscreenBottomPanel"));
    configureOverlayWidget(m_fullscreenBottomPanel);
    m_fullscreenBottomPanel->setStyleSheet(QStringLiteral(
        "#fullscreenBottomPanel { background-color: rgba(24, 24, 24, 224); border-top: 1px solid rgba(255, 255, 255, 48); }"
        "#fullscreenBottomPanel QLabel { color: rgba(245, 245, 245, 230); }"));

    auto *bottomLayout = new QHBoxLayout(m_fullscreenBottomPanel);
    bottomLayout->setContentsMargins(12, 6, 12, 6);
    bottomLayout->setSpacing(0);
    m_fullscreenStatusLabel = new QLabel(m_fullscreenBottomPanel);
    m_fullscreenStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_fullscreenStatusLabel->setMouseTracking(true);
    m_fullscreenStatusLabel->installEventFilter(this);
    bottomLayout->addWidget(m_fullscreenStatusLabel, 1);

    refreshFileList();
    updateInfoPanelForCurrentImage();
    updateFullscreenStatusText();
}

void MainWindow::loadSettings()
{
    QSettings settings;
    applySettings(core::AppSettings::read(settings), false);
}

void MainWindow::saveSettings()
{
    captureLayoutSettings();
    QSettings settings;
    m_settings.write(settings);
    settings.sync();
}

void MainWindow::applySettings(const core::AppSettings &settings, bool reloadCurrentImage)
{
    const bool defaultZoomChanged = settings.viewer.defaultZoomMode != m_settings.viewer.defaultZoomMode;
    const bool exifAutorotateChanged = settings.viewer.exifAutorotate != m_settings.viewer.exifAutorotate;

    m_settings = settings;
    if (defaultZoomChanged) {
        m_baseZoomMode = m_settings.viewer.defaultZoomMode == core::ZoomMode::ActualSize
            ? core::ZoomMode::ActualSize
            : core::ZoomMode::FitToWindow;
    }
    m_imageSwitchPolicy = m_settings.viewer.imageSwitchPolicy;
    m_clickZoomBehavior = m_settings.viewer.clickZoomBehavior;
    m_clickZoomMultiplier = m_settings.viewer.clickZoomMultiplier;
    m_zoomStepMultiplier = 1.0 + (m_settings.viewer.zoomStepPercent / 100.0);
    m_fullscreenOverlaySettings.sideTriggerRatio = m_settings.fullscreen.sideTriggerRatio;
    m_fullscreenOverlaySettings.verticalTriggerRatio = m_settings.fullscreen.verticalTriggerRatio;
    m_imageView->setClickZoomBehavior(m_clickZoomBehavior);
    if (m_exifAutorotateAction != nullptr) {
        const QSignalBlocker blocker(m_exifAutorotateAction);
        m_exifAutorotateAction->setChecked(m_settings.viewer.exifAutorotate);
    }

    if (!m_settings.interface.rememberLayout) {
        m_settings.interface.mainWindowGeometry.clear();
        m_settings.interface.mainWindowState.clear();
        m_settings.interface.fileSplitterState.clear();
        m_settings.interface.fullscreenFileSplitterState.clear();
    }

    if (reloadCurrentImage && m_controller.hasCollection()) {
        if (defaultZoomChanged) {
            m_temporaryZoomReturnMode.reset();
            if (m_baseZoomMode == core::ZoomMode::ActualSize) {
                m_controller.setActualSize();
            } else {
                m_controller.setFitToWindow();
            }
            m_imageView->resetPan();
            updateInteractionState();
            loadCurrentImage(false);
        } else if (exifAutorotateChanged) {
            loadCurrentImage(false);
        }
    }

    updateFullscreenOverlayGeometry();
    updateFullscreenOverlayVisibility();
    updateActionState();
}

void MainWindow::captureLayoutSettings()
{
    if (!m_settings.interface.rememberLayout) {
        m_settings.interface.mainWindowGeometry.clear();
        m_settings.interface.mainWindowState.clear();
        m_settings.interface.fileSplitterState.clear();
        m_settings.interface.fullscreenFileSplitterState.clear();
        return;
    }

    if (!isFullScreen()) {
        m_settings.interface.mainWindowGeometry = saveGeometry();
        m_settings.interface.mainWindowState = saveState();
    }
    if (!isFullScreen() && m_filePanelSplitter != nullptr) {
        m_settings.interface.fileSplitterState = m_filePanelSplitter->saveState();
    }
    if (m_fullscreenFilePanelSplitter != nullptr) {
        m_settings.interface.fullscreenFileSplitterState = m_fullscreenFilePanelSplitter->saveState();
    }
}

void MainWindow::restoreLayoutSettings()
{
    if (!m_settings.interface.rememberLayout) {
        return;
    }

    if (!m_settings.interface.mainWindowGeometry.isEmpty()) {
        restoreGeometry(m_settings.interface.mainWindowGeometry);
    }
    if (!m_settings.interface.mainWindowState.isEmpty()) {
        restoreState(m_settings.interface.mainWindowState);
    }
    if (!m_settings.interface.fileSplitterState.isEmpty() && m_filePanelSplitter != nullptr) {
        m_filePanelSplitter->restoreState(m_settings.interface.fileSplitterState);
    }
    if (!m_settings.interface.fullscreenFileSplitterState.isEmpty() && m_fullscreenFilePanelSplitter != nullptr) {
        m_fullscreenFilePanelSplitter->restoreState(m_settings.interface.fullscreenFileSplitterState);
    }
}

void MainWindow::openFile(const QString &filePath)
{
    clearArchivePassword();

    const core::ScanResult single = core::singleImageCollection(filePath);
    if (!single.ok()) {
        showError(single.errorMessage);
        return;
    }

    ++m_sourceGeneration;
    m_temporaryZoomReturnMode.reset();
    m_baseZoomMode = m_settings.viewer.defaultZoomMode;
    m_cache->clear();
    m_controller.setCollection(single.entries, single.selectedIndex);
    if (m_baseZoomMode == core::ZoomMode::ActualSize) {
        m_controller.setActualSize();
    } else {
        m_controller.setFitToWindow();
    }
    clearSourceTree();
    refreshFileList();
    updateInfoPanelForCurrentImage();
    updateInteractionState();
    loadCurrentImage();

    const QFileInfo fileInfo(filePath);
    startFolderScan(fileInfo.absolutePath(), fileInfo.absoluteFilePath());
}

void MainWindow::openFolder(const QString &folderPath)
{
    clearArchivePassword();

    ++m_sourceGeneration;
    m_temporaryZoomReturnMode.reset();
    m_baseZoomMode = m_settings.viewer.defaultZoomMode;
    m_cache->clear();
    m_controller.clearCollection();
    clearSourceTree();
    refreshFileList();
    m_infoPanel->showMessage(tr("Scanning folder..."));
    m_fullscreenInfoPanel->showMessage(tr("Scanning folder..."));
    m_imageView->setMessage(tr("Scanning folder..."));
    updateStatus(tr("Scanning folder..."));
    updateInteractionState();
    startFolderScan(folderPath, QString());
}

void MainWindow::openArchive(const QString &archivePath)
{
    clearArchivePassword();
    m_currentArchivePath = QFileInfo(archivePath).absoluteFilePath();

    ++m_sourceGeneration;
    m_temporaryZoomReturnMode.reset();
    m_baseZoomMode = m_settings.viewer.defaultZoomMode;
    m_cache->clear();
    m_controller.clearCollection();
    clearSourceTree();
    refreshFileList();
    m_infoPanel->showMessage(tr("Scanning archive..."));
    m_fullscreenInfoPanel->showMessage(tr("Scanning archive..."));
    m_imageView->setMessage(tr("Scanning archive..."));
    updateStatus(tr("Scanning archive..."));
    updateInteractionState();
    startArchiveScan(m_currentArchivePath, m_currentArchivePassword);
}

void MainWindow::startFolderScan(const QString &folderPath, const QString &selectedPath)
{
    const quint64 generation = m_sourceGeneration;
    if (m_scanWatcher.isRunning()) {
        m_scanWatcher.cancel();
    }

    m_scanWatcher.setFuture(QtConcurrent::run([folderPath, selectedPath, generation]() {
        ScanJobResult job;
        job.generation = generation;
        job.selectedPath = selectedPath;
        job.result = core::scanFileSystemSourceTree(folderPath, selectedPath);
        return job;
    }));
}

void MainWindow::startArchiveScan(const QString &archivePath, const QString &password)
{
    const quint64 generation = m_sourceGeneration;
    if (m_scanWatcher.isRunning()) {
        m_scanWatcher.cancel();
    }

    m_scanWatcher.setFuture(QtConcurrent::run([archivePath, password, generation]() {
        ScanJobResult job;
        job.generation = generation;
        job.selectedPath = archivePath;
        job.result = core::scanArchiveSourceTree(archivePath, password);
        return job;
    }));
}

void MainWindow::startFolderLoad(const QString &folderRelativePath, int selectedImageIndex)
{
    if (m_sourceRootPath.isEmpty()) {
        return;
    }

    core::CollectionFolderNode *node = folderNodeForPath(folderRelativePath);
    if (node == nullptr || node->loaded || !node->canLoadChildren) {
        return;
    }
    if (m_folderLoadWatcher.isRunning() && m_loadingFolderPath == folderRelativePath) {
        return;
    }

    const quint64 generation = m_sourceGeneration;
    if (m_folderLoadWatcher.isRunning()) {
        m_folderLoadWatcher.cancel();
    }

    const QString rootPath = m_sourceRootPath;
    m_loadingFolderPath = folderRelativePath;
    m_loadingFolderSelectedIndex = selectedImageIndex;
    m_folderLoadWatcher.setFuture(QtConcurrent::run([rootPath, folderRelativePath, generation]() {
        FolderLoadJobResult job;
        job.generation = generation;
        job.folderRelativePath = folderRelativePath;
        job.result = core::loadFileSystemFolder(rootPath, folderRelativePath);
        return job;
    }));
}

void MainWindow::applyScanResult(const ScanJobResult &job)
{
    if (job.generation != m_sourceGeneration) {
        return;
    }

    if (!job.result.ok()) {
        if ((job.result.errorReason == core::ScanErrorReason::PasswordRequired
                || job.result.errorReason == core::ScanErrorReason::InvalidPassword)
            && core::isSupportedArchiveFile(QFileInfo(job.selectedPath))) {
            const bool retry = job.result.errorReason == core::ScanErrorReason::InvalidPassword;
            if (promptArchivePassword(job.selectedPath, retry)) {
                m_infoPanel->showMessage(tr("Scanning archive..."));
                m_fullscreenInfoPanel->showMessage(tr("Scanning archive..."));
                m_imageView->setMessage(tr("Scanning archive..."));
                updateStatus(tr("Scanning archive..."));
                startArchiveScan(job.selectedPath, m_currentArchivePassword);
            } else {
                clearArchivePassword();
                showError(tr("Archive password is required."));
            }
            return;
        }

        if (!m_controller.hasCollection()) {
            showError(job.result.errorMessage);
        }
        return;
    }

    m_sourceRoot = job.result.root;
    m_sourceRootPath = job.result.rootPath;
    m_fileTreeModel->setRoot(m_sourceRoot);
    if (m_sourceRoot != nullptr) {
        m_fileTreeView->expand(m_fileTreeModel->indexForFolderPath(QString()));
        if (m_fullscreenFileTreeView != nullptr) {
            m_fullscreenFileTreeView->expand(m_fileTreeModel->indexForFolderPath(QString()));
        }
    }
    activateFolder(job.result.selectedFolderPath, job.result.selectedImageIndex, true);
}

void MainWindow::applyFolderLoadResult(const FolderLoadJobResult &job)
{
    if (job.generation != m_sourceGeneration) {
        return;
    }
    if (m_loadingFolderPath == job.folderRelativePath) {
        m_loadingFolderPath.clear();
    }

    core::CollectionFolderNode *node = folderNodeForPath(job.folderRelativePath);
    if (node == nullptr) {
        return;
    }

    if (!job.result.ok()) {
        if (m_activeFolderPath == job.folderRelativePath) {
            showError(job.result.errorMessage);
        }
        return;
    }

    node->images = job.result.images;
    node->children = job.result.children;
    node->loaded = true;
    for (const auto &child : node->children) {
        child->parent = node;
    }

    m_fileTreeModel->setRoot(m_sourceRoot);
    const QModelIndex loadedIndex = m_fileTreeModel->indexForFolderPath(job.folderRelativePath);
    if (loadedIndex.isValid()) {
        m_fileTreeView->expand(loadedIndex);
        if (m_fullscreenFileTreeView != nullptr) {
            m_fullscreenFileTreeView->expand(loadedIndex);
        }
    }

    if (m_activeFolderPath == job.folderRelativePath) {
        const int selectedImageIndex = m_loadingFolderPath.isEmpty() ? m_loadingFolderSelectedIndex : 0;
        activateFolder(job.folderRelativePath, selectedImageIndex, true);
    } else {
        syncFolderTreeSelection();
    }
}

bool MainWindow::promptArchivePassword(const QString &archivePath, bool retry)
{
    bool ok = false;
    const QFileInfo archiveInfo(archivePath);
    const QString label = retry
        ? tr("The password was not accepted for %1. Enter the archive password:").arg(archiveInfo.fileName())
        : tr("Enter the password for %1:").arg(archiveInfo.fileName());
    const QString password = QInputDialog::getText(
        this,
        tr("Archive Password"),
        label,
        QLineEdit::Password,
        QString(),
        &ok,
        Qt::Dialog);

    if (!ok) {
        return false;
    }

    m_currentArchivePath = QFileInfo(archivePath).absoluteFilePath();
    m_currentArchivePassword = password;
    return true;
}

void MainWindow::clearArchivePassword()
{
    m_currentArchivePath.clear();
    m_currentArchivePassword.clear();
}

void MainWindow::displayDecodedImage(const core::DecodedImage &decoded)
{
    m_imageRenderPending = false;
    m_currentDecoded = decoded;
    m_imageView->setImageInteractionEnabled(true);
    m_imageView->setImage(decoded.image);
    m_imageView->setPanOffset(m_controller.panOffset());
    updateInfoPanelForCurrentImage();
    updateStatus();
    updateActionState();
    updateInteractionState();
}

MainWindow::ViewRenderState MainWindow::captureViewRenderState() const
{
    return ViewRenderState {
        m_controller.zoomMode(),
        m_controller.zoomFactor(),
        m_controller.panOffset(),
        m_currentDecoded,
    };
}

void MainWindow::restoreViewRenderState(const ViewRenderState &state, const QString &overlayMessage)
{
    m_imageRenderPending = false;
    switch (state.zoomMode) {
    case core::ZoomMode::FitToWindow:
        m_controller.setFitToWindow();
        break;
    case core::ZoomMode::ActualSize:
        m_controller.setActualSize();
        break;
    case core::ZoomMode::Custom:
        m_controller.setCustomZoom(state.zoomFactor);
        break;
    }

    m_controller.setPanOffset(state.panOffset);
    m_currentDecoded = state.decoded;
    m_imageView->setImageInteractionEnabled(true);
    m_imageView->setImage(m_currentDecoded.image);
    m_imageView->setPanOffset(m_controller.panOffset());
    m_imageView->setOverlayMessage(overlayMessage);
    updateInfoPanelForCurrentImage();
    updateStatus(overlayMessage);
    updateActionState();
    updateInteractionState();
}

bool MainWindow::handleZoomRenderError(const DecodeJobResult &job)
{
    if (job.decoded.errorReason != core::DecodeErrorReason::RenderTooLarge
        || !m_pendingZoomRenderFallback.has_value()
        || m_pendingZoomRenderFallback->generation != job.generation
        || !m_pendingZoomRenderFallback->state.decoded.ok()) {
        return false;
    }

    m_activeHoldZoom.reset();
    restoreViewRenderState(m_pendingZoomRenderFallback->state, tr("Too large to render"));
    m_pendingZoomRenderFallback.reset();
    return true;
}

void MainWindow::showError(const QString &message)
{
    m_imageRenderPending = false;
    m_activeHoldZoom.reset();
    m_currentDecoded = {};
    const QString fallback = message.isEmpty() ? tr("Operation failed.") : message;
    m_imageView->setMessage(fallback);
    m_infoPanel->showMessage(fallback);
    if (m_fullscreenInfoPanel != nullptr) {
        m_fullscreenInfoPanel->showMessage(fallback);
    }
    updateStatus(fallback);
    updateActionState();
    updateInteractionState();
}

void MainWindow::showImageLoadError(const core::ImageEntry &entry, const QString &reason, const QString &details)
{
    const QString displayName = entry.displayName.isEmpty() ? entry.absolutePath : entry.displayName;
    const QString message = details.isEmpty()
        ? tr("%1: %2").arg(displayName, reason)
        : tr("%1: %2 (%3)").arg(displayName, reason, details);
    showError(message);
    m_infoPanel->showError(entry, details.isEmpty() ? reason : tr("%1 (%2)").arg(reason, details));
    if (m_fullscreenInfoPanel != nullptr) {
        m_fullscreenInfoPanel->showError(entry, details.isEmpty() ? reason : tr("%1 (%2)").arg(reason, details));
    }
}

void MainWindow::updateStatus(const QString &message)
{
    QString statusText;
    if (!message.isEmpty()) {
        statusText = message;
    } else {
        const core::ImageEntry *entry = m_controller.currentEntry();
        if (entry == nullptr) {
            statusText = tr("No image loaded.");
        } else {
            const QString displayPath = entry->sourceRelativePath.isEmpty()
                ? entry->displayName
                : entry->sourceRelativePath;
            const QString zoomText = m_controller.zoomMode() == core::ZoomMode::FitToWindow
                ? tr("fit")
                : tr("%1%").arg(std::lround(m_controller.zoomFactor() * 100.0));

            const QString indexText = tr("%1 / %2").arg(m_controller.currentIndex() + 1).arg(m_controller.count());
            if (m_currentDecoded.ok() && !m_currentDecoded.sourceSize.isEmpty()) {
                const QSize sourceSize = m_currentDecoded.sourceSize;
                const double megapixels = static_cast<double>(sourceSize.width()) * static_cast<double>(sourceSize.height()) / 1'000'000.0;
                const QString dimensionsText = tr("%1 x %2 · %3 MP")
                                                   .arg(sourceSize.width())
                                                   .arg(sourceSize.height())
                                                   .arg(QLocale::system().toString(megapixels, 'f', 2));
                statusText = tr("%1 · %2 · %3 · %4 · %5 · %6")
                                 .arg(displayPath,
                                      entry->formatHint,
                                      formattedFileSize(entry->fileSize),
                                      dimensionsText,
                                      zoomText,
                                      indexText);
            } else {
                statusText = tr("%1 · %2 · %3 · %4 · %5")
                                 .arg(displayPath,
                                      entry->formatHint,
                                      formattedFileSize(entry->fileSize),
                                      zoomText,
                                      indexText);
            }
        }
    }

    m_statusLabel->setText(statusText);
    updateFullscreenStatusText();
}

void MainWindow::updateActionState()
{
    const bool hasImage = m_controller.hasCollection();
    const bool hasPreviousInFolder = hasImage && m_controller.currentIndex() > 0;
    const bool hasNextInFolder = hasImage && m_controller.currentIndex() + 1 < m_controller.count();
    const bool hasPrevious = hasPreviousInFolder || hasAdjacentFolder(false);
    const bool hasNext = hasNextInFolder || hasAdjacentFolder(true);
    const bool canChangeView = hasImage && !m_imageRenderPending && !m_activeHoldZoom.has_value();

    m_previousAction->setEnabled(hasPrevious || hasImage);
    m_nextAction->setEnabled(hasNext || hasImage);
    m_firstAction->setEnabled(hasPreviousInFolder);
    m_lastAction->setEnabled(hasNextInFolder);
    m_quickJumpAction->setEnabled(hasImage);
    m_zoomInAction->setEnabled(canChangeView);
    m_zoomOutAction->setEnabled(canChangeView);
    m_fitAction->setEnabled(canChangeView);
    m_actualSizeAction->setEnabled(canChangeView);
    m_rotateLeftAction->setEnabled(canChangeView);
    m_rotateRightAction->setEnabled(canChangeView);
    m_fullscreenAction->setEnabled(hasImage);
}

void MainWindow::updateInteractionState()
{
    const bool panningEnabled = m_controller.hasCollection()
        && m_controller.zoomMode() != core::ZoomMode::FitToWindow;
    const bool clickZoomCursorEnabled = m_controller.hasCollection()
        && !m_imageRenderPending
        && !m_activeHoldZoom.has_value()
        && m_currentDecoded.ok()
        && m_currentDecoded.effectiveScale > 0.0;
    m_imageView->setPanningEnabled(panningEnabled);
    m_imageView->setClickZoomCursorEnabled(clickZoomCursorEnabled);
}

void MainWindow::updateInfoPanelForCurrentImage(const QString &state)
{
    const core::ImageEntry *entry = m_controller.currentEntry();
    if (entry == nullptr) {
        m_infoPanel->showMessage(tr("No image loaded."));
        if (m_fullscreenInfoPanel != nullptr) {
            m_fullscreenInfoPanel->showMessage(tr("No image loaded."));
        }
        return;
    }

    if (state.isEmpty() && m_currentDecoded.ok()) {
        m_infoPanel->showDecodedImage(*entry, m_currentDecoded);
        if (m_fullscreenInfoPanel != nullptr) {
            m_fullscreenInfoPanel->showDecodedImage(*entry, m_currentDecoded);
        }
        return;
    }

    m_infoPanel->showEntry(*entry, state);
    if (m_fullscreenInfoPanel != nullptr) {
        m_fullscreenInfoPanel->showEntry(*entry, state);
    }
}

void MainWindow::scheduleFitReload()
{
    if (m_controller.hasCollection() && m_controller.zoomMode() == core::ZoomMode::FitToWindow) {
        m_resizeReloadTimer.start();
    }
}

void MainWindow::prefetchNeighbors(const core::DecodeRequest &request)
{
    const QVector<core::ImageEntry> entries = m_controller.entries();
    const int currentIndex = m_controller.currentIndex();
    const auto cache = m_cache;
    const QString currentArchivePath = m_currentArchivePath;
    const QString currentArchivePassword = m_currentArchivePassword;

    for (int index : { currentIndex - 1, currentIndex + 1 }) {
        if (index < 0 || index >= entries.size()) {
            continue;
        }

        const core::ImageEntry entry = entries.at(index);
        const QString key = core::cacheKeyFor(entry, request);
        if (cache->contains(key)) {
            continue;
        }

        const QString archivePassword = entry.source == core::ImageEntrySource::Archive
            && entry.archivePath == currentArchivePath
            ? currentArchivePassword
            : QString();
        auto future = QtConcurrent::run([cache, entry, request, key, archivePassword]() {
            core::VipsImageDecoder decoder;
            const core::DecodedImage decoded = decoder.decode(entry, request, archivePassword);
            if (decoded.ok()) {
                cache->insert(key, decoded);
            }
        });
        Q_UNUSED(future)
    }
}

void MainWindow::reloadAfterImageSwitch()
{
    applyImageSwitchViewPolicy();
    syncFileListSelection();
    loadCurrentImage();
}

void MainWindow::applyImageSwitchViewPolicy()
{
    m_temporaryZoomReturnMode.reset();

    if (m_imageSwitchPolicy == core::ImageSwitchPolicy::ResetToBaseMode) {
        if (m_baseZoomMode == core::ZoomMode::ActualSize) {
            m_controller.setActualSize();
        } else {
            m_controller.setFitToWindow();
        }
        m_imageView->resetPan();
    }

    updateInteractionState();
}

void MainWindow::applyWheelAction(core::WheelAction action, int angleDeltaY, QPoint position)
{
    if (angleDeltaY == 0) {
        return;
    }

    switch (action) {
    case core::WheelAction::NavigateImages:
        if (angleDeltaY > 0) {
            showPreviousImage();
        } else {
            showNextImage();
        }
        break;
    case core::WheelAction::Zoom:
        if (m_imageRenderPending || m_activeHoldZoom.has_value()) {
            break;
        }
        if (angleDeltaY > 0) {
            zoomInAt(position);
        } else {
            zoomOutAt(position);
        }
        break;
    case core::WheelAction::NoAction:
        break;
    }
}

void MainWindow::applyCustomZoomWithFallback(double zoomFactor, QPointF panOffset)
{
    const ViewRenderState fallback = captureViewRenderState();
    applyCustomZoomWithFallback(zoomFactor, panOffset, fallback);
}

void MainWindow::applyCustomZoomWithFallback(double zoomFactor, QPointF panOffset, const ViewRenderState &fallback)
{
    m_controller.setCustomZoom(zoomFactor);
    m_controller.setPanOffset(panOffset);
    updateInteractionState();
    loadCurrentImage(false, fallback);
}

bool MainWindow::customZoomForAnchor(QPointF anchorPosition, double multiplier, double *zoomFactor, QPointF *panOffset) const
{
    if (zoomFactor == nullptr || panOffset == nullptr || !m_currentDecoded.ok() || m_currentDecoded.effectiveScale <= 0.0) {
        return false;
    }

    const QRectF imageRect = m_imageView->imageRect();
    if (imageRect.isEmpty()) {
        return false;
    }

    const double currentScale = m_currentDecoded.effectiveScale;
    const double newScale = std::clamp(
        currentScale * multiplier,
        core::MinimumZoomFactor,
        core::MaximumZoomFactor);
    if (qFuzzyCompare(currentScale, newScale)) {
        return false;
    }

    const QPointF anchor = clampedPointToRect(anchorPosition, imageRect);
    const QPointF imagePoint = anchor - imageRect.topLeft();
    const QPointF sourcePoint(imagePoint.x() / currentScale, imagePoint.y() / currentScale);
    const QSizeF newDisplaySize(
        m_currentDecoded.image.width() * newScale / currentScale,
        m_currentDecoded.image.height() * newScale / currentScale);
    const QPointF centeredTopLeft(
        (m_imageView->width() - newDisplaySize.width()) / 2.0,
        (m_imageView->height() - newDisplaySize.height()) / 2.0);
    const QPointF pointAtNewScale(sourcePoint.x() * newScale, sourcePoint.y() * newScale);

    *zoomFactor = newScale;
    *panOffset = anchor - centeredTopLeft - pointAtNewScale;
    return true;
}

void MainWindow::handleHoldZoomStarted(QPoint position)
{
    if (m_clickZoomBehavior != core::ClickZoomBehavior::HoldToZoomPan
        || m_imageRenderPending
        || m_activeHoldZoom.has_value()
        || !m_controller.hasCollection()) {
        return;
    }

    const ViewRenderState returnState = captureViewRenderState();
    double newScale = 1.0;
    QPointF newPanOffset;
    if (!customZoomForAnchor(QPointF(position), m_clickZoomMultiplier, &newScale, &newPanOffset)) {
        return;
    }

    m_temporaryZoomReturnMode.reset();
    m_activeHoldZoom = ActiveHoldZoom { returnState };
    applyCustomZoomWithFallback(newScale, newPanOffset, returnState);
}

void MainWindow::handleHoldZoomMoved(QPoint delta)
{
    if (!m_activeHoldZoom.has_value() || delta.isNull()) {
        return;
    }

    const QPointF panOffset = m_controller.panOffset() + QPointF(delta);
    m_controller.setPanOffset(panOffset);
    if (!m_imageRenderPending) {
        m_imageView->setPanOffset(panOffset);
    }
}

void MainWindow::handleHoldZoomEnded()
{
    if (!m_activeHoldZoom.has_value()) {
        return;
    }

    const ViewRenderState returnState = m_activeHoldZoom->returnState;
    m_activeHoldZoom.reset();
    if (m_imageRenderPending) {
        ++m_decodeGeneration;
        m_pendingZoomRenderFallback.reset();
    }
    restoreViewRenderState(returnState, QString());
}

void MainWindow::zoomByMultiplier(double multiplier, QPointF anchorPosition)
{
    if (m_imageRenderPending || m_activeHoldZoom.has_value() || !m_controller.hasCollection()) {
        return;
    }

    if (!m_currentDecoded.ok() || m_currentDecoded.effectiveScale <= 0.0) {
        const double fallbackScale = std::clamp(
            m_controller.zoomFactor() * multiplier,
            core::MinimumZoomFactor,
            core::MaximumZoomFactor);
        m_controller.setCustomZoom(fallbackScale);
        m_controller.resetPan();
        updateInteractionState();
        loadCurrentImage(false);
        return;
    }

    double newScale = 1.0;
    QPointF newPanOffset;
    if (customZoomForAnchor(anchorPosition, multiplier, &newScale, &newPanOffset)) {
        applyCustomZoomWithFallback(newScale, newPanOffset);
    }
}

void MainWindow::zoomInAt(QPointF anchorPosition)
{
    zoomByMultiplier(m_zoomStepMultiplier, anchorPosition);
}

void MainWindow::zoomOutAt(QPointF anchorPosition)
{
    zoomByMultiplier(1.0 / m_zoomStepMultiplier, anchorPosition);
}

core::WheelAction MainWindow::wheelActionForModifiers(Qt::KeyboardModifiers modifiers) const
{
    if (modifiers.testFlag(Qt::ControlModifier)) {
        return m_settings.viewer.ctrlWheelAction;
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        return m_settings.viewer.shiftWheelAction;
    }
    return m_settings.viewer.wheelAction;
}

void MainWindow::clearSourceTree()
{
    m_sourceRoot.reset();
    m_sourceRootPath.clear();
    m_activeFolderPath.clear();
    m_loadingFolderPath.clear();
    m_loadingFolderSelectedIndex = 0;
    if (m_fileTreeModel != nullptr) {
        m_fileTreeModel->clear();
    }
}

void MainWindow::activateFolder(const QString &folderRelativePath, int selectedImageIndex, bool loadCurrent)
{
    core::CollectionFolderNode *folder = folderNodeForPath(folderRelativePath);
    if (folder == nullptr) {
        m_activeFolderPath.clear();
        m_controller.clearCollection();
        refreshFileList();
        updateInfoPanelForCurrentImage();
        updateStatus();
        updateActionState();
        return;
    }

    m_activeFolderPath = folder->relativePath;
    syncFolderTreeSelection();

    if (!folder->loaded && folder->canLoadChildren) {
        m_currentDecoded = {};
        m_controller.clearCollection();
        refreshFileList();
        m_imageView->setMessage(tr("Loading folder..."));
        m_infoPanel->showMessage(tr("Loading folder..."));
        if (m_fullscreenInfoPanel != nullptr) {
            m_fullscreenInfoPanel->showMessage(tr("Loading folder..."));
        }
        updateStatus(tr("Loading folder..."));
        updateActionState();
        updateInteractionState();
        startFolderLoad(folder->relativePath, selectedImageIndex);
        return;
    }

    if (selectedImageIndex < 0 && !folder->images.isEmpty()) {
        selectedImageIndex = folder->images.size() - 1;
    }

    m_temporaryZoomReturnMode.reset();
    m_controller.setCollection(folder->images, selectedImageIndex);
    if (m_baseZoomMode == core::ZoomMode::ActualSize) {
        m_controller.setActualSize();
    } else {
        m_controller.setFitToWindow();
    }
    m_imageView->resetPan();
    refreshFileList();
    updateInteractionState();

    if (!m_controller.hasCollection()) {
        m_currentDecoded = {};
        m_imageView->setMessage(tr("Folder contains no supported images."));
        updateInfoPanelForCurrentImage();
        updateStatus(tr("Folder contains no supported images."));
        updateActionState();
        return;
    }

    if (loadCurrent) {
        loadCurrentImage();
    } else {
        updateInfoPanelForCurrentImage();
        updateStatus();
        updateActionState();
    }
}

bool MainWindow::navigateToAdjacentFolder(bool forward)
{
    if (!hasAdjacentFolder(forward)) {
        return false;
    }

    QVector<const core::CollectionFolderNode *> folders;
    appendFoldersInPreorder(m_sourceRoot.get(), &folders);
    int activeIndex = -1;
    for (qsizetype index = 0; index < folders.size(); ++index) {
        if (folders.at(index)->relativePath == m_activeFolderPath) {
            activeIndex = static_cast<int>(index);
            break;
        }
    }
    if (activeIndex < 0) {
        return false;
    }

    if (forward) {
        for (int index = activeIndex + 1; index < folders.size(); ++index) {
            if (isNavigableFolder(folders.at(index))) {
                activateFolder(folders.at(index)->relativePath, 0, true);
                return true;
            }
        }
        return false;
    }

    for (int index = activeIndex - 1; index >= 0; --index) {
        if (isNavigableFolder(folders.at(index))) {
            activateFolder(folders.at(index)->relativePath, -1, true);
            return true;
        }
    }
    return false;
}

bool MainWindow::hasAdjacentFolder(bool forward) const
{
    if (m_sourceRoot == nullptr) {
        return false;
    }

    QVector<const core::CollectionFolderNode *> folders;
    appendFoldersInPreorder(m_sourceRoot.get(), &folders);
    int activeIndex = -1;
    for (qsizetype index = 0; index < folders.size(); ++index) {
        if (folders.at(index)->relativePath == m_activeFolderPath) {
            activeIndex = static_cast<int>(index);
            break;
        }
    }
    if (activeIndex < 0) {
        return false;
    }

    if (forward) {
        for (int index = activeIndex + 1; index < folders.size(); ++index) {
            if (isNavigableFolder(folders.at(index))) {
                return true;
            }
        }
        return false;
    }

    for (int index = activeIndex - 1; index >= 0; --index) {
        if (isNavigableFolder(folders.at(index))) {
            return true;
        }
    }
    return false;
}

void MainWindow::refreshFileList()
{
    m_fileListModel->setEntries(m_controller.entries());
    m_fileListView->setEnabled(m_controller.hasCollection());
    if (m_fullscreenFileListView != nullptr) {
        m_fullscreenFileListView->setEnabled(m_controller.hasCollection());
    }
    syncFileListSelection();
    syncFolderTreeSelection();
}

void MainWindow::syncFileListSelection()
{
    QItemSelectionModel *selectionModel = m_fileListView->selectionModel();
    if (selectionModel == nullptr) {
        return;
    }

    const QScopedValueRollback<bool> guard(m_syncingFileListSelection, true);
    const int currentIndex = m_controller.currentIndex();
    if (currentIndex < 0 || currentIndex >= m_fileListModel->rowCount()) {
        selectionModel->clear();
        m_fileListView->setCurrentIndex(QModelIndex());
        syncFullscreenFileListSelection();
        return;
    }

    const QModelIndex modelIndex = m_fileListModel->index(currentIndex, 0);
    selectionModel->setCurrentIndex(
        modelIndex,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current | QItemSelectionModel::Rows);
    m_fileListView->scrollTo(modelIndex, QAbstractItemView::EnsureVisible);
    syncFullscreenFileListSelection();
}

void MainWindow::syncFolderTreeSelection()
{
    if (m_fileTreeModel == nullptr || m_fileTreeView == nullptr) {
        return;
    }

    const QModelIndex modelIndex = m_fileTreeModel->indexForFolderPath(m_activeFolderPath);
    const QScopedValueRollback<bool> guard(m_syncingFolderTreeSelection, true);
    if (!modelIndex.isValid()) {
        m_fileTreeView->setCurrentIndex(QModelIndex());
        if (m_fullscreenFileTreeView != nullptr) {
            m_fullscreenFileTreeView->setCurrentIndex(QModelIndex());
        }
        return;
    }

    m_fileTreeView->setCurrentIndex(modelIndex);
    m_fileTreeView->scrollTo(modelIndex, QAbstractItemView::EnsureVisible);
    m_fileTreeView->expand(modelIndex);
    if (m_fullscreenFileTreeView != nullptr) {
        m_fullscreenFileTreeView->setCurrentIndex(modelIndex);
        m_fullscreenFileTreeView->scrollTo(modelIndex, QAbstractItemView::EnsureVisible);
        m_fullscreenFileTreeView->expand(modelIndex);
    }
}

void MainWindow::selectImageFromFileList(const QModelIndex &index)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_controller.count()) {
        return;
    }

    if (m_controller.setCurrentIndex(index.row())) {
        reloadAfterImageSwitch();
    }
}

void MainWindow::selectFolderFromTree(const QModelIndex &index)
{
    core::CollectionFolderNode *node = m_fileTreeModel->nodeForIndex(index);
    if (node == nullptr) {
        return;
    }
    activateFolder(node->relativePath, 0, true);
}

core::CollectionFolderNode *MainWindow::folderNodeForPath(const QString &folderRelativePath) const
{
    if (m_fileTreeModel == nullptr) {
        return nullptr;
    }
    const QModelIndex index = m_fileTreeModel->indexForFolderPath(folderRelativePath);
    return m_fileTreeModel->nodeForIndex(index);
}

void MainWindow::rememberTemporaryZoomReturnMode()
{
    if (m_temporaryZoomReturnMode.has_value()) {
        return;
    }

    m_temporaryZoomReturnMode = m_controller.zoomMode() == core::ZoomMode::ActualSize
        ? core::ZoomMode::ActualSize
        : core::ZoomMode::FitToWindow;
}

bool MainWindow::restoreTemporaryZoomReturnMode()
{
    if (!m_temporaryZoomReturnMode.has_value()) {
        return false;
    }

    const core::ZoomMode returnMode = *m_temporaryZoomReturnMode;
    m_temporaryZoomReturnMode.reset();
    m_baseZoomMode = returnMode;
    if (returnMode == core::ZoomMode::ActualSize) {
        m_controller.setActualSize();
    } else {
        m_controller.setFitToWindow();
    }
    m_imageView->resetPan();
    loadCurrentImage();
    return true;
}

void MainWindow::enterFullscreenMode()
{
    if (m_fullscreenUiActive) {
        return;
    }

    m_preFullscreenUiState.menuBarVisible = menuBar()->isVisible();
    m_preFullscreenUiState.toolBarVisible = m_mainToolBar != nullptr && m_mainToolBar->isVisible();
    m_preFullscreenUiState.statusBarVisible = statusBar()->isVisible();
    m_preFullscreenUiState.fileDockVisible = m_fileListDock != nullptr && m_fileListDock->isVisible();
    m_preFullscreenUiState.infoDockVisible = m_infoDock != nullptr && m_infoDock->isVisible();

    captureLayoutSettings();
    m_fullscreenUiActive = true;
    hideFullscreenOverlayPanels();

    menuBar()->hide();
    if (m_mainToolBar != nullptr) {
        m_mainToolBar->hide();
    }
    statusBar()->hide();
    if (m_fileListDock != nullptr) {
        m_fileListDock->hide();
    }
    if (m_infoDock != nullptr) {
        m_infoDock->hide();
    }

    showFullScreen();
    updateFullscreenOverlayGeometry();
    syncFullscreenFileListSelection();
    updateFullscreenStatusText();
    updateFullscreenOverlayVisibility();
}

void MainWindow::exitFullscreenMode()
{
    hideFullscreenOverlayPanels();

    const bool shouldRestoreUi = m_fullscreenUiActive;
    m_fullscreenUiActive = false;
    showNormal();

    if (!shouldRestoreUi) {
        return;
    }

    menuBar()->setVisible(m_preFullscreenUiState.menuBarVisible);
    if (m_mainToolBar != nullptr) {
        m_mainToolBar->setVisible(m_preFullscreenUiState.toolBarVisible);
    }
    statusBar()->setVisible(m_preFullscreenUiState.statusBarVisible);
    if (m_fileListDock != nullptr) {
        m_fileListDock->setVisible(m_preFullscreenUiState.fileDockVisible);
    }
    if (m_infoDock != nullptr) {
        m_infoDock->setVisible(m_preFullscreenUiState.infoDockVisible);
    }
}

void MainWindow::hideFullscreenOverlayPanels()
{
    if (m_fullscreenLeftPanel != nullptr) {
        m_fullscreenLeftPanel->hide();
    }
    if (m_fullscreenRightPanel != nullptr) {
        m_fullscreenRightPanel->hide();
    }
    if (m_fullscreenTopPanel != nullptr) {
        m_fullscreenTopPanel->hide();
    }
    if (m_fullscreenBottomPanel != nullptr) {
        m_fullscreenBottomPanel->hide();
    }
}

void MainWindow::updateFullscreenOverlayGeometry()
{
    if (m_fullscreenLeftPanel == nullptr
        || m_fullscreenRightPanel == nullptr
        || m_fullscreenTopPanel == nullptr
        || m_fullscreenBottomPanel == nullptr) {
        return;
    }

    const QRect windowRect = rect();
    if (windowRect.isEmpty()) {
        return;
    }

    const int sideMaximum = std::max(240, windowRect.width() * 45 / 100);
    const int sidePanelWidth = std::min(std::max(260, windowRect.width() / 4), sideMaximum);
    const int topPanelHeight = std::max(40, m_fullscreenTopPanel->sizeHint().height());
    const int bottomPanelHeight = std::max(34, m_fullscreenBottomPanel->sizeHint().height());

    m_fullscreenLeftPanel->setGeometry(0, 0, sidePanelWidth, windowRect.height());
    m_fullscreenRightPanel->setGeometry(windowRect.width() - sidePanelWidth, 0, sidePanelWidth, windowRect.height());
    m_fullscreenTopPanel->setGeometry(0, 0, windowRect.width(), topPanelHeight);
    m_fullscreenBottomPanel->setGeometry(0, windowRect.height() - bottomPanelHeight, windowRect.width(), bottomPanelHeight);
}

void MainWindow::updateFullscreenOverlayVisibility()
{
    if (!m_fullscreenUiActive || !isFullScreen() || m_quickJumpDialogOpen) {
        hideFullscreenOverlayPanels();
        return;
    }

    updateFullscreenOverlayGeometry();

    const QPoint cursorPosition = mapFromGlobal(QCursor::pos());
    if (!rect().contains(cursorPosition)) {
        hideFullscreenOverlayPanels();
        return;
    }

    const int sideTriggerWidth = std::max(1, static_cast<int>(std::lround(width() * m_fullscreenOverlaySettings.sideTriggerRatio)));
    const int verticalTriggerHeight = std::max(1, static_cast<int>(std::lround(height() * m_fullscreenOverlaySettings.verticalTriggerRatio)));
    QWidget *targetPanel = nullptr;

    if (cursorPosition.x() <= sideTriggerWidth) {
        targetPanel = m_fullscreenLeftPanel;
    } else if (cursorPosition.x() >= width() - sideTriggerWidth) {
        targetPanel = m_fullscreenRightPanel;
    } else if (m_fullscreenLeftPanel->isVisible() && m_fullscreenLeftPanel->geometry().contains(cursorPosition)) {
        targetPanel = m_fullscreenLeftPanel;
    } else if (m_fullscreenRightPanel->isVisible() && m_fullscreenRightPanel->geometry().contains(cursorPosition)) {
        targetPanel = m_fullscreenRightPanel;
    } else if (cursorPosition.y() <= verticalTriggerHeight) {
        targetPanel = m_fullscreenTopPanel;
    } else if (cursorPosition.y() >= height() - verticalTriggerHeight) {
        targetPanel = m_fullscreenBottomPanel;
    } else if (m_fullscreenTopPanel->isVisible() && m_fullscreenTopPanel->geometry().contains(cursorPosition)) {
        targetPanel = m_fullscreenTopPanel;
    } else if (m_fullscreenBottomPanel->isVisible() && m_fullscreenBottomPanel->geometry().contains(cursorPosition)) {
        targetPanel = m_fullscreenBottomPanel;
    }

    const QList<QWidget *> panels {
        m_fullscreenLeftPanel,
        m_fullscreenRightPanel,
        m_fullscreenTopPanel,
        m_fullscreenBottomPanel,
    };
    for (QWidget *panel : panels) {
        const bool shouldShow = panel == targetPanel;
        if (panel->isVisible() != shouldShow) {
            panel->setVisible(shouldShow);
        }
        if (shouldShow) {
            panel->raise();
        }
    }
}

void MainWindow::syncFullscreenFileListSelection()
{
    if (m_fullscreenFileListView == nullptr) {
        return;
    }

    QItemSelectionModel *selectionModel = m_fullscreenFileListView->selectionModel();
    if (selectionModel == nullptr) {
        return;
    }

    const QScopedValueRollback<bool> guard(m_syncingFileListSelection, true);
    const int currentIndex = m_controller.currentIndex();
    if (currentIndex < 0 || currentIndex >= m_fileListModel->rowCount()) {
        selectionModel->clear();
        m_fullscreenFileListView->setCurrentIndex(QModelIndex());
        return;
    }

    const QModelIndex modelIndex = m_fileListModel->index(currentIndex, 0);
    selectionModel->setCurrentIndex(
        modelIndex,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current | QItemSelectionModel::Rows);
    m_fullscreenFileListView->scrollTo(modelIndex, QAbstractItemView::EnsureVisible);
}

void MainWindow::updateFullscreenStatusText()
{
    if (m_fullscreenStatusLabel != nullptr && m_statusLabel != nullptr) {
        m_fullscreenStatusLabel->setText(m_statusLabel->text());
    }
}

core::DecodeRequest MainWindow::currentDecodeRequest() const
{
    core::DecodeRequest request;
    request.viewportSize = m_imageView->size();
    request.rotationDegrees = m_controller.rotationDegrees();
    request.fitToWindow = m_controller.zoomMode() == core::ZoomMode::FitToWindow;
    request.exifAutorotate = m_settings.viewer.exifAutorotate;
    request.scaleFactor = m_controller.zoomMode() == core::ZoomMode::ActualSize ? 1.0 : m_controller.zoomFactor();
    return request;
}

QString MainWindow::currentCacheKey(const core::DecodeRequest &request) const
{
    const core::ImageEntry *entry = m_controller.currentEntry();
    return entry == nullptr ? QString() : core::cacheKeyFor(*entry, request);
}

QString MainWindow::imageDialogFilter() const
{
    return core::supportedOpenNameFilters().join(QStringLiteral(";;"));
}

} // namespace insidious::ui
