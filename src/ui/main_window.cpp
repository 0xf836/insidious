#include "ui/main_window.h"

#include "core/image_entry_validation.h"
#include "core/image_formats.h"
#include "core/vips_image_decoder.h"
#include "ui/quick_jump_dialog.h"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QListView>
#include <QLocale>
#include <QMenuBar>
#include <QMimeData>
#include <QScopedValueRollback>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace insidious::ui {
namespace {

constexpr double DefaultClickZoomMultiplier = 2.0;
constexpr double ZoomStepMultiplier = 1.25;
constexpr double MinZoomFactor = 0.05;
constexpr double MaxZoomFactor = 16.0;

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
    setupConnections();
    refreshFileList();
    updateInfoPanelForCurrentImage();

    m_resizeReloadTimer.setSingleShot(true);
    m_resizeReloadTimer.setInterval(120);
    connect(&m_resizeReloadTimer, &QTimer::timeout, this, [this]() {
        loadCurrentImage();
    });

    resize(1100, 760);
    updateStatus();
    updateActionState();
    updateInteractionState();
}

void MainWindow::openPath(const QString &path)
{
    const QFileInfo info(path);
    if (info.isDir()) {
        openFolder(info.absoluteFilePath());
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
    if (info.isDir() || core::isSupportedImageFile(info)) {
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

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    scheduleFitReload();
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        showNormal();
        event->accept();
        return;
    }

    QMainWindow::keyPressEvent(event);
}

void MainWindow::openFileDialog()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open image"),
        QString(),
        imageDialogFilter(),
        nullptr,
        QFileDialog::DontUseNativeDialog);
    if (!filePath.isEmpty()) {
        openFile(filePath);
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
    const core::ImageEntry *entry = m_controller.currentEntry();
    if (entry == nullptr) {
        updateActionState();
        updateInfoPanelForCurrentImage();
        return;
    }

    const core::EntryValidationResult validation = core::validateImageEntryLoadable(*entry);
    if (!validation.ok()) {
        ++m_decodeGeneration;
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
        displayDecodedImage(cached);
        prefetchNeighbors(request);
        return;
    }

    const quint64 generation = ++m_decodeGeneration;
    const bool hasDisplayedImage = m_imageView->hasImage();
    m_currentDecoded = {};
    updateInfoPanelForCurrentImage(tr("Loading..."));
    m_imageView->setImageInteractionEnabled(false);
    if (hasDisplayedImage && showTransientLoading) {
        m_imageView->setOverlayMessage(tr("Loading image..."));
        updateStatus(tr("Loading image..."));
    } else if (!hasDisplayedImage) {
        m_imageView->setMessage(tr("Loading image..."));
        updateStatus(tr("Loading image..."));
    }

    if (m_decodeWatcher.isRunning()) {
        m_decodeWatcher.cancel();
    }

    const core::ImageEntry entryCopy = *entry;
    m_decodeWatcher.setFuture(QtConcurrent::run([entryCopy, request, generation, cacheKey]() {
        core::VipsImageDecoder decoder;
        DecodeJobResult job;
        job.generation = generation;
        job.cacheKey = cacheKey;
        job.entry = entryCopy;
        job.decoded = decoder.decode(entryCopy, request);
        return job;
    }));
}

void MainWindow::showNextImage()
{
    if (!m_controller.hasCollection()) {
        return;
    }

    m_controller.goNext();
    reloadAfterImageSwitch();
}

void MainWindow::showPreviousImage()
{
    if (!m_controller.hasCollection()) {
        return;
    }

    m_controller.goPrevious();
    reloadAfterImageSwitch();
}

void MainWindow::showFirstImage()
{
    if (m_controller.goFirst()) {
        syncFileListSelection();
        loadCurrentImage();
    }
}

void MainWindow::showLastImage()
{
    if (m_controller.goLast()) {
        syncFileListSelection();
        loadCurrentImage();
    }
}

void MainWindow::zoomIn()
{
    if (m_currentDecoded.ok() && m_currentDecoded.effectiveScale > 0.0) {
        m_controller.setCustomZoom(m_currentDecoded.effectiveScale * ZoomStepMultiplier);
    } else {
        m_controller.zoomIn();
    }
    m_controller.resetPan();
    updateInteractionState();
    loadCurrentImage(false);
}

void MainWindow::zoomOut()
{
    if (m_currentDecoded.ok() && m_currentDecoded.effectiveScale > 0.0) {
        m_controller.setCustomZoom(m_currentDecoded.effectiveScale / ZoomStepMultiplier);
    } else {
        m_controller.zoomOut();
    }
    m_controller.resetPan();
    updateInteractionState();
    loadCurrentImage(false);
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
    if (!m_controller.hasCollection() || !m_currentDecoded.ok() || m_currentDecoded.effectiveScale <= 0.0) {
        return;
    }

    if (m_controller.zoomMode() == core::ZoomMode::Custom && restoreTemporaryZoomReturnMode()) {
        return;
    }

    rememberTemporaryZoomReturnMode();

    const QRectF imageRect = m_imageView->imageRect();
    if (imageRect.isEmpty()) {
        return;
    }

    const double currentScale = m_currentDecoded.effectiveScale;
    const QPointF imagePoint = clampedPointToRect(QPointF(position), imageRect) - imageRect.topLeft();
    const QPointF sourcePoint(imagePoint.x() / currentScale, imagePoint.y() / currentScale);
    const double newScale = std::clamp(currentScale * DefaultClickZoomMultiplier, MinZoomFactor, MaxZoomFactor);
    const QSizeF newDisplaySize(
        m_currentDecoded.image.width() * newScale / currentScale,
        m_currentDecoded.image.height() * newScale / currentScale);
    const QPointF centeredTopLeft(
        (m_imageView->width() - newDisplaySize.width()) / 2.0,
        (m_imageView->height() - newDisplaySize.height()) / 2.0);
    const QPointF viewportCenter(m_imageView->width() / 2.0, m_imageView->height() / 2.0);
    const QPointF pointAtNewScale(sourcePoint.x() * newScale, sourcePoint.y() * newScale);
    const QPointF newPanOffset = viewportCenter - centeredTopLeft - pointAtNewScale;

    m_controller.setCustomZoom(newScale);
    m_controller.setPanOffset(newPanOffset);
    updateInteractionState();
    loadCurrentImage(false);
}

void MainWindow::zoomToRect(const QRect &selectionRect)
{
    if (!m_controller.hasCollection() || !m_currentDecoded.ok() || m_currentDecoded.effectiveScale <= 0.0) {
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
    const double newScale = std::clamp(currentScale * scaleMultiplier, MinZoomFactor, MaxZoomFactor);

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

    m_controller.setCustomZoom(newScale);
    m_controller.setPanOffset(newPanOffset);
    updateInteractionState();
    loadCurrentImage(false);
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

void MainWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void MainWindow::showQuickJumpDialog()
{
    if (!m_controller.hasCollection() || m_quickJumpDialogOpen) {
        return;
    }

    const QScopedValueRollback<bool> guard(m_quickJumpDialogOpen, true);
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

void MainWindow::setupActions()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    auto *navigateMenu = menuBar()->addMenu(tr("&Navigate"));
    auto *toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);

    m_openFileAction = new QAction(tr("Open File"), this);
    m_openFileAction->setShortcut(QKeySequence::Open);
    connect(m_openFileAction, &QAction::triggered, this, &MainWindow::openFileDialog);
    fileMenu->addAction(m_openFileAction);
    toolbar->addAction(m_openFileAction);

    m_openFolderAction = new QAction(tr("Open Folder"), this);
    m_openFolderAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(m_openFolderAction, &QAction::triggered, this, &MainWindow::openFolderDialog);
    fileMenu->addAction(m_openFolderAction);
    toolbar->addAction(m_openFolderAction);

    fileMenu->addSeparator();
    auto *quitAction = new QAction(tr("Quit"), this);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
    fileMenu->addAction(quitAction);

    toolbar->addSeparator();

    m_previousAction = new QAction(tr("Previous"), this);
    m_previousAction->setShortcuts(QList<QKeySequence> { QKeySequence(Qt::Key_Left), QKeySequence(Qt::Key_PageUp) });
    connect(m_previousAction, &QAction::triggered, this, &MainWindow::showPreviousImage);
    navigateMenu->addAction(m_previousAction);
    toolbar->addAction(m_previousAction);

    m_nextAction = new QAction(tr("Next"), this);
    m_nextAction->setShortcuts(QList<QKeySequence> { QKeySequence(Qt::Key_Right), QKeySequence(Qt::Key_PageDown) });
    connect(m_nextAction, &QAction::triggered, this, &MainWindow::showNextImage);
    navigateMenu->addAction(m_nextAction);
    toolbar->addAction(m_nextAction);

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

    toolbar->addSeparator();

    m_zoomInAction = new QAction(tr("Zoom In"), this);
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(m_zoomInAction, &QAction::triggered, this, &MainWindow::zoomIn);
    viewMenu->addAction(m_zoomInAction);
    toolbar->addAction(m_zoomInAction);

    m_zoomOutAction = new QAction(tr("Zoom Out"), this);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(m_zoomOutAction, &QAction::triggered, this, &MainWindow::zoomOut);
    viewMenu->addAction(m_zoomOutAction);
    toolbar->addAction(m_zoomOutAction);

    m_fitAction = new QAction(tr("Fit"), this);
    m_fitAction->setShortcut(QKeySequence(Qt::Key_0));
    connect(m_fitAction, &QAction::triggered, this, &MainWindow::setFitToWindow);
    viewMenu->addAction(m_fitAction);
    toolbar->addAction(m_fitAction);

    m_actualSizeAction = new QAction(tr("Actual Size"), this);
    m_actualSizeAction->setShortcut(QKeySequence(Qt::Key_1));
    connect(m_actualSizeAction, &QAction::triggered, this, &MainWindow::setActualSize);
    viewMenu->addAction(m_actualSizeAction);
    toolbar->addAction(m_actualSizeAction);

    m_rotateLeftAction = new QAction(tr("Rotate Left"), this);
    m_rotateLeftAction->setShortcut(QKeySequence(Qt::Key_BracketLeft));
    connect(m_rotateLeftAction, &QAction::triggered, this, &MainWindow::rotateLeft);
    viewMenu->addAction(m_rotateLeftAction);

    m_rotateRightAction = new QAction(tr("Rotate Right"), this);
    m_rotateRightAction->setShortcut(QKeySequence(Qt::Key_BracketRight));
    connect(m_rotateRightAction, &QAction::triggered, this, &MainWindow::rotateRight);
    viewMenu->addAction(m_rotateRightAction);

    m_fullscreenAction = new QAction(tr("Fullscreen"), this);
    m_fullscreenAction->setShortcut(QKeySequence(Qt::Key_F11));
    connect(m_fullscreenAction, &QAction::triggered, this, &MainWindow::toggleFullscreen);
    viewMenu->addAction(m_fullscreenAction);
    toolbar->addAction(m_fullscreenAction);

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
    connect(m_imageView, &ImageViewWidget::imageClicked, this, &MainWindow::handleImageClick);
    connect(m_imageView, &ImageViewWidget::zoomToRectRequested, this, &MainWindow::zoomToRect);
    connect(m_imageView, &ImageViewWidget::toggleFullscreenRequested, this, &MainWindow::toggleFullscreen);
    connect(m_imageView, &ImageViewWidget::panChanged, this, [this](QPointF offset) {
        m_controller.setPanOffset(offset);
    });
    connect(m_fileListView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current, const QModelIndex &) {
        if (m_syncingFileListSelection) {
            return;
        }
        selectImageFromFileList(current);
    });
    connect(m_fileListView, &QListView::activated, this, &MainWindow::selectImageFromFileList);

    connect(&m_decodeWatcher, &QFutureWatcher<DecodeJobResult>::finished, this, [this]() {
        const DecodeJobResult job = m_decodeWatcher.result();
        if (job.generation != m_decodeGeneration) {
            return;
        }
        if (!job.decoded.ok()) {
            showImageLoadError(job.entry, tr("Could not decode image."), job.decoded.errorMessage);
            return;
        }
        m_cache->insert(job.cacheKey, job.decoded);
        displayDecodedImage(job.decoded);
        prefetchNeighbors(currentDecodeRequest());
    });

    connect(&m_scanWatcher, &QFutureWatcher<ScanJobResult>::finished, this, [this]() {
        applyScanResult(m_scanWatcher.result());
    });
}

void MainWindow::setupFileListPanel()
{
    m_fileListModel = new ImageListModel(this);

    m_fileListView = new QListView(this);
    m_fileListView->setModel(m_fileListModel);
    m_fileListView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileListView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileListView->setAlternatingRowColors(true);
    m_fileListView->setUniformItemSizes(true);

    m_fileListDock = new QDockWidget(tr("Files"), this);
    m_fileListDock->setObjectName(QStringLiteral("fileListDock"));
    m_fileListDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_fileListDock->setWidget(m_fileListView);
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

void MainWindow::openFile(const QString &filePath)
{
    const core::ScanResult single = core::singleImageCollection(filePath);
    if (!single.ok()) {
        showError(single.errorMessage);
        return;
    }

    ++m_sourceGeneration;
    m_temporaryZoomReturnMode.reset();
    m_baseZoomMode = core::ZoomMode::FitToWindow;
    m_cache->clear();
    m_controller.setCollection(single.entries, single.selectedIndex);
    m_controller.setFitToWindow();
    refreshFileList();
    updateInfoPanelForCurrentImage();
    updateInteractionState();
    loadCurrentImage();

    const QFileInfo fileInfo(filePath);
    startFolderScan(fileInfo.absolutePath(), fileInfo.absoluteFilePath());
}

void MainWindow::openFolder(const QString &folderPath)
{
    ++m_sourceGeneration;
    m_temporaryZoomReturnMode.reset();
    m_baseZoomMode = core::ZoomMode::FitToWindow;
    m_cache->clear();
    m_controller.clearCollection();
    refreshFileList();
    m_infoPanel->showMessage(tr("Scanning folder..."));
    m_imageView->setMessage(tr("Scanning folder..."));
    updateStatus(tr("Scanning folder..."));
    updateInteractionState();
    startFolderScan(folderPath, QString());
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
        job.result = core::scanFolder(folderPath, selectedPath);
        return job;
    }));
}

void MainWindow::applyScanResult(const ScanJobResult &job)
{
    if (job.generation != m_sourceGeneration) {
        return;
    }

    if (!job.result.ok()) {
        if (!m_controller.hasCollection()) {
            showError(job.result.errorMessage);
        }
        return;
    }

    const QString currentPath = m_controller.currentEntry() == nullptr ? QString() : m_controller.currentEntry()->absolutePath;
    m_controller.setCollection(job.result.entries, job.result.selectedIndex);
    refreshFileList();

    if (currentPath.isEmpty() || currentPath != m_controller.currentEntry()->absolutePath) {
        loadCurrentImage();
    } else {
        updateInfoPanelForCurrentImage();
        updateStatus();
        updateActionState();
    }
}

void MainWindow::displayDecodedImage(const core::DecodedImage &decoded)
{
    m_currentDecoded = decoded;
    m_imageView->setImageInteractionEnabled(true);
    m_imageView->setImage(decoded.image);
    m_imageView->setPanOffset(m_controller.panOffset());
    updateInfoPanelForCurrentImage();
    updateStatus();
    updateActionState();
    updateInteractionState();
}

void MainWindow::showError(const QString &message)
{
    m_currentDecoded = {};
    const QString fallback = message.isEmpty() ? tr("Operation failed.") : message;
    m_imageView->setMessage(fallback);
    m_infoPanel->showMessage(fallback);
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
}

void MainWindow::updateStatus(const QString &message)
{
    if (!message.isEmpty()) {
        m_statusLabel->setText(message);
        return;
    }

    const core::ImageEntry *entry = m_controller.currentEntry();
    if (entry == nullptr) {
        m_statusLabel->setText(tr("No image loaded."));
        return;
    }

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
        m_statusLabel->setText(tr("%1 · %2 · %3 · %4 · %5 · %6")
                                   .arg(entry->displayName,
                                        entry->formatHint,
                                        formattedFileSize(entry->fileSize),
                                        dimensionsText,
                                        zoomText,
                                        indexText));
        return;
    }

    m_statusLabel->setText(tr("%1 · %2 · %3 · %4 · %5")
                               .arg(entry->displayName,
                                    entry->formatHint,
                                    formattedFileSize(entry->fileSize),
                                    zoomText,
                                    indexText));
}

void MainWindow::updateActionState()
{
    const bool hasImage = m_controller.hasCollection();
    const bool hasPrevious = hasImage && m_controller.currentIndex() > 0;
    const bool hasNext = hasImage && m_controller.currentIndex() + 1 < m_controller.count();

    m_previousAction->setEnabled(hasImage);
    m_nextAction->setEnabled(hasImage);
    m_firstAction->setEnabled(hasPrevious);
    m_lastAction->setEnabled(hasNext);
    m_quickJumpAction->setEnabled(hasImage);
    m_zoomInAction->setEnabled(hasImage);
    m_zoomOutAction->setEnabled(hasImage);
    m_fitAction->setEnabled(hasImage);
    m_actualSizeAction->setEnabled(hasImage);
    m_rotateLeftAction->setEnabled(hasImage);
    m_rotateRightAction->setEnabled(hasImage);
    m_fullscreenAction->setEnabled(hasImage);
}

void MainWindow::updateInteractionState()
{
    const bool panningEnabled = m_controller.hasCollection()
        && m_controller.zoomMode() != core::ZoomMode::FitToWindow;
    m_imageView->setPanningEnabled(panningEnabled);
}

void MainWindow::updateInfoPanelForCurrentImage(const QString &state)
{
    const core::ImageEntry *entry = m_controller.currentEntry();
    if (entry == nullptr) {
        m_infoPanel->showMessage(tr("No image loaded."));
        return;
    }

    if (state.isEmpty() && m_currentDecoded.ok()) {
        m_infoPanel->showDecodedImage(*entry, m_currentDecoded);
        return;
    }

    m_infoPanel->showEntry(*entry, state);
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

    for (int index : { currentIndex - 1, currentIndex + 1 }) {
        if (index < 0 || index >= entries.size()) {
            continue;
        }

        const core::ImageEntry entry = entries.at(index);
        const QString key = core::cacheKeyFor(entry, request);
        if (cache->contains(key)) {
            continue;
        }

        auto future = QtConcurrent::run([cache, entry, request, key]() {
            core::VipsImageDecoder decoder;
            const core::DecodedImage decoded = decoder.decode(entry, request);
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

    if (m_imageSwitchViewPolicy == ImageSwitchViewPolicy::ResetToBaseMode) {
        if (m_baseZoomMode == core::ZoomMode::ActualSize) {
            m_controller.setActualSize();
        } else {
            m_controller.setFitToWindow();
        }
        m_imageView->resetPan();
    }

    updateInteractionState();
}

void MainWindow::refreshFileList()
{
    m_fileListModel->setEntries(m_controller.entries());
    m_fileListView->setEnabled(m_controller.hasCollection());
    syncFileListSelection();
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
        return;
    }

    const QModelIndex modelIndex = m_fileListModel->index(currentIndex, 0);
    selectionModel->setCurrentIndex(
        modelIndex,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current | QItemSelectionModel::Rows);
    m_fileListView->scrollTo(modelIndex, QAbstractItemView::EnsureVisible);
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

core::DecodeRequest MainWindow::currentDecodeRequest() const
{
    core::DecodeRequest request;
    request.viewportSize = m_imageView->size();
    request.rotationDegrees = m_controller.rotationDegrees();
    request.fitToWindow = m_controller.zoomMode() == core::ZoomMode::FitToWindow;
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
    return core::supportedImageNameFilters().join(QStringLiteral(";;"));
}

} // namespace insidious::ui
