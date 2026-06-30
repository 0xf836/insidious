#pragma once

#include "core/decoded_image.h"
#include "core/folder_collection_source.h"
#include "core/image_sort.h"
#include "core/memory_image_cache.h"
#include "core/app_settings.h"
#include "core/viewer_controller.h"
#include "ui/collection_tree_model.h"
#include "ui/image_info_panel.h"
#include "ui/image_list_model.h"
#include "ui/image_view_widget.h"

#include <QByteArray>
#include <QFutureWatcher>
#include <QImage>
#include <QHash>
#include <QLabel>
#include <QMainWindow>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <Qt>

#include <memory>
#include <optional>

class QAction;
class QCloseEvent;
class QDockWidget;
class QListView;
class QModelIndex;
class QSplitter;
class QToolBar;
class QToolButton;
class QTreeView;

namespace insidious::ui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    void openPath(const QString &path);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void openFileDialog();
    void openFolderDialog();
    void loadCurrentImage();
    void showNextImage();
    void showPreviousImage();
    void showNextFolder();
    void showPreviousFolder();
    void showFirstImage();
    void showLastImage();
    void zoomIn();
    void zoomOut();
    void setFitToWindow();
    void setActualSize();
    void handleImageClick(const QPoint &position);
    void zoomToRect(const QRect &selectionRect);
    void rotateLeft();
    void rotateRight();
    void setExifAutorotateEnabled(bool enabled);
    void toggleFullscreen();
    void showQuickJumpDialog();
    void showSettingsDialog();
    void copyOriginalImageToClipboard();
    void handleWheelAction(Qt::KeyboardModifiers modifiers, int angleDeltaY, QPoint position);
    void handleMouseShortcut(Qt::KeyboardModifiers modifiers, Qt::MouseButton button, bool *handled);

private:
    struct DecodeJobResult {
        quint64 generation = 0;
        QString cacheKey;
        core::ImageEntry entry;
        core::DecodedImage decoded;
    };

    struct PendingDecodeRequest {
        quint64 generation = 0;
        QString cacheKey;
        core::ImageEntry entry;
        core::DecodeRequest request;
        QString archivePassword;
    };

    struct ScanJobResult {
        quint64 generation = 0;
        QString selectedPath;
        core::CollectionTreeResult result;
    };

    struct FolderLoadJobResult {
        quint64 generation = 0;
        QString folderRelativePath;
        core::FolderLoadResult result;
    };

    struct CopyOriginalImageJobResult {
        quint64 generation = 0;
        core::ImageEntry entry;
        QByteArray data;
        QImage imageData;
        QString mimeType;
        QString errorMessage;

        [[nodiscard]] bool ok() const { return errorMessage.isEmpty() && !data.isEmpty() && !mimeType.isEmpty(); }
    };

    struct FullscreenOverlaySettings {
        double sideTriggerRatio = 0.07;
        double verticalTriggerRatio = 0.05;
    };

    struct FullscreenUiState {
        Qt::WindowStates windowState;
        bool menuBarVisible = true;
        bool toolBarVisible = true;
        bool statusBarVisible = true;
        bool fileDockVisible = true;
        bool infoDockVisible = false;
    };

    struct ViewRenderState {
        core::ZoomMode zoomMode = core::ZoomMode::FitToWindow;
        double zoomFactor = 1.0;
        QPointF panOffset;
        core::DecodedImage decoded;
    };

    struct PendingZoomRenderFallback {
        quint64 generation = 0;
        ViewRenderState state;
    };

    struct ActiveHoldZoom {
        ViewRenderState returnState;
    };

    void setupActions();
    void setupConnections();
    void showTransientMenuBar();
    void hideTransientMenuBar();
    void scheduleTransientMenuBarHide();
    void setupFileListPanel();
    void setupInfoPanel();
    void setupFullscreenOverlayPanels();
    void loadSettings();
    void saveSettings();
    void applySettings(const core::AppSettings &settings, bool reloadCurrentImage);
    void applyTheme();
    void captureLayoutSettings();
    void restoreLayoutSettings();
    void openFile(const QString &filePath);
    void openFolder(const QString &folderPath);
    void openArchive(const QString &archivePath);
    void startFolderScan(const QString &folderPath, const QString &selectedPath);
    void startArchiveScan(const QString &archivePath, const QString &password);
    void startFolderLoad(const QString &folderRelativePath, int selectedImageIndex = 0);
    void applyScanResult(const ScanJobResult &job);
    void applyFolderLoadResult(const FolderLoadJobResult &job);
    bool promptArchivePassword(const QString &archivePath, bool retry);
    void clearArchivePassword();
    void displayDecodedImage(const core::DecodedImage &decoded);
    void loadCurrentImage(bool showTransientLoading);
    void loadCurrentImage(bool showTransientLoading, std::optional<ViewRenderState> zoomFallback);
    void scheduleDelayedLoadingOverlay(quint64 generation);
    void cancelDelayedLoadingOverlay();
    void cancelDelayedLoadingOverlayForGeneration(quint64 generation);
    void startDecodeJob(const PendingDecodeRequest &request);
    bool startPendingDecodeJob();
    void clearPendingDecodeRequest();
    [[nodiscard]] bool isSameDecodeEntry(const PendingDecodeRequest &left, const PendingDecodeRequest &right) const;
    [[nodiscard]] ViewRenderState captureViewRenderState() const;
    void restoreViewRenderState(const ViewRenderState &state, const QString &overlayMessage);
    [[nodiscard]] bool handleZoomRenderError(const DecodeJobResult &job);
    void showError(const QString &message);
    void showImageLoadError(const core::ImageEntry &entry, const QString &reason, const QString &details = {});
    void updateStatus(const QString &message = {});
    void updateActionState();
    void updateInteractionState();
    void updateInfoPanelForCurrentImage(const QString &state = {});
    void scheduleFitReload();
    void scheduleRegionReload();
    void clearPrefetchJobs();
    void prefetchNeighbors(const core::DecodeRequest &request);
    void reloadAfterImageSwitch();
    void applyImageSwitchViewPolicy();
    void applyWheelAction(core::WheelAction action, int angleDeltaY, QPoint position);
    void applyCustomZoomWithFallback(double zoomFactor, QPointF panOffset);
    void applyCustomZoomWithFallback(double zoomFactor, QPointF panOffset, const ViewRenderState &fallback);
    bool customZoomForAnchor(QPointF anchorPosition, double multiplier, double *zoomFactor, QPointF *panOffset) const;
    void handleHoldZoomStarted(QPoint position);
    void handleHoldZoomMoved(QPoint delta);
    void handleHoldZoomEnded();
    void zoomByMultiplier(double multiplier, QPointF anchorPosition);
    void zoomInAt(QPointF anchorPosition);
    void zoomOutAt(QPointF anchorPosition);
    [[nodiscard]] core::WheelAction wheelActionForModifiers(Qt::KeyboardModifiers modifiers) const;
    void clearSourceTree();
    void activateFolder(
        const QString &folderRelativePath,
        int selectedImageIndex = 0,
        bool loadCurrent = true,
        const QString &selectedIdentityKey = {});
    bool navigateToAdjacentFolder(bool forward, int selectedImageIndex);
    [[nodiscard]] bool hasAdjacentFolder(bool forward) const;
    [[nodiscard]] QVector<core::ImageEntry> sortedImagesForFolder(
        const core::CollectionFolderNode &folder,
        int selectedImageIndex,
        const QString &selectedIdentityKey,
        int *sortedSelectedIndex) const;
    [[nodiscard]] QString identityKeyForSelectedPath(
        const core::CollectionFolderNode &folder,
        const QString &selectedPath) const;
    void applyImageSortMode(core::ImageSortMode mode);
    QToolButton *addToolbarButton(QToolBar *toolbar, QAction *action, const QString &label, bool fullscreen);
    void setupFileSortButton(QToolButton *button, bool fullscreen);
    void syncFileSortControls();
    void refreshFileList();
    void syncFileListSelection();
    void syncFolderTreeSelection();
    void selectImageFromFileList(const QModelIndex &index);
    void selectFolderFromTree(const QModelIndex &index);
    [[nodiscard]] core::CollectionFolderNode *folderNodeForPath(const QString &folderRelativePath) const;
    void rememberTemporaryZoomReturnMode();
    bool restoreTemporaryZoomReturnMode();
    void enterFullscreenMode();
    void exitFullscreenMode();
    void refreshNormalLayoutAfterFullscreen();
    void hideFullscreenOverlayPanels();
    void updateFullscreenOverlayGeometry();
    void updateFullscreenOverlayVisibility();
    void syncFullscreenFileListSelection();
    void updateFullscreenStatusText();
    void applyFullscreenInfoLineSettings();
    void updateFullscreenInfoLine();
    [[nodiscard]] core::DecodeRequest currentDecodeRequest() const;
    [[nodiscard]] QString currentCacheKey(const core::DecodeRequest &request) const;
    [[nodiscard]] QString imageDialogFilter() const;
    [[nodiscard]] QAction *actionForShortcutCommand(core::ShortcutCommand command) const;
    void applyShortcutSettings();
    [[nodiscard]] bool triggerMouseShortcut(Qt::KeyboardModifiers modifiers, Qt::MouseButton button);

    ImageViewWidget *m_imageView = nullptr;
    QDockWidget *m_fileListDock = nullptr;
    QSplitter *m_filePanelSplitter = nullptr;
    QTreeView *m_fileTreeView = nullptr;
    QListView *m_fileListView = nullptr;
    CollectionTreeModel *m_fileTreeModel = nullptr;
    ImageListModel *m_fileListModel = nullptr;
    QDockWidget *m_infoDock = nullptr;
    ImageInfoPanel *m_infoPanel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QToolBar *m_mainToolBar = nullptr;
    QWidget *m_fullscreenLeftPanel = nullptr;
    QWidget *m_fullscreenRightPanel = nullptr;
    QToolBar *m_fullscreenTopPanel = nullptr;
    QToolButton *m_fullscreenSortingButton = nullptr;
    QWidget *m_fullscreenBottomPanel = nullptr;
    QSplitter *m_fullscreenFilePanelSplitter = nullptr;
    QTreeView *m_fullscreenFileTreeView = nullptr;
    QListView *m_fullscreenFileListView = nullptr;
    ImageInfoPanel *m_fullscreenInfoPanel = nullptr;
    QLabel *m_fullscreenStatusLabel = nullptr;
    QLabel *m_fullscreenInfoLineLabel = nullptr;

    QAction *m_openFileAction = nullptr;
    QAction *m_openFolderAction = nullptr;
    QAction *m_previousAction = nullptr;
    QAction *m_nextAction = nullptr;
    QAction *m_previousFolderAction = nullptr;
    QAction *m_nextFolderAction = nullptr;
    QAction *m_firstAction = nullptr;
    QAction *m_lastAction = nullptr;
    QAction *m_quickJumpAction = nullptr;
    QAction *m_copyOriginalImageAction = nullptr;
    QAction *m_preferencesAction = nullptr;
    QToolButton *m_sortingButton = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
    QAction *m_fitAction = nullptr;
    QAction *m_actualSizeAction = nullptr;
    QAction *m_rotateLeftAction = nullptr;
    QAction *m_rotateRightAction = nullptr;
    QAction *m_exifAutorotateAction = nullptr;
    QAction *m_fullscreenAction = nullptr;
    bool m_menuBarShownByAlt = false;

    core::ViewerController m_controller;
    core::DecodedImage m_currentDecoded;
    core::ZoomMode m_baseZoomMode = core::ZoomMode::FitToWindow;
    core::ImageSwitchPolicy m_imageSwitchPolicy = core::ImageSwitchPolicy::ResetToBaseMode;
    core::ClickZoomBehavior m_clickZoomBehavior = core::ClickZoomBehavior::ToggleOnClick;
    core::ImageSortMode m_imageSortMode = core::defaultImageSortMode();
    std::optional<core::ZoomMode> m_temporaryZoomReturnMode;
    core::AppSettings m_settings;
    double m_clickZoomMultiplier = 2.0;
    double m_zoomStepMultiplier = 1.25;
    std::shared_ptr<core::MemoryImageCache> m_cache;
    QFutureWatcher<DecodeJobResult> m_decodeWatcher;
    QFutureWatcher<ScanJobResult> m_scanWatcher;
    QFutureWatcher<FolderLoadJobResult> m_folderLoadWatcher;
    QFutureWatcher<CopyOriginalImageJobResult> m_copyOriginalImageWatcher;
    QHash<QString, QFutureWatcher<void> *> m_prefetchWatchers;
    QTimer m_resizeReloadTimer;
    QTimer m_regionReloadTimer;
    QTimer m_loadingOverlayTimer;
    quint64 m_sourceGeneration = 0;
    quint64 m_decodeGeneration = 0;
    quint64 m_loadingOverlayGeneration = 0;
    quint64 m_copyOriginalImageGeneration = 0;
    std::optional<PendingDecodeRequest> m_activeDecodeRequest;
    std::optional<PendingDecodeRequest> m_pendingDecodeRequest;
    std::optional<PendingZoomRenderFallback> m_pendingZoomRenderFallback;
    std::optional<ActiveHoldZoom> m_activeHoldZoom;
    bool m_imageRenderPending = false;
    bool m_syncingFileListSelection = false;
    bool m_syncingFolderTreeSelection = false;
    bool m_quickJumpDialogOpen = false;
    QString m_currentArchivePath;
    QString m_currentArchivePassword;
    std::shared_ptr<core::CollectionFolderNode> m_sourceRoot;
    QString m_sourceRootPath;
    QString m_activeFolderPath;
    QString m_loadingFolderPath;
    int m_loadingFolderSelectedIndex = 0;
    FullscreenOverlaySettings m_fullscreenOverlaySettings;
    FullscreenUiState m_preFullscreenUiState;
    bool m_fullscreenUiActive = false;
};

} // namespace insidious::ui
