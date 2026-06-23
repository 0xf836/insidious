#include "ui/main_window.h"

#include "core/archive_collection_source.h"
#include "core/image_entry_validation.h"
#include "core/image_formats.h"
#include "core/vips_image_decoder.h"
#include "ui/quick_jump_dialog.h"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
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
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
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
    setupFullscreenOverlayPanels();
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

void MainWindow::setupActions()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
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
    leftLayout->addWidget(m_fullscreenFileListView, 1);

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
    clearArchivePassword();

    ++m_sourceGeneration;
    m_temporaryZoomReturnMode.reset();
    m_baseZoomMode = core::ZoomMode::FitToWindow;
    m_cache->clear();
    m_controller.clearCollection();
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
    m_baseZoomMode = core::ZoomMode::FitToWindow;
    m_cache->clear();
    m_controller.clearCollection();
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
        job.result = core::scanFolder(folderPath, selectedPath);
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
        job.result = core::scanArchive(archivePath, password);
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
                                 .arg(entry->displayName,
                                      entry->formatHint,
                                      formattedFileSize(entry->fileSize),
                                      dimensionsText,
                                      zoomText,
                                      indexText);
            } else {
                statusText = tr("%1 · %2 · %3 · %4 · %5")
                                 .arg(entry->displayName,
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
    if (m_fullscreenFileListView != nullptr) {
        m_fullscreenFileListView->setEnabled(m_controller.hasCollection());
    }
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
