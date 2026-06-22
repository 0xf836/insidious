#pragma once

#include "core/decoded_image.h"
#include "core/folder_collection_source.h"
#include "core/memory_image_cache.h"
#include "core/viewer_controller.h"
#include "ui/image_view_widget.h"

#include <QFutureWatcher>
#include <QLabel>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>

#include <memory>

class QAction;

namespace insidious::ui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    void openPath(const QString &path);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void openFileDialog();
    void openFolderDialog();
    void loadCurrentImage();
    void showNextImage();
    void showPreviousImage();
    void showFirstImage();
    void showLastImage();
    void zoomIn();
    void zoomOut();
    void setFitToWindow();
    void setActualSize();
    void toggleZoom();
    void rotateLeft();
    void rotateRight();
    void toggleFullscreen();

private:
    struct DecodeJobResult {
        quint64 generation = 0;
        QString cacheKey;
        core::DecodedImage decoded;
    };

    struct ScanJobResult {
        quint64 generation = 0;
        QString selectedPath;
        core::ScanResult result;
    };

    void setupActions();
    void setupConnections();
    void openFile(const QString &filePath);
    void openFolder(const QString &folderPath);
    void startFolderScan(const QString &folderPath, const QString &selectedPath);
    void applyScanResult(const ScanJobResult &job);
    void displayDecodedImage(const core::DecodedImage &decoded);
    void showError(const QString &message);
    void updateStatus(const QString &message = {});
    void updateActionState();
    void scheduleFitReload();
    void prefetchNeighbors(const core::DecodeRequest &request);
    [[nodiscard]] core::DecodeRequest currentDecodeRequest() const;
    [[nodiscard]] QString currentCacheKey(const core::DecodeRequest &request) const;
    [[nodiscard]] QString imageDialogFilter() const;

    ImageViewWidget *m_imageView = nullptr;
    QLabel *m_statusLabel = nullptr;

    QAction *m_openFileAction = nullptr;
    QAction *m_openFolderAction = nullptr;
    QAction *m_previousAction = nullptr;
    QAction *m_nextAction = nullptr;
    QAction *m_firstAction = nullptr;
    QAction *m_lastAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
    QAction *m_fitAction = nullptr;
    QAction *m_actualSizeAction = nullptr;
    QAction *m_rotateLeftAction = nullptr;
    QAction *m_rotateRightAction = nullptr;
    QAction *m_fullscreenAction = nullptr;

    core::ViewerController m_controller;
    std::shared_ptr<core::MemoryImageCache> m_cache;
    QFutureWatcher<DecodeJobResult> m_decodeWatcher;
    QFutureWatcher<ScanJobResult> m_scanWatcher;
    QTimer m_resizeReloadTimer;
    quint64 m_sourceGeneration = 0;
    quint64 m_decodeGeneration = 0;
};

} // namespace insidious::ui
