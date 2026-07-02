#include "ui/main_window.h"

#include "core/archive_collection_source.h"
#include "core/image_entry_validation.h"
#include "core/image_formats.h"
#include "core/multi_collection_source.h"
#include "core/vips_image_decoder.h"
#include "ui/quick_jump_dialog.h"
#include "ui/settings_dialog.h"
#include "ui/status_line_formatter.h"
#include "ui/thumbnail_list_model.h"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QCursor>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFont>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLayout>
#include <QInputDialog>
#include <QImageReader>
#include <QListView>
#include <QLineEdit>
#include <QLocale>
#include <QMenuBar>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace insidious::ui {
namespace {

constexpr int FullscreenInfoLineMarginLeft = 16;
constexpr int FullscreenInfoLineMarginTop = 14;
constexpr int LoadingOverlayDelayMs = 150;
constexpr qint64 MaxOriginalClipboardBytes = 256LL * 1024LL * 1024LL;
constexpr int MainToolbarButtonHeight = 28;
constexpr int FullscreenToolbarButtonHeight = 30;
constexpr int ThumbnailImageSize = 160;
constexpr int ThumbnailGridWidth = 220;
constexpr int ThumbnailGridHeight = 218;
constexpr int ThumbnailNearbyRows = 12;
constexpr int MaxComparisonItems = 8;

QPalette darkApplicationPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#202124")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#f1f3f4")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#15171a")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#20242a")));
    palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#252a31")));
    palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#f1f3f4")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#f1f3f4")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#2b3036")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#f1f3f4")));
    palette.setColor(QPalette::BrightText, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#3d78d8")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::Link, QColor(QStringLiteral("#7fb1ff")));
    palette.setColor(QPalette::Mid, QColor(QStringLiteral("#3a414a")));
    palette.setColor(QPalette::Dark, QColor(QStringLiteral("#111317")));
    palette.setColor(QPalette::Light, QColor(QStringLiteral("#4a5563")));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(QStringLiteral("#858b94")));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(QStringLiteral("#858b94")));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(QStringLiteral("#858b94")));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(QStringLiteral("#303640")));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(QStringLiteral("#858b94")));
    return palette;
}

QString applicationStyleSheet(core::UiTheme theme)
{
    if (theme != core::UiTheme::Dark) {
        return {};
    }

    return QStringLiteral(
        "QToolTip {"
        "  color: #f1f3f4;"
        "  background-color: #252a31;"
        "  border: 1px solid #4a5563;"
        "  padding: 3px 6px;"
        "}"
        "QMenu {"
        "  color: #f1f3f4;"
        "  background-color: #20242a;"
        "  border: 1px solid #3a414a;"
        "}"
        "QMenu::item {"
        "  padding: 4px 24px 4px 22px;"
        "}"
        "QMenu::item:selected {"
        "  background-color: #315f9f;"
        "}"
        "QMenu::separator {"
        "  height: 1px;"
        "  background-color: #3a414a;"
        "  margin: 4px 8px;"
        "}"
        "QMenuBar::item:selected {"
        "  background-color: #2f3742;"
        "}"
        "QDockWidget::title {"
        "  background-color: #2a2f36;"
        "  color: #f1f3f4;"
        "  padding: 4px;"
        "  border-bottom: 1px solid #3a414a;"
        "}"
        "QTreeView, QListView, QTableWidget {"
        "  background-color: #171a1f;"
        "  alternate-background-color: #20242a;"
        "  color: #e8eaed;"
        "  selection-background-color: #315f9f;"
        "  selection-color: #ffffff;"
        "  border: 1px solid #3a414a;"
        "}"
        "QHeaderView::section {"
        "  background-color: #252a31;"
        "  color: #f1f3f4;"
        "  border: 0;"
        "  border-right: 1px solid #3a414a;"
        "  border-bottom: 1px solid #3a414a;"
        "  padding: 4px 6px;"
        "}"
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QFontComboBox {"
        "  background-color: #15171a;"
        "  color: #f1f3f4;"
        "  border: 1px solid #4a5563;"
        "  border-radius: 4px;"
        "  padding: 3px 6px;"
        "}"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QFontComboBox:focus {"
        "  border-color: #3d78d8;"
        "}"
        "QPushButton {"
        "  background-color: #2b3036;"
        "  color: #f1f3f4;"
        "  border: 1px solid #4a5563;"
        "  border-radius: 4px;"
        "  padding: 4px 10px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #343b44;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #263a5f;"
        "}"
        "QPushButton:disabled {"
        "  color: #858b94;"
        "  background-color: #252a31;"
        "}"
        "QTabWidget::pane, QFrame {"
        "  border-color: #3a414a;"
        "}"
        "QScrollBar:vertical, QScrollBar:horizontal {"
        "  background: #20242a;"
        "  margin: 1px;"
        "}"
        "QScrollBar:vertical {"
        "  width: 10px;"
        "}"
        "QScrollBar:horizontal {"
        "  height: 10px;"
        "}"
        "QScrollBar::handle:vertical, QScrollBar::handle:horizontal {"
        "  background: #4a5563;"
        "  border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  min-height: 24px;"
        "}"
        "QScrollBar::handle:horizontal {"
        "  min-width: 24px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0;"
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
        "  width: 0;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical,"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
        "  background: transparent;"
        "}");
}

struct OriginalImageReadResult {
    QByteArray data;
    QImage imageData;
    QString mimeType;
    QString errorMessage;
};

QString sortKeyLabel(core::ImageSortKey key)
{
    switch (key) {
    case core::ImageSortKey::Name:
        return QObject::tr("Name");
    case core::ImageSortKey::Type:
        return QObject::tr("Type");
    case core::ImageSortKey::Size:
        return QObject::tr("Size");
    case core::ImageSortKey::Date:
        return QObject::tr("Date");
    }
    return QObject::tr("Name");
}

QString sortModeLabel(core::ImageSortMode mode)
{
    return QStringLiteral("%1 %2").arg(
        sortKeyLabel(mode.key),
        mode.direction == core::SortDirection::Descending ? QStringLiteral("↓") : QStringLiteral("↑"));
}

QString sortModeMenuLabel(core::ImageSortMode mode)
{
    return QStringLiteral("%1\t%2").arg(
        sortKeyLabel(mode.key),
        mode.direction == core::SortDirection::Descending ? QStringLiteral("↓") : QStringLiteral("↑"));
}

void updateSortMenuSelection(QMenu *menu, core::ImageSortMode mode)
{
    if (menu == nullptr) {
        return;
    }

    for (QAction *action : menu->actions()) {
        if (!action->property("sortKey").isValid() || !action->property("sortDirection").isValid()) {
            continue;
        }
        const auto key = static_cast<core::ImageSortKey>(action->property("sortKey").toInt());
        const auto direction = static_cast<core::SortDirection>(action->property("sortDirection").toInt());
        const bool selected = key == mode.key && direction == mode.direction;
        QFont font = action->font();
        font.setBold(selected);
        action->setFont(font);
        action->setEnabled(!selected);
    }
}

class ThumbnailItemDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(ThumbnailGridWidth, ThumbnailGridHeight);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (painter == nullptr) {
            return;
        }

        painter->save();
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
        if (selected || hovered) {
            const QColor background = selected
                ? option.palette.color(QPalette::Highlight)
                : option.palette.color(QPalette::AlternateBase);
            painter->fillRect(option.rect.adjusted(2, 2, -2, -2), background);
        }

        const int margin = 8;
        const int textHeight = option.fontMetrics.height() + 8;
        const QRect imageArea = option.rect.adjusted(margin, margin, -margin, -(textHeight + margin));
        const QImage thumbnail = index.data(ThumbnailListModel::ThumbnailImageRole).value<QImage>();
        if (!thumbnail.isNull() && !imageArea.isEmpty()) {
            const QSize targetSize = thumbnail.size().scaled(imageArea.size(), Qt::KeepAspectRatio);
            const QRect targetRect(
                imageArea.x() + (imageArea.width() - targetSize.width()) / 2,
                imageArea.y() + (imageArea.height() - targetSize.height()) / 2,
                targetSize.width(),
                targetSize.height());
            painter->drawImage(targetRect, thumbnail);
        }

        const QRect textRect = option.rect.adjusted(margin, option.rect.height() - textHeight - margin / 2, -margin, -margin / 2);
        painter->setPen(selected
            ? option.palette.color(QPalette::HighlightedText)
            : option.palette.color(QPalette::Text));
        painter->drawText(
            textRect,
            Qt::AlignHCenter | Qt::AlignVCenter,
            option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width()));
        painter->restore();
    }
};

void configureThumbnailView(QListView *view, bool fullscreen)
{
    view->setViewMode(QListView::IconMode);
    view->setResizeMode(QListView::Adjust);
    view->setMovement(QListView::Static);
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setSelectionBehavior(QAbstractItemView::SelectItems);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setUniformItemSizes(true);
    view->setWordWrap(false);
    view->setTextElideMode(Qt::ElideRight);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setIconSize(QSize(ThumbnailImageSize, ThumbnailImageSize));
    view->setGridSize(QSize(ThumbnailGridWidth, ThumbnailGridHeight));
    view->setSpacing(fullscreen ? 4 : 6);
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    view->setItemDelegate(new ThumbnailItemDelegate(view));
}

QPointF clampedPointToRect(const QPointF &point, const QRectF &rect)
{
    return QPointF(
        std::clamp(point.x(), rect.left(), rect.right()),
        std::clamp(point.y(), rect.top(), rect.bottom()));
}

double decodedLogicalScale(const core::DecodedImage &decoded)
{
    if (decoded.effectiveScale <= 0.0) {
        return 0.0;
    }
    const double devicePixelRatio = std::isfinite(decoded.devicePixelRatio) && decoded.devicePixelRatio > 0.0
        ? decoded.devicePixelRatio
        : 1.0;
    return decoded.effectiveScale / devicePixelRatio;
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

QString joinPath(const QString &left, const QString &right)
{
    if (left.isEmpty()) {
        return right;
    }
    if (right.isEmpty()) {
        return left;
    }
    return left + QLatin1Char('/') + right;
}

enum class OpenPathKind {
    Invalid,
    Image,
    Folder,
    Archive,
};

OpenPathKind openPathKind(const QString &path)
{
    const QFileInfo info(path);
    if (info.isDir()) {
        return OpenPathKind::Folder;
    }
    if (core::isSupportedArchiveFile(info)) {
        return OpenPathKind::Archive;
    }
    if (core::isSupportedImageFile(info)) {
        return OpenPathKind::Image;
    }
    return OpenPathKind::Invalid;
}

QStringList localPathsFromUrls(const QList<QUrl> &urls)
{
    QStringList paths;
    paths.reserve(urls.size());
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            return {};
        }
        const QString path = url.toLocalFile();
        if (path.isEmpty()) {
            return {};
        }
        paths.append(path);
    }
    return paths;
}

bool classifyOpenPaths(const QStringList &paths, OpenPathKind *kind, QString *errorMessage)
{
    if (paths.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("No files, folders, or archives were selected.");
        }
        return false;
    }

    OpenPathKind firstKind = OpenPathKind::Invalid;
    for (const QString &path : paths) {
        const OpenPathKind currentKind = openPathKind(path);
        if (currentKind == OpenPathKind::Invalid) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("Unsupported dropped path: %1").arg(path);
            }
            return false;
        }
        if (firstKind == OpenPathKind::Invalid) {
            firstKind = currentKind;
            continue;
        }
        if (currentKind != firstKind) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("Drop or open images, folders, or archives as separate homogeneous groups.");
            }
            return false;
        }
    }

    if (kind != nullptr) {
        *kind = firstKind;
    }
    return true;
}

void rebaseLoadedFolderResult(
    const core::CollectionFolderNode *node,
    QVector<core::ImageEntry> *images,
    QVector<std::shared_ptr<core::CollectionFolderNode>> *children)
{
    if (node == nullptr || images == nullptr || children == nullptr || node->sourceTreePrefix.isEmpty()) {
        return;
    }

    for (core::ImageEntry &entry : *images) {
        entry.sourceFolderPath = node->relativePath;
        entry.sourceRelativePath = joinPath(node->relativePath, entry.displayName);
    }
    for (const auto &child : *children) {
        child->sourceTreePrefix = node->sourceTreePrefix;
        child->loadRootPath = node->loadRootPath;
        child->loadFolderRelativePath = child->relativePath;
        child->relativePath = joinPath(node->sourceTreePrefix, child->loadFolderRelativePath);
    }
}

QList<QKeySequence> keySequencesFromSettings(const QStringList &values)
{
    QList<QKeySequence> sequences;
    for (const QString &value : values) {
        if (core::shortcutSequenceIsMouseInput(value)) {
            continue;
        }
        const QKeySequence sequence = QKeySequence::fromString(value, QKeySequence::PortableText);
        if (!sequence.isEmpty()) {
            sequences.append(sequence);
        }
    }
    return sequences;
}

Qt::KeyboardModifiers shortcutRelevantModifiers(Qt::KeyboardModifiers modifiers)
{
    return modifiers & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
}

QImage decodedClipboardImage(const QByteArray &data)
{
    QBuffer buffer;
    buffer.setData(data);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return {};
    }

    QImageReader reader(&buffer);
    reader.setAutoTransform(false);
    return reader.read();
}

OriginalImageReadResult readOriginalImageBytes(const core::ImageEntry &entry, const QString &archivePassword)
{
    OriginalImageReadResult result;
    if (entry.source == core::ImageEntrySource::Archive) {
        const core::ArchiveEntryReadResult archiveResult = core::readArchiveEntry(entry, archivePassword, MaxOriginalClipboardBytes);
        if (!archiveResult.ok()) {
            result.errorMessage = archiveResult.errorMessage;
            return result;
        }
        result.data = archiveResult.data;
        result.mimeType = core::imageMimeTypeFromPath(entry.archiveEntryPath);
        result.imageData = decodedClipboardImage(result.data);
        return result;
    }

    const QFileInfo fileInfo(entry.absolutePath);
    if (!fileInfo.exists() || !fileInfo.isFile() || !fileInfo.isReadable()) {
        result.errorMessage = QObject::tr("Original image file is not readable.");
        return result;
    }
    if (fileInfo.size() > MaxOriginalClipboardBytes) {
        result.errorMessage = QObject::tr("Original image is too large to copy.");
        return result;
    }

    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = file.errorString();
        return result;
    }

    result.data = file.readAll();
    if (result.data.isEmpty()) {
        result.errorMessage = QObject::tr("Original image file is empty.");
        return result;
    }
    result.mimeType = core::imageMimeTypeFromPath(fileInfo.absoluteFilePath());
    result.imageData = decodedClipboardImage(result.data);
    return result;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_cache(std::make_shared<core::MemoryImageCache>())
{
    setWindowTitle(tr("Insidious"));
    setAcceptDrops(true);

    m_centralTabs = new QTabWidget(this);
    m_centralTabs->setDocumentMode(true);
    m_imageView = new ImageViewWidget(m_centralTabs);
    m_comparisonView = new ComparisonViewWidget(m_centralTabs);
    m_devicePixelRatio = devicePixelRatioF();
    m_imageView->setMessage(tr("Open an image or folder."));
    m_centralTabs->addTab(m_imageView, tr("Viewer"));
    m_centralTabs->addTab(m_comparisonView, tr("Compare"));
    setCentralWidget(m_centralTabs);
    updateComparisonTabTitle();

    statusBar()->setObjectName(QStringLiteral("mainStatusBar"));
    statusBar()->setSizeGripEnabled(false);
    statusBar()->setContentsMargins(0, 0, 0, 0);
    statusBar()->setAutoFillBackground(true);
    statusBar()->setAttribute(Qt::WA_StyledBackground, true);
    statusBar()->setStyleSheet(QStringLiteral(
        "QStatusBar#mainStatusBar {"
        "  background: palette(window);"
        "  color: palette(window-text);"
        "  border-top: 1px solid palette(mid);"
        "  border-bottom: 0;"
        "  margin: 0;"
        "  padding: 0;"
        "}"
        "QStatusBar#mainStatusBar::item {"
        "  border: 0;"
        "}"
        "QLabel#mainStatusLabel {"
        "  background: transparent;"
        "  color: palette(window-text);"
        "}"));

    m_statusLabel = new QLabel(statusBar());
    m_statusLabel->setObjectName(QStringLiteral("mainStatusLabel"));
    m_statusLabel->setTextFormat(Qt::RichText);
    m_statusLabel->setMargin(0);
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
        if (m_controller.hasCollection()
            && m_controller.zoomMode() != core::ZoomMode::FitToWindow
            && m_currentDecoded.ok()) {
            loadCurrentImage(false, captureViewRenderState());
            return;
        }
        loadCurrentImage();
    });

    m_regionReloadTimer.setSingleShot(true);
    m_regionReloadTimer.setInterval(120);
    connect(&m_regionReloadTimer, &QTimer::timeout, this, [this]() {
        if (!m_controller.hasCollection() || m_imageRenderPending || !m_currentDecoded.regionBased) {
            return;
        }
        loadCurrentImage(false, captureViewRenderState());
    });

    m_loadingOverlayTimer.setSingleShot(true);
    m_loadingOverlayTimer.setInterval(LoadingOverlayDelayMs);
    connect(&m_loadingOverlayTimer, &QTimer::timeout, this, [this]() {
        if (m_loadingOverlayGeneration == 0
            || m_loadingOverlayGeneration != m_decodeGeneration
            || !m_imageRenderPending
            || !m_imageView->hasImage()) {
            return;
        }
        m_imageView->setOverlayMessage(tr("Loading image..."));
        updateStatus(tr("Loading image..."));
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

void MainWindow::openPaths(const QStringList &paths)
{
    if (paths.size() == 1) {
        openPath(paths.first());
        return;
    }

    OpenPathKind kind = OpenPathKind::Invalid;
    QString errorMessage;
    if (!classifyOpenPaths(paths, &kind, &errorMessage)) {
        showError(errorMessage);
        return;
    }

    QStringList absolutePaths;
    absolutePaths.reserve(paths.size());
    for (const QString &path : paths) {
        absolutePaths.append(QFileInfo(path).absoluteFilePath());
    }

    switch (kind) {
    case OpenPathKind::Image:
        openFileList(absolutePaths);
        break;
    case OpenPathKind::Folder:
        openFolderList(absolutePaths);
        break;
    case OpenPathKind::Archive:
        openArchiveList(absolutePaths);
        break;
    case OpenPathKind::Invalid:
        showError(tr("Unsupported dropped paths."));
        break;
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (mimeData == nullptr || !mimeData->hasUrls()) {
        return;
    }

    OpenPathKind kind = OpenPathKind::Invalid;
    const QStringList paths = localPathsFromUrls(mimeData->urls());
    if (classifyOpenPaths(paths, &kind, nullptr)) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QStringList paths = localPathsFromUrls(event->mimeData()->urls());
    if (paths.isEmpty()) {
        showError(tr("Drop local image files, folders, or archives."));
        return;
    }

    openPaths(paths);
    event->acceptProposedAction();
}

bool MainWindow::event(QEvent *event)
{
    if (event->type() == QEvent::DevicePixelRatioChange) {
        handleDevicePixelRatioChanged();
    }
    return QMainWindow::event(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == menuBar()) {
        if (event->type() == QEvent::FocusOut) {
            scheduleTransientMenuBarHide();
        } else if (event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Escape && m_menuBarShownByAlt) {
                hideTransientMenuBar();
                event->accept();
                return true;
            }
        }
    }

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
    if (m_fullscreenUiActive && m_fullscreenContentMode == FullscreenContentMode::Viewer) {
        updateFullscreenOverlayGeometry();
        updateFullscreenOverlayVisibility();
    } else if (m_fullscreenUiActive || isFullScreen()) {
        hideFullscreenOverlayPanels();
    } else {
        hideFullscreenOverlayPanels();
    }
    if (!m_fullscreenUiActive || m_fullscreenContentMode != FullscreenContentMode::Compare) {
        scheduleFitReload();
    }
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

    if (event->key() == Qt::Key_Alt
        && !event->isAutoRepeat()
        && !isFullScreen()
        && !m_fullscreenUiActive) {
        showTransientMenuBar();
        event->accept();
        return;
    }

    QMainWindow::keyPressEvent(event);
}

void MainWindow::openFileDialog()
{
    const QStringList filePaths = QFileDialog::getOpenFileNames(
        this,
        tr("Open images or archives"),
        QString(),
        imageDialogFilter(),
        nullptr,
        QFileDialog::DontUseNativeDialog);
    if (!filePaths.isEmpty()) {
        openPaths(filePaths);
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
    m_regionReloadTimer.stop();
    cancelDelayedLoadingOverlay();
    if (!zoomFallback.has_value()) {
        m_activeHoldZoom.reset();
    }

    const core::ImageEntry *entry = m_controller.currentEntry();
    if (entry == nullptr) {
        m_imageRenderPending = false;
        clearPendingDecodeRequest();
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
        clearPendingDecodeRequest();
        m_pendingZoomRenderFallback.reset();
        if (m_decodeWatcher.isRunning()) {
            m_decodeWatcher.cancel();
        }
        showImageLoadError(*entry, validation.errorMessage);
        return;
    }

    const core::DecodeRequest request = currentDecodeRequest();
    const QString cacheKey = currentCacheKey(request);
    const quint64 generation = ++m_decodeGeneration;
    const bool cacheable = !request.regionRendering;

    core::DecodedImage cached;
    if (cacheable && m_cache->tryGet(cacheKey, &cached)) {
        m_imageRenderPending = false;
        clearPendingDecodeRequest();
        m_pendingZoomRenderFallback.reset();
        displayDecodedImage(cached);
        prefetchNeighbors(request);
        return;
    }

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
        m_imageView->setOverlayMessage(QString());
        scheduleDelayedLoadingOverlay(generation);
    } else if (hasDisplayedImage) {
        m_imageView->setOverlayMessage(QString());
    } else if (!hasDisplayedImage) {
        m_imageView->setMessage(tr("Loading image..."));
        updateStatus(tr("Loading image..."));
    }
    updateActionState();
    updateInteractionState();

    const core::ImageEntry entryCopy = *entry;
    const QString archivePassword = archivePasswordForEntry(entryCopy);
    const PendingDecodeRequest decodeRequest {
        generation,
        cacheKey,
        entryCopy,
        request,
        archivePassword,
    };

    if (m_decodeWatcher.isRunning()) {
        m_decodeWatcher.cancel();
        if (m_activeDecodeRequest.has_value()
            && isSameDecodeEntry(*m_activeDecodeRequest, decodeRequest)) {
            m_pendingDecodeRequest = decodeRequest;
            return;
        }

        clearPendingDecodeRequest();
        startDecodeJob(decodeRequest);
        return;
    }

    startDecodeJob(decodeRequest);
}

void MainWindow::startDecodeJob(const PendingDecodeRequest &request)
{
    m_activeDecodeRequest = request;
    m_decodeWatcher.setFuture(QtConcurrent::run([request]() {
        core::VipsImageDecoder decoder;
        DecodeJobResult job;
        job.generation = request.generation;
        job.cacheKey = request.cacheKey;
        job.entry = request.entry;
        job.decoded = decoder.decode(request.entry, request.request, request.archivePassword);
        return job;
    }));
}

void MainWindow::handleDevicePixelRatioChanged()
{
    const double devicePixelRatio = devicePixelRatioF();
    if (qFuzzyCompare(m_devicePixelRatio, devicePixelRatio)) {
        return;
    }

    m_devicePixelRatio = devicePixelRatio;
    if (!m_controller.hasCollection()) {
        return;
    }

    if (m_currentDecoded.ok() && m_controller.zoomMode() != core::ZoomMode::FitToWindow) {
        loadCurrentImage(false, captureViewRenderState());
        return;
    }

    loadCurrentImage(false);
}

void MainWindow::scheduleDelayedLoadingOverlay(quint64 generation)
{
    m_loadingOverlayGeneration = generation;
    m_loadingOverlayTimer.start();
}

void MainWindow::cancelDelayedLoadingOverlay()
{
    m_loadingOverlayTimer.stop();
    m_loadingOverlayGeneration = 0;
}

void MainWindow::cancelDelayedLoadingOverlayForGeneration(quint64 generation)
{
    if (generation == 0 || m_loadingOverlayGeneration != generation) {
        return;
    }
    cancelDelayedLoadingOverlay();
}

bool MainWindow::startPendingDecodeJob()
{
    if (!m_pendingDecodeRequest.has_value() || m_decodeWatcher.isRunning()) {
        return false;
    }

    const PendingDecodeRequest request = *m_pendingDecodeRequest;
    m_pendingDecodeRequest.reset();
    startDecodeJob(request);
    return true;
}

void MainWindow::clearPendingDecodeRequest()
{
    m_pendingDecodeRequest.reset();
}

bool MainWindow::isSameDecodeEntry(const PendingDecodeRequest &left, const PendingDecodeRequest &right) const
{
    return left.entry.identityKey() == right.entry.identityKey();
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
    if (navigateToAdjacentFolder(true, 0)) {
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
    if (navigateToAdjacentFolder(false, -1)) {
        return;
    }
    if (!m_controller.hasCollection()) {
        updateActionState();
        return;
    }
    reloadAfterImageSwitch();
}

void MainWindow::showNextFolder()
{
    if (navigateToAdjacentFolder(true, 0)) {
        return;
    }
    updateActionState();
}

void MainWindow::showPreviousFolder()
{
    if (navigateToAdjacentFolder(false, 0)) {
        return;
    }
    updateActionState();
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
    const double currentLogicalScale = decodedLogicalScale(m_currentDecoded);
    if (currentLogicalScale <= 0.0) {
        return;
    }
    const double scaleMultiplier = std::min(
        static_cast<double>(m_imageView->width()) / selected.width(),
        static_cast<double>(m_imageView->height()) / selected.height());
    const double newScale = std::clamp(currentScale * scaleMultiplier, core::MinimumZoomFactor, core::MaximumZoomFactor);
    const double newLogicalScale = currentLogicalScale * newScale / currentScale;

    const QPointF selectedCenterInImage = selected.center() - imageRect.topLeft();
    const QPointF sourceCenter(selectedCenterInImage.x() / currentLogicalScale, selectedCenterInImage.y() / currentLogicalScale);
    const QSize currentDisplaySize = m_currentDecoded.fullDisplaySize.isEmpty()
        ? m_currentDecoded.image.size()
        : m_currentDecoded.fullDisplaySize;
    const QSizeF newDisplaySize(
        currentDisplaySize.width() * newScale / currentScale,
        currentDisplaySize.height() * newScale / currentScale);
    const QPointF centeredTopLeft(
        (m_imageView->width() - newDisplaySize.width()) / 2.0,
        (m_imageView->height() - newDisplaySize.height()) / 2.0);
    const QPointF viewportCenter(m_imageView->width() / 2.0, m_imageView->height() / 2.0);
    const QPointF selectedCenterAtNewScale(sourceCenter.x() * newLogicalScale, sourceCenter.y() * newLogicalScale);
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
    } else if (!compareTabActive() || compareFullscreenAvailable()) {
        enterFullscreenMode();
    }
}

bool MainWindow::compareTabActive() const
{
    return m_centralTabs != nullptr
        && m_comparisonView != nullptr
        && m_centralTabs->currentWidget() == m_comparisonView;
}

bool MainWindow::compareFullscreenAvailable() const
{
    return compareTabActive()
        && m_comparisonView != nullptr
        && m_comparisonView->hasSession();
}

void MainWindow::setCentralTabBarVisible(bool visible)
{
    if (m_centralTabs == nullptr) {
        return;
    }
    if (QTabBar *tabBar = m_centralTabs->findChild<QTabBar *>()) {
        tabBar->setVisible(visible);
    }
}

void MainWindow::enterFullscreenMode()
{
    if (m_fullscreenUiActive) {
        return;
    }

    m_fullscreenContentMode = compareFullscreenAvailable()
        ? FullscreenContentMode::Compare
        : FullscreenContentMode::Viewer;

    if (m_fullscreenContentMode == FullscreenContentMode::Viewer
        && m_centralTabs != nullptr
        && m_imageView != nullptr) {
        m_centralTabs->setCurrentWidget(m_imageView);
    }

    hideTransientMenuBar();
    m_preFullscreenUiState.windowState = windowState() & ~Qt::WindowFullScreen;
    m_preFullscreenUiState.menuBarVisible = menuBar()->isVisible();
    m_preFullscreenUiState.toolBarVisible = m_mainToolBar != nullptr && m_mainToolBar->isVisible();
    m_preFullscreenUiState.statusBarVisible = statusBar()->isVisible();
    m_preFullscreenUiState.fileDockVisible = m_fileListDock != nullptr && m_fileListDock->isVisible();
    m_preFullscreenUiState.infoDockVisible = m_infoDock != nullptr && m_infoDock->isVisible();
    if (m_centralTabs != nullptr) {
        if (QTabBar *tabBar = m_centralTabs->findChild<QTabBar *>()) {
            m_preFullscreenUiState.centralTabBarVisible = tabBar->isVisible();
        }
    }

    captureLayoutSettings();
    m_fullscreenUiActive = true;
    hideFullscreenOverlayPanels();
    setCentralTabBarVisible(false);
    if (m_comparisonView != nullptr) {
        m_comparisonView->setFullscreenPresentation(m_fullscreenContentMode == FullscreenContentMode::Compare);
    }
    if (m_fullscreenAction != nullptr) {
        m_fullscreenAction->setText(tr("Exit Fullscreen"));
    }

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
    if (m_fullscreenContentMode == FullscreenContentMode::Viewer) {
        updateFullscreenOverlayGeometry();
        syncFullscreenFileListSelection();
        updateFullscreenStatusText();
        updateFullscreenInfoLine();
        updateFullscreenOverlayVisibility();
    } else {
        hideFullscreenOverlayPanels();
        updateFullscreenInfoLine();
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

void MainWindow::copyOriginalImageToClipboard()
{
    const core::ImageEntry *entry = m_controller.currentEntry();
    if (entry == nullptr || m_copyOriginalImageWatcher.isRunning()) {
        return;
    }

    const quint64 generation = ++m_copyOriginalImageGeneration;
    const core::ImageEntry entryCopy = *entry;
    const QString archivePassword = archivePasswordForEntry(entryCopy);

    m_copyOriginalImageWatcher.setFuture(QtConcurrent::run([generation, entryCopy, archivePassword]() {
        const OriginalImageReadResult readResult = readOriginalImageBytes(entryCopy, archivePassword);
        CopyOriginalImageJobResult job;
        job.generation = generation;
        job.entry = entryCopy;
        job.data = readResult.data;
        job.imageData = readResult.imageData;
        job.mimeType = readResult.mimeType;
        job.errorMessage = readResult.errorMessage;
        return job;
    }));
    updateActionState();
}

void MainWindow::handleWheelAction(Qt::KeyboardModifiers modifiers, int angleDeltaY, QPoint position)
{
    applyWheelAction(wheelActionForModifiers(modifiers), angleDeltaY, position);
}

void MainWindow::handleMouseShortcut(Qt::KeyboardModifiers modifiers, Qt::MouseButton button, bool *handled)
{
    if (handled == nullptr) {
        return;
    }
    *handled = triggerMouseShortcut(modifiers, button);
}

void MainWindow::setupActions()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    auto *navigateMenu = menuBar()->addMenu(tr("&Navigate"));
    menuBar()->installEventFilter(this);
    connect(menuBar(), &QMenuBar::triggered, this, [this]() {
        hideTransientMenuBar();
    });
    const QList<QMenu *> menus { fileMenu, editMenu, viewMenu, navigateMenu };
    for (QMenu *menu : menus) {
        connect(menu, &QMenu::aboutToHide, this, [this]() {
            scheduleTransientMenuBarHide();
        });
    }

    m_mainToolBar = addToolBar(tr("Main"));
    m_mainToolBar->setObjectName(QStringLiteral("mainToolBar"));
    m_mainToolBar->setMovable(false);
    m_mainToolBar->setFloatable(false);
    m_mainToolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_mainToolBar->setStyleSheet(QStringLiteral(
        "#mainToolBar {"
        "  spacing: 6px;"
        "  padding: 4px 6px;"
        "  border-bottom: 1px solid palette(mid);"
        "}"
        "#mainToolBar QToolButton {"
        "  min-height: 18px;"
        "  padding: 2px 6px;"
        "  border: 1px solid transparent;"
        "  border-radius: 4px;"
        "}"
        "#mainToolBar QToolButton:hover {"
        "  background: palette(alternate-base);"
        "  border-color: palette(mid);"
        "}"
        "#mainToolBar QToolButton:checked {"
        "  background: palette(highlight);"
        "  color: palette(highlighted-text);"
        "  border-color: palette(highlight);"
        "}"
        "#mainToolBar QToolButton:disabled {"
        "  color: palette(mid);"
        "}"));

    m_openFileAction = new QAction(tr("Open File"), this);
    connect(m_openFileAction, &QAction::triggered, this, &MainWindow::openFileDialog);
    fileMenu->addAction(m_openFileAction);
    addToolbarButton(m_mainToolBar, m_openFileAction, tr("Open"), false);

    m_openFolderAction = new QAction(tr("Open Folder"), this);
    connect(m_openFolderAction, &QAction::triggered, this, &MainWindow::openFolderDialog);
    fileMenu->addAction(m_openFolderAction);
    addToolbarButton(m_mainToolBar, m_openFolderAction, tr("Folder"), false);

    fileMenu->addSeparator();
    m_quitAction = new QAction(tr("Quit"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, qApp, &QApplication::quit);
    fileMenu->addAction(m_quitAction);

    m_mainToolBar->addSeparator();

    m_previousAction = new QAction(tr("Previous"), this);
    connect(m_previousAction, &QAction::triggered, this, &MainWindow::showPreviousImage);
    navigateMenu->addAction(m_previousAction);
    addToolbarButton(m_mainToolBar, m_previousAction, tr("Prev"), false);

    m_nextAction = new QAction(tr("Next"), this);
    connect(m_nextAction, &QAction::triggered, this, &MainWindow::showNextImage);
    navigateMenu->addAction(m_nextAction);
    addToolbarButton(m_mainToolBar, m_nextAction, tr("Next"), false);

    m_previousFolderAction = new QAction(tr("Previous Folder"), this);
    connect(m_previousFolderAction, &QAction::triggered, this, &MainWindow::showPreviousFolder);
    navigateMenu->addAction(m_previousFolderAction);
    addToolbarButton(m_mainToolBar, m_previousFolderAction, QStringLiteral("Folder ‹"), false);

    m_nextFolderAction = new QAction(tr("Next Folder"), this);
    connect(m_nextFolderAction, &QAction::triggered, this, &MainWindow::showNextFolder);
    navigateMenu->addAction(m_nextFolderAction);
    addToolbarButton(m_mainToolBar, m_nextFolderAction, QStringLiteral("Folder ›"), false);

    navigateMenu->addSeparator();
    m_mainToolBar->addSeparator();

    m_firstAction = new QAction(tr("First"), this);
    connect(m_firstAction, &QAction::triggered, this, &MainWindow::showFirstImage);
    navigateMenu->addAction(m_firstAction);
    addToolbarButton(m_mainToolBar, m_firstAction, tr("First"), false);

    m_lastAction = new QAction(tr("Last"), this);
    connect(m_lastAction, &QAction::triggered, this, &MainWindow::showLastImage);
    navigateMenu->addAction(m_lastAction);
    addToolbarButton(m_mainToolBar, m_lastAction, tr("Last"), false);

    m_quickJumpAction = new QAction(tr("Quick Jump"), this);
    m_quickJumpAction->setShortcutContext(Qt::WindowShortcut);
    connect(m_quickJumpAction, &QAction::triggered, this, &MainWindow::showQuickJumpDialog);
    navigateMenu->addAction(m_quickJumpAction);
    addToolbarButton(m_mainToolBar, m_quickJumpAction, tr("Find"), false);

    m_copyOriginalImageAction = new QAction(tr("Copy Original Image"), this);
    connect(m_copyOriginalImageAction, &QAction::triggered, this, &MainWindow::copyOriginalImageToClipboard);
    editMenu->addAction(m_copyOriginalImageAction);
    m_mainToolBar->addSeparator();
    addToolbarButton(m_mainToolBar, m_copyOriginalImageAction, tr("Copy"), false);

    editMenu->addSeparator();
    m_preferencesAction = new QAction(tr("Preferences"), this);
    connect(m_preferencesAction, &QAction::triggered, this, &MainWindow::showSettingsDialog);
    editMenu->addAction(m_preferencesAction);

    m_mainToolBar->addSeparator();

    m_zoomInAction = new QAction(tr("Zoom In"), this);
    connect(m_zoomInAction, &QAction::triggered, this, &MainWindow::zoomIn);
    viewMenu->addAction(m_zoomInAction);
    addToolbarButton(m_mainToolBar, m_zoomInAction, QStringLiteral("+"), false);

    m_zoomOutAction = new QAction(tr("Zoom Out"), this);
    connect(m_zoomOutAction, &QAction::triggered, this, &MainWindow::zoomOut);
    viewMenu->addAction(m_zoomOutAction);
    addToolbarButton(m_mainToolBar, m_zoomOutAction, QStringLiteral("−"), false);

    m_fitAction = new QAction(tr("Fit"), this);
    connect(m_fitAction, &QAction::triggered, this, &MainWindow::setFitToWindow);
    viewMenu->addAction(m_fitAction);
    addToolbarButton(m_mainToolBar, m_fitAction, tr("Fit"), false);

    m_actualSizeAction = new QAction(tr("Actual Size"), this);
    connect(m_actualSizeAction, &QAction::triggered, this, &MainWindow::setActualSize);
    viewMenu->addAction(m_actualSizeAction);
    addToolbarButton(m_mainToolBar, m_actualSizeAction, tr("1:1"), false);

    m_rotateLeftAction = new QAction(tr("Rotate Left"), this);
    connect(m_rotateLeftAction, &QAction::triggered, this, &MainWindow::rotateLeft);
    viewMenu->addAction(m_rotateLeftAction);
    addToolbarButton(m_mainToolBar, m_rotateLeftAction, QStringLiteral("↶"), false);

    m_rotateRightAction = new QAction(tr("Rotate Right"), this);
    connect(m_rotateRightAction, &QAction::triggered, this, &MainWindow::rotateRight);
    viewMenu->addAction(m_rotateRightAction);
    addToolbarButton(m_mainToolBar, m_rotateRightAction, QStringLiteral("↷"), false);

    m_exifAutorotateAction = new QAction(tr("Use EXIF Orientation"), this);
    m_exifAutorotateAction->setCheckable(true);
    connect(m_exifAutorotateAction, &QAction::toggled, this, &MainWindow::setExifAutorotateEnabled);
    viewMenu->addAction(m_exifAutorotateAction);

    m_fullscreenAction = new QAction(tr("Fullscreen"), this);
    connect(m_fullscreenAction, &QAction::triggered, this, &MainWindow::toggleFullscreen);
    viewMenu->addAction(m_fullscreenAction);

    addToolbarSpacer(m_mainToolBar);
    m_appMenuButton = addAppMenuButton(m_mainToolBar, false);

    viewMenu->addSeparator();
    QAction *fileListAction = m_fileListDock->toggleViewAction();
    fileListAction->setText(tr("Files"));
    viewMenu->addAction(fileListAction);

    QAction *infoAction = m_infoDock->toggleViewAction();
    infoAction->setText(tr("Info"));
    viewMenu->addAction(infoAction);

    applyShortcutSettings();
    menuBar()->hide();
}

void MainWindow::setupConnections()
{
    connect(m_imageView, &ImageViewWidget::navigatePreviousRequested, this, &MainWindow::showPreviousImage);
    connect(m_imageView, &ImageViewWidget::navigateNextRequested, this, &MainWindow::showNextImage);
    connect(m_imageView, &ImageViewWidget::zoomInRequested, this, &MainWindow::zoomIn);
    connect(m_imageView, &ImageViewWidget::zoomOutRequested, this, &MainWindow::zoomOut);
    connect(m_imageView, &ImageViewWidget::wheelActionRequested, this, &MainWindow::handleWheelAction);
    connect(m_imageView, &ImageViewWidget::mouseShortcutRequested, this, &MainWindow::handleMouseShortcut);
    connect(m_imageView, &ImageViewWidget::imageClicked, this, &MainWindow::handleImageClick);
    connect(m_imageView, &ImageViewWidget::holdZoomStarted, this, &MainWindow::handleHoldZoomStarted);
    connect(m_imageView, &ImageViewWidget::holdZoomMoved, this, &MainWindow::handleHoldZoomMoved);
    connect(m_imageView, &ImageViewWidget::holdZoomEnded, this, &MainWindow::handleHoldZoomEnded);
    connect(m_imageView, &ImageViewWidget::zoomToRectRequested, this, &MainWindow::zoomToRect);
    connect(m_imageView, &ImageViewWidget::toggleFullscreenRequested, this, &MainWindow::toggleFullscreen);
    connect(m_imageView, &ImageViewWidget::panChanged, this, [this](QPointF offset) {
        m_controller.setPanOffset(offset);
        scheduleRegionReload();
    });
    connect(m_centralTabs, &QTabWidget::currentChanged, this, [this]() {
        if (m_centralTabs->currentWidget() == m_comparisonView && !m_comparisonStatusMessage.isEmpty()) {
            updateStatus(m_comparisonStatusMessage);
        } else {
            updateStatus();
        }
        updateActionState();
    });
    connect(m_comparisonView, &ComparisonViewWidget::closeRequested, this, [this]() {
        updateComparisonTabTitle();
        if (m_centralTabs != nullptr) {
            m_centralTabs->setCurrentWidget(m_imageView);
        }
    });
    connect(m_comparisonView, &ComparisonViewWidget::statusMessageChanged, this, [this](const QString &message) {
        m_comparisonStatusMessage = message;
        if (m_centralTabs != nullptr && m_centralTabs->currentWidget() == m_comparisonView) {
            updateStatus(message);
        }
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
    connect(m_fileListView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](const QItemSelection &selected, const QItemSelection &deselected) {
        updateComparisonSelectionOrder(selected, deselected);
    });
    connect(m_fileListView, &QListView::activated, this, &MainWindow::selectImageFromFileList);
    connect(m_fileListView, &QListView::customContextMenuRequested, this, [this](const QPoint &position) {
        showComparisonContextMenu(m_fileListView, position);
    });
    connect(m_thumbnailListView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current, const QModelIndex &) {
        if (m_syncingFileListSelection) {
            return;
        }
        selectImageFromThumbnailList(current);
    });
    connect(m_thumbnailListView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](const QItemSelection &selected, const QItemSelection &deselected) {
        updateComparisonSelectionOrder(selected, deselected);
    });
    connect(m_thumbnailListView, &QListView::activated, this, &MainWindow::selectImageFromThumbnailList);
    connect(m_thumbnailListView, &QListView::customContextMenuRequested, this, [this](const QPoint &position) {
        showComparisonContextMenu(m_thumbnailListView, position);
    });
    connect(m_thumbnailListView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        requestVisibleThumbnails();
    });
    connect(m_thumbnailListView->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        requestVisibleThumbnails();
    });
    connect(m_filePanelTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (m_syncingFilePanelTabs) {
            return;
        }
        syncFilePanelMode(index == 1 ? FilePanelMode::Thumbnails : FilePanelMode::List);
    });
    connect(m_fileListDock, &QDockWidget::visibilityChanged, this, [this](bool) {
        updateThumbnailActivation();
    });
    connect(m_thumbnailLoader, &ThumbnailLoader::thumbnailReady, this, &MainWindow::handleThumbnailReady);
    connect(m_thumbnailLoader, &ThumbnailLoader::thumbnailFailed, this, &MainWindow::handleThumbnailFailed);

    connect(&m_decodeWatcher, &QFutureWatcher<DecodeJobResult>::finished, this, [this]() {
        if (m_decodeWatcher.isRunning()) {
            return;
        }

        const QFuture<DecodeJobResult> future = m_decodeWatcher.future();
        const quint64 finishedGeneration = m_activeDecodeRequest.has_value()
            ? m_activeDecodeRequest->generation
            : 0;
        m_activeDecodeRequest.reset();
        if (future.isCanceled() || future.resultCount() == 0 || !future.isResultReadyAt(0)) {
            if (startPendingDecodeJob()) {
                return;
            }
            cancelDelayedLoadingOverlayForGeneration(finishedGeneration);
            m_imageRenderPending = false;
            m_pendingZoomRenderFallback.reset();
            updateActionState();
            updateInteractionState();
            return;
        }

        const DecodeJobResult job = future.result();
        if (job.generation != m_decodeGeneration) {
            if (m_pendingZoomRenderFallback.has_value()
                && m_pendingZoomRenderFallback->generation == job.generation) {
                m_pendingZoomRenderFallback.reset();
            }
            if (startPendingDecodeJob()) {
                return;
            }
            cancelDelayedLoadingOverlayForGeneration(job.generation);
            m_imageRenderPending = false;
            updateActionState();
            updateInteractionState();
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
        if (!job.decoded.regionBased) {
            m_cache->insert(job.cacheKey, job.decoded);
        }
        displayDecodedImage(job.decoded);
        prefetchNeighbors(currentDecodeRequest());
    });

    connect(&m_scanWatcher, &QFutureWatcher<ScanJobResult>::finished, this, [this]() {
        applyScanResult(m_scanWatcher.result());
    });
    connect(&m_folderLoadWatcher, &QFutureWatcher<FolderLoadJobResult>::finished, this, [this]() {
        applyFolderLoadResult(m_folderLoadWatcher.result());
    });
    connect(&m_copyOriginalImageWatcher, &QFutureWatcher<CopyOriginalImageJobResult>::finished, this, [this]() {
        updateActionState();
        const QFuture<CopyOriginalImageJobResult> future = m_copyOriginalImageWatcher.future();
        if (future.isCanceled() || future.resultCount() == 0 || !future.isResultReadyAt(0)) {
            return;
        }

        const CopyOriginalImageJobResult job = future.result();
        const core::ImageEntry *currentEntry = m_controller.currentEntry();
        if (job.generation != m_copyOriginalImageGeneration
            || currentEntry == nullptr
            || currentEntry->identityKey() != job.entry.identityKey()) {
            return;
        }

        if (!job.ok()) {
            const QString message = job.errorMessage.isEmpty()
                ? tr("Could not copy original image.")
                : tr("Could not copy original image: %1").arg(job.errorMessage);
            m_imageView->setOverlayMessage(message);
            updateStatus(message);
            return;
        }

        auto *mimeData = new QMimeData;
        mimeData->setData(job.mimeType, job.data);
        if (!job.imageData.isNull()) {
            mimeData->setImageData(job.imageData);
        }
        QApplication::clipboard()->setMimeData(mimeData);
        const QString message = tr("Original image copied to clipboard.");
        m_imageView->setOverlayMessage(message);
        updateStatus(message);
    });
}

void MainWindow::showTransientMenuBar()
{
    QMenuBar *bar = menuBar();
    if (bar == nullptr) {
        return;
    }

    m_menuBarShownByAlt = true;
    bar->show();
    bar->raise();
    if (!bar->actions().isEmpty()) {
        bar->setActiveAction(bar->actions().constFirst());
    }
    bar->setFocus(Qt::MenuBarFocusReason);
}

void MainWindow::hideTransientMenuBar()
{
    if (!m_menuBarShownByAlt || isFullScreen() || m_fullscreenUiActive) {
        return;
    }

    QMenuBar *bar = menuBar();
    if (bar == nullptr) {
        return;
    }

    bar->setActiveAction(nullptr);
    bar->hide();
    m_menuBarShownByAlt = false;
    if (m_imageView != nullptr) {
        m_imageView->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::scheduleTransientMenuBarHide()
{
    if (!m_menuBarShownByAlt) {
        return;
    }

    QTimer::singleShot(0, this, [this]() {
        if (!m_menuBarShownByAlt) {
            return;
        }
        if (QApplication::activePopupWidget() != nullptr) {
            return;
        }
        hideTransientMenuBar();
    });
}

void MainWindow::setupFileListPanel()
{
    m_fileTreeModel = new CollectionTreeModel(this);
    m_fileListModel = new ImageListModel(this);
    m_thumbnailListModel = new ThumbnailListModel(this);
    m_thumbnailLoader = new ThumbnailLoader(this);

    m_fileTreeView = new QTreeView(this);
    m_fileTreeView->setModel(m_fileTreeModel);
    m_fileTreeView->setHeaderHidden(true);
    m_fileTreeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileTreeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileTreeView->setAnimated(false);
    m_fileTreeView->setUniformRowHeights(true);
    m_fileTreeView->setTextElideMode(Qt::ElideRight);
    m_fileTreeView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_fileListView = new QListView(this);
    m_fileListView->setModel(m_fileListModel);
    m_fileListView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileListView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileListView->setAlternatingRowColors(true);
    m_fileListView->setUniformItemSizes(true);
    m_fileListView->setTextElideMode(Qt::ElideRight);
    m_fileListView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fileListView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_thumbnailListView = new QListView(this);
    m_thumbnailListView->setModel(m_thumbnailListModel);
    configureThumbnailView(m_thumbnailListView, false);

    m_filePanelTabs = new QTabWidget(this);
    m_filePanelTabs->setDocumentMode(true);
    m_filePanelTabs->addTab(m_fileListView, tr("List"));
    m_filePanelTabs->addTab(m_thumbnailListView, tr("Thumbnails"));

    auto *fileListContainer = new QWidget(this);
    auto *fileListLayout = new QVBoxLayout(fileListContainer);
    fileListLayout->setContentsMargins(0, 0, 0, 0);
    fileListLayout->setSpacing(4);
    fileListLayout->addWidget(m_filePanelTabs, 1);

    m_filePanelSplitter = new QSplitter(Qt::Vertical, this);
    m_filePanelSplitter->addWidget(m_fileTreeView);
    m_filePanelSplitter->addWidget(fileListContainer);
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
        "#fullscreenLeftPanel QTabWidget::pane { border: none; }"
        "#fullscreenLeftPanel QTabBar::tab { color: rgba(245, 245, 245, 220); background: rgba(255, 255, 255, 18); padding: 4px 10px; margin-right: 2px; border-radius: 4px; }"
        "#fullscreenLeftPanel QTabBar::tab:selected { background: rgba(80, 150, 255, 96); color: rgba(255, 255, 255, 245); }"
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
    m_fullscreenFileTreeView->setTextElideMode(Qt::ElideRight);
    m_fullscreenFileTreeView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fullscreenFileTreeView->setAutoFillBackground(false);
    m_fullscreenFileTreeView->setMouseTracking(true);
    m_fullscreenFileTreeView->installEventFilter(this);
    m_fullscreenFileTreeView->viewport()->setAutoFillBackground(false);
    m_fullscreenFileTreeView->viewport()->setMouseTracking(true);
    m_fullscreenFileTreeView->viewport()->installEventFilter(this);
    m_fullscreenFilePanelSplitter->addWidget(m_fullscreenFileTreeView);

    auto *fullscreenFileListContainer = new QWidget(m_fullscreenLeftPanel);
    fullscreenFileListContainer->setAutoFillBackground(false);
    auto *fullscreenFileListLayout = new QVBoxLayout(fullscreenFileListContainer);
    fullscreenFileListLayout->setContentsMargins(0, 0, 0, 0);
    fullscreenFileListLayout->setSpacing(4);
    m_fullscreenFileListView = new QListView(m_fullscreenLeftPanel);
    m_fullscreenFileListView->setModel(m_fileListModel);
    m_fullscreenFileListView->setFrameShape(QFrame::NoFrame);
    m_fullscreenFileListView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fullscreenFileListView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fullscreenFileListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fullscreenFileListView->setAlternatingRowColors(false);
    m_fullscreenFileListView->setUniformItemSizes(true);
    m_fullscreenFileListView->setTextElideMode(Qt::ElideRight);
    m_fullscreenFileListView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fullscreenFileListView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_fullscreenFileListView->setSpacing(1);
    m_fullscreenFileListView->setAutoFillBackground(false);
    m_fullscreenFileListView->setMouseTracking(true);
    m_fullscreenFileListView->installEventFilter(this);
    m_fullscreenFileListView->viewport()->setAutoFillBackground(false);
    m_fullscreenFileListView->viewport()->setMouseTracking(true);
    m_fullscreenFileListView->viewport()->installEventFilter(this);

    m_fullscreenThumbnailListView = new QListView(m_fullscreenLeftPanel);
    m_fullscreenThumbnailListView->setModel(m_thumbnailListModel);
    m_fullscreenThumbnailListView->setFrameShape(QFrame::NoFrame);
    m_fullscreenThumbnailListView->setAutoFillBackground(false);
    m_fullscreenThumbnailListView->setMouseTracking(true);
    m_fullscreenThumbnailListView->installEventFilter(this);
    m_fullscreenThumbnailListView->viewport()->setAutoFillBackground(false);
    m_fullscreenThumbnailListView->viewport()->setMouseTracking(true);
    m_fullscreenThumbnailListView->viewport()->installEventFilter(this);
    m_fullscreenThumbnailListView->setContextMenuPolicy(Qt::CustomContextMenu);
    configureThumbnailView(m_fullscreenThumbnailListView, true);

    m_fullscreenFilePanelTabs = new QTabWidget(m_fullscreenLeftPanel);
    m_fullscreenFilePanelTabs->setDocumentMode(true);
    m_fullscreenFilePanelTabs->addTab(m_fullscreenFileListView, tr("List"));
    m_fullscreenFilePanelTabs->addTab(m_fullscreenThumbnailListView, tr("Thumbnails"));
    fullscreenFileListLayout->addWidget(m_fullscreenFilePanelTabs, 1);
    m_fullscreenFilePanelSplitter->addWidget(fullscreenFileListContainer);
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
    connect(m_fullscreenFileListView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](const QItemSelection &selected, const QItemSelection &deselected) {
        updateComparisonSelectionOrder(selected, deselected);
    });
    connect(m_fullscreenFileListView, &QListView::activated, this, &MainWindow::selectImageFromFileList);
    connect(m_fullscreenFileListView, &QListView::customContextMenuRequested, this, [this](const QPoint &position) {
        showComparisonContextMenu(m_fullscreenFileListView, position);
    });
    connect(m_fullscreenThumbnailListView->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current, const QModelIndex &) {
        if (m_syncingFileListSelection) {
            return;
        }
        selectImageFromThumbnailList(current);
    });
    connect(m_fullscreenThumbnailListView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](const QItemSelection &selected, const QItemSelection &deselected) {
        updateComparisonSelectionOrder(selected, deselected);
    });
    connect(m_fullscreenThumbnailListView, &QListView::activated, this, &MainWindow::selectImageFromThumbnailList);
    connect(m_fullscreenThumbnailListView, &QListView::customContextMenuRequested, this, [this](const QPoint &position) {
        showComparisonContextMenu(m_fullscreenThumbnailListView, position);
    });
    connect(m_fullscreenThumbnailListView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        requestVisibleThumbnails();
    });
    connect(m_fullscreenThumbnailListView->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        requestVisibleThumbnails();
    });
    connect(m_fullscreenFilePanelTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (m_syncingFilePanelTabs) {
            return;
        }
        syncFilePanelMode(index == 1 ? FilePanelMode::Thumbnails : FilePanelMode::List);
    });

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
        "#fullscreenTopPanel {"
        "  background-color: rgba(18, 18, 18, 232);"
        "  border-bottom: 1px solid rgba(255, 255, 255, 52);"
        "  spacing: 6px;"
        "  padding: 5px 8px;"
        "}"
        "#fullscreenTopPanel QToolButton {"
        "  color: rgba(245, 245, 245, 236);"
        "  background-color: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 4px;"
        "  padding: 2px 6px;"
        "  min-height: 18px;"
        "}"
        "#fullscreenTopPanel QToolButton:hover {"
        "  background-color: rgba(255, 255, 255, 36);"
        "  border-color: rgba(255, 255, 255, 64);"
        "}"
        "#fullscreenTopPanel QToolButton:checked {"
        "  background-color: rgba(80, 150, 255, 72);"
        "  border-color: rgba(160, 205, 255, 120);"
        "}"
        "#fullscreenTopPanel QToolButton:disabled {"
        "  color: rgba(245, 245, 245, 82);"
        "}"
        "#fullscreenTopPanel QToolButton::menu-indicator { image: none; width: 0; }"));
    addToolbarButton(m_fullscreenTopPanel, m_openFileAction, tr("Open"), true);
    addToolbarButton(m_fullscreenTopPanel, m_openFolderAction, tr("Folder"), true);
    m_fullscreenTopPanel->addSeparator();
    addToolbarButton(m_fullscreenTopPanel, m_previousAction, QStringLiteral("‹"), true);
    addToolbarButton(m_fullscreenTopPanel, m_nextAction, QStringLiteral("›"), true);
    addToolbarButton(m_fullscreenTopPanel, m_previousFolderAction, QStringLiteral("Folder ‹"), true);
    addToolbarButton(m_fullscreenTopPanel, m_nextFolderAction, QStringLiteral("Folder ›"), true);
    addToolbarButton(m_fullscreenTopPanel, m_firstAction, QStringLiteral("First"), true);
    addToolbarButton(m_fullscreenTopPanel, m_lastAction, QStringLiteral("Last"), true);
    addToolbarButton(m_fullscreenTopPanel, m_quickJumpAction, tr("Find"), true);
    m_fullscreenTopPanel->addSeparator();
    addToolbarButton(m_fullscreenTopPanel, m_copyOriginalImageAction, tr("Copy"), true);
    m_fullscreenTopPanel->addSeparator();
    addToolbarButton(m_fullscreenTopPanel, m_zoomInAction, QStringLiteral("+"), true);
    addToolbarButton(m_fullscreenTopPanel, m_zoomOutAction, QStringLiteral("−"), true);
    addToolbarButton(m_fullscreenTopPanel, m_fitAction, tr("Fit"), true);
    addToolbarButton(m_fullscreenTopPanel, m_actualSizeAction, QStringLiteral("1:1"), true);
    addToolbarButton(m_fullscreenTopPanel, m_rotateLeftAction, QStringLiteral("↶"), true);
    addToolbarButton(m_fullscreenTopPanel, m_rotateRightAction, QStringLiteral("↷"), true);
    m_fullscreenAppMenuButton = addAppMenuButton(m_fullscreenTopPanel, true);
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
    m_fullscreenStatusLabel->setTextFormat(Qt::RichText);
    m_fullscreenStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_fullscreenStatusLabel->setMouseTracking(true);
    m_fullscreenStatusLabel->installEventFilter(this);
    bottomLayout->addWidget(m_fullscreenStatusLabel, 1);

    m_fullscreenInfoLineLabel = new QLabel(this);
    m_fullscreenInfoLineLabel->setObjectName(QStringLiteral("fullscreenInfoLine"));
    m_fullscreenInfoLineLabel->setTextFormat(Qt::RichText);
    m_fullscreenInfoLineLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_fullscreenInfoLineLabel->setAutoFillBackground(false);
    m_fullscreenInfoLineLabel->hide();
    auto *shadow = new QGraphicsDropShadowEffect(m_fullscreenInfoLineLabel);
    shadow->setBlurRadius(5.0);
    shadow->setOffset(1.0, 1.0);
    shadow->setColor(QColor(0, 0, 0, 220));
    m_fullscreenInfoLineLabel->setGraphicsEffect(shadow);

    refreshFileList();
    updateInfoPanelForCurrentImage();
    updateFullscreenStatusText();
    applyFullscreenInfoLineSettings();
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
    const bool regionRenderingChanged = settings.viewer.regionRenderingEnabled != m_settings.viewer.regionRenderingEnabled;
    const bool scalingQualityChanged = settings.viewer.scalingQuality != m_settings.viewer.scalingQuality;
    const bool colorManagementChanged = settings.viewer.colorManagementEnabled != m_settings.viewer.colorManagementEnabled;

    m_settings = settings;
    applyTheme();
    applyShortcutSettings();
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
    applyFullscreenInfoLineSettings();
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
        } else if (exifAutorotateChanged || regionRenderingChanged || scalingQualityChanged || colorManagementChanged) {
            if (m_controller.zoomMode() == core::ZoomMode::FitToWindow) {
                loadCurrentImage(false);
            } else {
                loadCurrentImage(false, captureViewRenderState());
            }
        }
    }
    if (exifAutorotateChanged || colorManagementChanged) {
        resetThumbnailWork(true);
        updateThumbnailActivation();
    }

    updateFullscreenOverlayGeometry();
    updateFullscreenOverlayVisibility();
    updateFullscreenInfoLine();
    updateActionState();
}

void MainWindow::applyTheme()
{
    if (qApp == nullptr) {
        return;
    }

    if (m_settings.interface.theme == core::UiTheme::Dark) {
        qApp->setPalette(darkApplicationPalette());
    } else if (qApp->style() != nullptr) {
        qApp->setPalette(qApp->style()->standardPalette());
    }
    qApp->setStyleSheet(applicationStyleSheet(m_settings.interface.theme));
}

QAction *MainWindow::actionForShortcutCommand(core::ShortcutCommand command) const
{
    switch (command) {
    case core::ShortcutCommand::OpenFile:
        return m_openFileAction;
    case core::ShortcutCommand::OpenFolder:
        return m_openFolderAction;
    case core::ShortcutCommand::PreviousImage:
        return m_previousAction;
    case core::ShortcutCommand::NextImage:
        return m_nextAction;
    case core::ShortcutCommand::PreviousFolder:
        return m_previousFolderAction;
    case core::ShortcutCommand::NextFolder:
        return m_nextFolderAction;
    case core::ShortcutCommand::FirstImage:
        return m_firstAction;
    case core::ShortcutCommand::LastImage:
        return m_lastAction;
    case core::ShortcutCommand::QuickJump:
        return m_quickJumpAction;
    case core::ShortcutCommand::CopyOriginalImage:
        return m_copyOriginalImageAction;
    case core::ShortcutCommand::Preferences:
        return m_preferencesAction;
    case core::ShortcutCommand::ZoomIn:
        return m_zoomInAction;
    case core::ShortcutCommand::ZoomOut:
        return m_zoomOutAction;
    case core::ShortcutCommand::FitToWindow:
        return m_fitAction;
    case core::ShortcutCommand::ActualSize:
        return m_actualSizeAction;
    case core::ShortcutCommand::RotateLeft:
        return m_rotateLeftAction;
    case core::ShortcutCommand::RotateRight:
        return m_rotateRightAction;
    case core::ShortcutCommand::ExifAutorotate:
        return m_exifAutorotateAction;
    case core::ShortcutCommand::Fullscreen:
        return m_fullscreenAction;
    }
    return nullptr;
}

void MainWindow::applyShortcutSettings()
{
    for (core::ShortcutCommand command : core::shortcutCommands()) {
        QAction *action = actionForShortcutCommand(command);
        if (action == nullptr) {
            continue;
        }
        action->setShortcuts(keySequencesFromSettings(core::shortcutSequences(m_settings.shortcuts, command)));
    }
}

bool MainWindow::triggerMouseShortcut(Qt::KeyboardModifiers modifiers, Qt::MouseButton button)
{
    const Qt::KeyboardModifiers normalizedModifiers = shortcutRelevantModifiers(modifiers);
    for (core::ShortcutCommand command : core::shortcutCommands()) {
        QAction *action = actionForShortcutCommand(command);
        if (action == nullptr || !action->isEnabled()) {
            continue;
        }

        for (const QString &value : core::shortcutSequences(m_settings.shortcuts, command)) {
            core::MouseShortcut shortcut;
            if (!core::mouseShortcutFromSettingsString(value, &shortcut)) {
                continue;
            }
            if (shortcut.button == button && shortcut.modifiers == normalizedModifiers) {
                action->trigger();
                return true;
            }
        }
    }
    return false;
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
    clearArchivePasswords();

    const core::ScanResult single = core::singleImageCollection(filePath);
    if (!single.ok()) {
        showError(single.errorMessage);
        return;
    }

    ++m_sourceGeneration;
    ++m_decodeGeneration;
    clearPendingDecodeRequest();
    m_pendingZoomRenderFallback.reset();
    m_imageRenderPending = false;
    m_temporaryZoomReturnMode.reset();
    m_baseZoomMode = m_settings.viewer.defaultZoomMode;
    clearPrefetchJobs();
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
    clearArchivePasswords();

    ++m_sourceGeneration;
    ++m_decodeGeneration;
    clearPendingDecodeRequest();
    m_pendingZoomRenderFallback.reset();
    m_imageRenderPending = false;
    m_temporaryZoomReturnMode.reset();
    m_baseZoomMode = m_settings.viewer.defaultZoomMode;
    clearPrefetchJobs();
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
    clearArchivePasswords();
    const QString absoluteArchivePath = QFileInfo(archivePath).absoluteFilePath();

    ++m_sourceGeneration;
    ++m_decodeGeneration;
    clearPendingDecodeRequest();
    m_pendingZoomRenderFallback.reset();
    m_imageRenderPending = false;
    m_temporaryZoomReturnMode.reset();
    m_baseZoomMode = m_settings.viewer.defaultZoomMode;
    clearPrefetchJobs();
    m_cache->clear();
    m_controller.clearCollection();
    clearSourceTree();
    refreshFileList();
    m_infoPanel->showMessage(tr("Scanning archive..."));
    m_fullscreenInfoPanel->showMessage(tr("Scanning archive..."));
    m_imageView->setMessage(tr("Scanning archive..."));
    updateStatus(tr("Scanning archive..."));
    updateInteractionState();
    startArchiveScan(absoluteArchivePath, archivePasswordForPath(absoluteArchivePath));
}

void MainWindow::openFileList(const QStringList &filePaths)
{
    clearArchivePasswords();

    const core::CollectionTreeResult source = core::fileListSourceTree(filePaths);
    if (!source.ok()) {
        showError(source.errorMessage);
        return;
    }

    ++m_sourceGeneration;
    ++m_decodeGeneration;
    clearPendingDecodeRequest();
    m_pendingZoomRenderFallback.reset();
    m_imageRenderPending = false;
    m_temporaryZoomReturnMode.reset();
    m_baseZoomMode = m_settings.viewer.defaultZoomMode;
    clearPrefetchJobs();
    m_cache->clear();
    m_controller.clearCollection();
    clearSourceTree();

    m_sourceRoot = source.root;
    m_sourceRootPath = source.rootPath;
    m_fileTreeModel->setRoot(m_sourceRoot);
    rebuildSourceFolderTraversal();
    if (m_sourceRoot != nullptr) {
        m_fileTreeView->expand(m_fileTreeModel->indexForFolderPath(QString()));
        if (m_fullscreenFileTreeView != nullptr) {
            m_fullscreenFileTreeView->expand(m_fileTreeModel->indexForFolderPath(QString()));
        }
    }
    activateFolder(source.selectedFolderPath, source.selectedImageIndex, true);
}

void MainWindow::openFolderList(const QStringList &folderPaths)
{
    clearArchivePasswords();

    ++m_sourceGeneration;
    ++m_decodeGeneration;
    clearPendingDecodeRequest();
    m_pendingZoomRenderFallback.reset();
    m_imageRenderPending = false;
    m_temporaryZoomReturnMode.reset();
    m_baseZoomMode = m_settings.viewer.defaultZoomMode;
    clearPrefetchJobs();
    m_cache->clear();
    m_controller.clearCollection();
    clearSourceTree();
    refreshFileList();
    m_infoPanel->showMessage(tr("Scanning folders..."));
    m_fullscreenInfoPanel->showMessage(tr("Scanning folders..."));
    m_imageView->setMessage(tr("Scanning folders..."));
    updateStatus(tr("Scanning folders..."));
    updateInteractionState();
    startFolderListScan(folderPaths);
}

void MainWindow::openArchiveList(const QStringList &archivePaths)
{
    clearArchivePasswords();

    ++m_sourceGeneration;
    ++m_decodeGeneration;
    clearPendingDecodeRequest();
    m_pendingZoomRenderFallback.reset();
    m_imageRenderPending = false;
    m_temporaryZoomReturnMode.reset();
    m_baseZoomMode = m_settings.viewer.defaultZoomMode;
    clearPrefetchJobs();
    m_cache->clear();
    m_controller.clearCollection();
    clearSourceTree();
    refreshFileList();
    m_infoPanel->showMessage(tr("Scanning archives..."));
    m_fullscreenInfoPanel->showMessage(tr("Scanning archives..."));
    m_imageView->setMessage(tr("Scanning archives..."));
    updateStatus(tr("Scanning archives..."));
    updateInteractionState();
    startArchiveListScan(archivePaths, QString());
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

void MainWindow::startFolderListScan(const QStringList &folderPaths)
{
    const quint64 generation = m_sourceGeneration;
    if (m_scanWatcher.isRunning()) {
        m_scanWatcher.cancel();
    }

    m_scanWatcher.setFuture(QtConcurrent::run([folderPaths, generation]() {
        ScanJobResult job;
        job.generation = generation;
        job.selectedPaths = folderPaths;
        job.result = core::folderListSourceTree(folderPaths);
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
        job.selectedPaths = { archivePath };
        job.archivePassword = password;
        job.result = core::scanArchiveSourceTree(archivePath, password);
        return job;
    }));
}

void MainWindow::startArchiveListScan(const QStringList &archivePaths, const QString &password)
{
    const quint64 generation = m_sourceGeneration;
    if (m_scanWatcher.isRunning()) {
        m_scanWatcher.cancel();
    }

    m_scanWatcher.setFuture(QtConcurrent::run([archivePaths, password, generation]() {
        ScanJobResult job;
        job.generation = generation;
        job.selectedPaths = archivePaths;
        job.selectedPath = archivePaths.isEmpty() ? QString() : archivePaths.first();
        job.archivePassword = password;
        job.result = core::archiveListSourceTree(archivePaths, password);
        return job;
    }));
}

void MainWindow::startFolderLoad(const QString &folderRelativePath, int selectedImageIndex)
{
    core::CollectionFolderNode *node = folderNodeForPath(folderRelativePath);
    if (node == nullptr || node->loaded || !node->canLoadChildren) {
        return;
    }
    if (node->loadRootPath.isEmpty()) {
        return;
    }
    if (m_folderLoadWatcher.isRunning() && m_loadingFolderPath == folderRelativePath) {
        return;
    }

    const quint64 generation = m_sourceGeneration;
    if (m_folderLoadWatcher.isRunning()) {
        m_folderLoadWatcher.cancel();
    }

    const QString rootPath = node->loadRootPath;
    const QString loadFolderRelativePath = node->loadFolderRelativePath;
    m_loadingFolderPath = folderRelativePath;
    m_loadingFolderSelectedIndex = selectedImageIndex;
    m_folderLoadWatcher.setFuture(QtConcurrent::run([rootPath, folderRelativePath, loadFolderRelativePath, generation]() {
        FolderLoadJobResult job;
        job.generation = generation;
        job.folderRelativePath = folderRelativePath;
        job.result = core::loadFileSystemFolder(rootPath, loadFolderRelativePath);
        return job;
    }));
}

void MainWindow::applyScanResult(const ScanJobResult &job)
{
    if (job.generation != m_sourceGeneration) {
        return;
    }

    if (!job.result.ok()) {
        const bool archiveSelection = !job.selectedPaths.isEmpty()
            && core::isSupportedArchiveFile(QFileInfo(job.selectedPaths.first()));
        if ((job.result.errorReason == core::ScanErrorReason::PasswordRequired
                || job.result.errorReason == core::ScanErrorReason::InvalidPassword)
            && archiveSelection) {
            if (job.selectedPaths.size() > 1 && !job.archivePassword.isEmpty()) {
                clearArchivePasswords();
                showError(job.result.errorMessage);
                return;
            }

            const bool retry = job.result.errorReason == core::ScanErrorReason::InvalidPassword;
            const bool multiArchive = job.selectedPaths.size() > 1;
            QString password;
            const QString promptPath = multiArchive ? QString() : job.selectedPaths.first();
            if (promptArchivePassword(promptPath, retry, &password)) {
                for (const QString &archivePath : job.selectedPaths) {
                    rememberArchivePassword(archivePath, password);
                }

                const QString scanMessage = multiArchive ? tr("Scanning archives...") : tr("Scanning archive...");
                m_infoPanel->showMessage(scanMessage);
                m_fullscreenInfoPanel->showMessage(scanMessage);
                m_imageView->setMessage(scanMessage);
                updateStatus(scanMessage);
                if (multiArchive) {
                    startArchiveListScan(job.selectedPaths, password);
                } else {
                    startArchiveScan(job.selectedPaths.first(), password);
                }
            } else {
                clearArchivePasswords();
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
    rebuildSourceFolderTraversal();
    if (m_sourceRoot != nullptr) {
        m_fileTreeView->expand(m_fileTreeModel->indexForFolderPath(QString()));
        if (m_fullscreenFileTreeView != nullptr) {
            m_fullscreenFileTreeView->expand(m_fileTreeModel->indexForFolderPath(QString()));
        }
    }
    QString selectedIdentityKey;
    if (!job.selectedPath.isEmpty() && core::isSupportedImageFile(QFileInfo(job.selectedPath))) {
        if (const core::CollectionFolderNode *folder = folderNodeForPath(job.result.selectedFolderPath)) {
            selectedIdentityKey = identityKeyForSelectedPath(*folder, job.selectedPath);
        }
    }
    activateFolder(job.result.selectedFolderPath, job.result.selectedImageIndex, true, selectedIdentityKey);
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

    QVector<core::ImageEntry> images = job.result.images;
    QVector<std::shared_ptr<core::CollectionFolderNode>> children = job.result.children;
    rebaseLoadedFolderResult(node, &images, &children);
    if (!m_fileTreeModel->replaceLoadedFolderContents(node, std::move(images), std::move(children))) {
        showError(tr("Could not update folder tree."));
        return;
    }
    rebuildSourceFolderTraversal();

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

bool MainWindow::promptArchivePassword(const QString &archivePath, bool retry, QString *passwordOut)
{
    bool ok = false;
    const QFileInfo archiveInfo(archivePath);
    const QString target = archivePath.isEmpty()
        ? tr("selected archives")
        : archiveInfo.fileName();
    const QString label = retry
        ? tr("The password was not accepted for %1. Enter the archive password:").arg(target)
        : tr("Enter the password for %1:").arg(target);
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

    if (passwordOut != nullptr) {
        *passwordOut = password;
    }
    if (!archivePath.isEmpty()) {
        rememberArchivePassword(archivePath, password);
    }
    return true;
}

void MainWindow::rememberArchivePassword(const QString &archivePath, const QString &password)
{
    m_archivePasswords.insert(QFileInfo(archivePath).absoluteFilePath(), password);
}

QString MainWindow::archivePasswordForPath(const QString &archivePath) const
{
    return m_archivePasswords.value(QFileInfo(archivePath).absoluteFilePath());
}

QString MainWindow::archivePasswordForEntry(const core::ImageEntry &entry) const
{
    if (entry.source != core::ImageEntrySource::Archive || entry.archivePath.isEmpty()) {
        return {};
    }
    return archivePasswordForPath(entry.archivePath);
}

void MainWindow::clearArchivePasswords()
{
    m_archivePasswords.clear();
}

void MainWindow::displayDecodedImage(const core::DecodedImage &decoded)
{
    cancelDelayedLoadingOverlay();
    m_imageRenderPending = false;
    m_currentDecoded = decoded;
    m_imageView->setImageInteractionEnabled(true);
    m_imageView->setImage(decoded.image, decoded.fullDisplaySize, decoded.displayRegionOffset);
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
    cancelDelayedLoadingOverlay();
    m_imageRenderPending = false;
    clearPendingDecodeRequest();
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
    m_imageView->setImage(m_currentDecoded.image, m_currentDecoded.fullDisplaySize, m_currentDecoded.displayRegionOffset);
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
    cancelDelayedLoadingOverlay();
    ++m_decodeGeneration;
    m_imageRenderPending = false;
    clearPendingDecodeRequest();
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
    QString effectiveMessage = message;
    const bool compareActive = compareTabActive();
    if (compareActive && !m_comparisonStatusMessage.isEmpty()) {
        effectiveMessage = m_comparisonStatusMessage;
    }

    const StatusLineContext context {
        m_controller.currentEntry(),
        &m_currentDecoded,
        m_controller.zoomMode(),
        m_controller.zoomFactor(),
        m_controller.currentIndex(),
        m_controller.count(),
        effectiveMessage,
    };
    const QString statusText = formatStatusLine(context);

    m_statusLabel->setText(statusText);
    updateFullscreenStatusText();
    updateFullscreenInfoLine();
}

void MainWindow::updateActionState()
{
    const bool compareActive = m_centralTabs != nullptr
        && m_comparisonView != nullptr
        && m_centralTabs->currentWidget() == m_comparisonView;
    const bool hasImage = m_controller.hasCollection();
    const bool hasPreviousInFolder = hasImage && m_controller.currentIndex() > 0;
    const bool hasNextInFolder = hasImage && m_controller.currentIndex() + 1 < m_controller.count();
    const bool hasPreviousAdjacentFolder = adjacentFolder(false) != nullptr;
    const bool hasNextAdjacentFolder = adjacentFolder(true) != nullptr;
    const bool hasPrevious = hasPreviousInFolder || hasPreviousAdjacentFolder;
    const bool hasNext = hasNextInFolder || hasNextAdjacentFolder;
    const bool canChangeView = hasImage && !m_imageRenderPending && !m_activeHoldZoom.has_value();

    m_previousAction->setEnabled(!compareActive && (hasPrevious || hasImage));
    m_nextAction->setEnabled(!compareActive && (hasNext || hasImage));
    m_previousFolderAction->setEnabled(!compareActive && hasPreviousAdjacentFolder);
    m_nextFolderAction->setEnabled(!compareActive && hasNextAdjacentFolder);
    m_firstAction->setEnabled(!compareActive && hasPreviousInFolder);
    m_lastAction->setEnabled(!compareActive && hasNextInFolder);
    m_quickJumpAction->setEnabled(!compareActive && hasImage);
    m_copyOriginalImageAction->setEnabled(!compareActive && hasImage && !m_copyOriginalImageWatcher.isRunning());
    m_zoomInAction->setEnabled(!compareActive && canChangeView);
    m_zoomOutAction->setEnabled(!compareActive && canChangeView);
    m_fitAction->setEnabled(!compareActive && canChangeView);
    m_actualSizeAction->setEnabled(!compareActive && canChangeView);
    m_rotateLeftAction->setEnabled(!compareActive && canChangeView);
    m_rotateRightAction->setEnabled(!compareActive && canChangeView);
    m_fullscreenAction->setEnabled(isFullScreen()
        || (!compareActive && hasImage)
        || (compareActive && m_comparisonView != nullptr && m_comparisonView->hasSession()));
}

void MainWindow::updateInteractionState()
{
    const bool panningEnabled = m_controller.hasCollection()
        && m_controller.zoomMode() != core::ZoomMode::FitToWindow;
    const QSize displayedSize = m_currentDecoded.fullDisplaySize.isEmpty()
        ? m_currentDecoded.image.size()
        : m_currentDecoded.fullDisplaySize;
    const bool displayedImageExceedsViewport = displayedSize.width() > m_imageView->width()
        || displayedSize.height() > m_imageView->height();
    const bool fitHandPanEnabled = m_controller.hasCollection()
        && !m_imageRenderPending
        && !m_activeHoldZoom.has_value()
        && m_currentDecoded.ok()
        && m_controller.zoomMode() != core::ZoomMode::FitToWindow
        && displayedImageExceedsViewport;
    const bool clickZoomCursorEnabled = m_controller.hasCollection()
        && !m_imageRenderPending
        && !m_activeHoldZoom.has_value()
        && m_currentDecoded.ok()
        && m_currentDecoded.effectiveScale > 0.0;
    m_imageView->setPanningEnabled(panningEnabled);
    m_imageView->setFitHandPanEnabled(fitHandPanEnabled);
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
    if (m_controller.hasCollection()) {
        m_resizeReloadTimer.start();
    }
}

void MainWindow::scheduleRegionReload()
{
    if (!m_controller.hasCollection()
        || m_imageRenderPending
        || !m_currentDecoded.regionBased) {
        return;
    }

    m_regionReloadTimer.start();
}

void MainWindow::clearPrefetchJobs()
{
    for (auto it = m_prefetchWatchers.begin(); it != m_prefetchWatchers.end(); ++it) {
        QFutureWatcher<void> *watcher = it.value();
        if (watcher == nullptr) {
            continue;
        }

        disconnect(watcher, nullptr, this, nullptr);
        watcher->cancel();
        watcher->deleteLater();
    }
    m_prefetchWatchers.clear();
}

void MainWindow::prefetchNeighbors(const core::DecodeRequest &request)
{
    if (!core::shouldPrefetchDecodeRequest(request)) {
        return;
    }

    const QVector<core::ImageEntry> &entries = m_controller.entries();
    const int currentIndex = m_controller.currentIndex();
    const auto cache = m_cache;
    const quint64 cacheGeneration = cache->generation();
    for (int index : { currentIndex - 1, currentIndex + 1 }) {
        if (index < 0 || index >= entries.size()) {
            continue;
        }

        const core::ImageEntry entry = entries.at(index);
        const QString key = core::cacheKeyFor(entry, request);
        if (cache->contains(key) || m_prefetchWatchers.contains(key)) {
            continue;
        }

        const QString archivePassword = archivePasswordForEntry(entry);
        if (entry.source == core::ImageEntrySource::Archive && !archivePassword.isEmpty()) {
            continue;
        }

        auto *watcher = new QFutureWatcher<void>(this);
        m_prefetchWatchers.insert(key, watcher);
        connect(watcher, &QFutureWatcher<void>::finished, this, [this, key, watcher]() {
            auto it = m_prefetchWatchers.find(key);
            if (it != m_prefetchWatchers.end() && it.value() == watcher) {
                m_prefetchWatchers.erase(it);
            }
            watcher->deleteLater();
        });

        watcher->setFuture(QtConcurrent::run([cache, entry, request, key, archivePassword, cacheGeneration]() {
            core::VipsImageDecoder decoder;
            const core::DecodedImage decoded = decoder.decode(entry, request, archivePassword);
            if (decoded.ok()) {
                [[maybe_unused]] const bool inserted = cache->insertIfGeneration(key, decoded, cacheGeneration);
            }
        }));
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
    const double currentLogicalScale = decodedLogicalScale(m_currentDecoded);
    if (currentLogicalScale <= 0.0) {
        return false;
    }
    const double newScale = std::clamp(
        currentScale * multiplier,
        core::MinimumZoomFactor,
        core::MaximumZoomFactor);
    if (qFuzzyCompare(currentScale, newScale)) {
        return false;
    }

    const QPointF anchor = clampedPointToRect(anchorPosition, imageRect);
    const QPointF imagePoint = anchor - imageRect.topLeft();
    const QPointF sourcePoint(imagePoint.x() / currentLogicalScale, imagePoint.y() / currentLogicalScale);
    const QSize currentDisplaySize = m_currentDecoded.fullDisplaySize.isEmpty()
        ? m_currentDecoded.image.size()
        : m_currentDecoded.fullDisplaySize;
    const QSizeF newDisplaySize(
        currentDisplaySize.width() * newScale / currentScale,
        currentDisplaySize.height() * newScale / currentScale);
    const QPointF centeredTopLeft(
        (m_imageView->width() - newDisplaySize.width()) / 2.0,
        (m_imageView->height() - newDisplaySize.height()) / 2.0);
    const double newLogicalScale = currentLogicalScale * newScale / currentScale;
    const QPointF pointAtNewScale(sourcePoint.x() * newLogicalScale, sourcePoint.y() * newLogicalScale);

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
        scheduleRegionReload();
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
    clearComparisonSession();
    clearComparisonSelection();
    resetThumbnailWork(true);
    m_sourceRoot.reset();
    m_sourceRootPath.clear();
    m_sourceFoldersPreorder.clear();
    m_activeFolderPath.clear();
    m_loadingFolderPath.clear();
    m_loadingFolderSelectedIndex = 0;
    if (m_fileTreeModel != nullptr) {
        m_fileTreeModel->clear();
    }
}

void MainWindow::activateFolder(
    const QString &folderRelativePath,
    int selectedImageIndex,
    bool loadCurrent,
    const QString &selectedIdentityKey)
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

    int sortedSelectedIndex = selectedImageIndex;
    QVector<core::ImageEntry> sortedImages = sortedImagesForFolder(
        *folder,
        selectedImageIndex,
        selectedIdentityKey,
        &sortedSelectedIndex);

    m_temporaryZoomReturnMode.reset();
    m_controller.setCollection(std::move(sortedImages), sortedSelectedIndex);
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

bool MainWindow::navigateToAdjacentFolder(bool forward, int selectedImageIndex)
{
    const core::CollectionFolderNode *folder = adjacentFolder(forward);
    if (folder == nullptr) {
        return false;
    }
    activateFolder(folder->relativePath, selectedImageIndex, true);
    return true;
}

bool MainWindow::hasAdjacentFolder(bool forward) const
{
    return adjacentFolder(forward) != nullptr;
}

const core::CollectionFolderNode *MainWindow::adjacentFolder(bool forward) const
{
    if (m_sourceFoldersPreorder.isEmpty()) {
        return nullptr;
    }

    int activeIndex = -1;
    for (qsizetype index = 0; index < m_sourceFoldersPreorder.size(); ++index) {
        if (m_sourceFoldersPreorder.at(index)->relativePath == m_activeFolderPath) {
            activeIndex = static_cast<int>(index);
            break;
        }
    }
    if (activeIndex < 0) {
        return nullptr;
    }

    if (forward) {
        for (int index = activeIndex + 1; index < m_sourceFoldersPreorder.size(); ++index) {
            if (isNavigableFolder(m_sourceFoldersPreorder.at(index))) {
                return m_sourceFoldersPreorder.at(index);
            }
        }
        return nullptr;
    }

    for (int index = activeIndex - 1; index >= 0; --index) {
        if (isNavigableFolder(m_sourceFoldersPreorder.at(index))) {
            return m_sourceFoldersPreorder.at(index);
        }
    }
    return nullptr;
}

void MainWindow::rebuildSourceFolderTraversal()
{
    m_sourceFoldersPreorder.clear();
    appendFoldersInPreorder(m_sourceRoot.get(), &m_sourceFoldersPreorder);
}

QVector<core::ImageEntry> MainWindow::sortedImagesForFolder(
    const core::CollectionFolderNode &folder,
    int selectedImageIndex,
    const QString &selectedIdentityKey,
    int *sortedSelectedIndex) const
{
    QVector<core::ImageEntry> entries = folder.images;
    const bool preserveCurrentOrder = folder.preserveImageOrder
        && m_imageSortMode.key == core::ImageSortKey::Name
        && m_imageSortMode.direction == core::SortDirection::Ascending;
    if (!preserveCurrentOrder) {
        core::sortImageEntries(&entries, m_imageSortMode);
    }

    int resolvedIndex = entries.isEmpty() ? -1 : 0;
    if (!entries.isEmpty()) {
        if (!selectedIdentityKey.isEmpty()) {
            for (qsizetype index = 0; index < entries.size(); ++index) {
                if (entries.at(index).identityKey() == selectedIdentityKey) {
                    resolvedIndex = static_cast<int>(index);
                    break;
                }
            }
        } else if (selectedImageIndex < 0) {
            resolvedIndex = entries.size() - 1;
        } else {
            resolvedIndex = std::clamp(selectedImageIndex, 0, static_cast<int>(entries.size()) - 1);
        }
    }

    if (sortedSelectedIndex != nullptr) {
        *sortedSelectedIndex = resolvedIndex;
    }
    return entries;
}

QString MainWindow::identityKeyForSelectedPath(
    const core::CollectionFolderNode &folder,
    const QString &selectedPath) const
{
    const QString absoluteSelected = QFileInfo(selectedPath).absoluteFilePath();
    for (const core::ImageEntry &entry : folder.images) {
        if (entry.absolutePath == absoluteSelected) {
            return entry.identityKey();
        }
    }
    return {};
}

void MainWindow::applyImageSortMode(core::ImageSortMode mode)
{
    core::CollectionFolderNode *folder = folderNodeForPath(m_activeFolderPath);
    const bool forceSortPreservedOrder = folder != nullptr && folder->preserveImageOrder;
    if (!forceSortPreservedOrder && mode.key == m_imageSortMode.key && mode.direction == m_imageSortMode.direction) {
        syncFileSortControls();
        return;
    }

    const core::ImageEntry *currentEntry = m_controller.currentEntry();
    const QString currentIdentityKey = currentEntry == nullptr ? QString() : currentEntry->identityKey();
    m_imageSortMode = mode;
    if (folder != nullptr) {
        folder->preserveImageOrder = false;
    }
    syncFileSortControls();

    if (folder == nullptr) {
        refreshFileList();
        updateActionState();
        return;
    }

    int selectedIndex = -1;
    QVector<core::ImageEntry> sortedImages = sortedImagesForFolder(*folder, 0, currentIdentityKey, &selectedIndex);
    if (sortedImages.isEmpty()) {
        m_controller.clearCollection();
        refreshFileList();
        updateInfoPanelForCurrentImage();
        updateStatus();
        updateActionState();
        return;
    }

    const bool keptCurrentEntry = !currentIdentityKey.isEmpty()
        && selectedIndex >= 0
        && selectedIndex < sortedImages.size()
        && sortedImages.at(selectedIndex).identityKey() == currentIdentityKey;
    m_controller.setCollectionPreservingView(std::move(sortedImages), selectedIndex);
    refreshFileList();
    updateInfoPanelForCurrentImage();
    updateStatus();
    updateActionState();

    if (!keptCurrentEntry) {
        reloadAfterImageSwitch();
    }
}

QToolButton *MainWindow::addToolbarButton(QToolBar *toolbar, QAction *action, const QString &label, bool fullscreen)
{
    if (toolbar == nullptr || action == nullptr) {
        return nullptr;
    }

    auto *button = new QToolButton(toolbar);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setText(label);
    button->setAutoRaise(!fullscreen);
    button->setMinimumHeight(fullscreen ? FullscreenToolbarButtonHeight : MainToolbarButtonHeight);
    button->setMouseTracking(true);
    button->installEventFilter(this);

    auto applyPresentation = [button, action, label]() {
        button->setText(label);
        QString tooltip = action->toolTip();
        if (tooltip.isEmpty()) {
            tooltip = action->text();
        }
        button->setToolTip(tooltip.remove(QLatin1Char('&')));
    };
    applyPresentation();
    connect(action, &QAction::changed, button, applyPresentation);

    toolbar->addWidget(button);
    return button;
}

void MainWindow::addToolbarSpacer(QToolBar *toolbar)
{
    if (toolbar == nullptr) {
        return;
    }

    auto *spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
}

QToolButton *MainWindow::addAppMenuButton(QToolBar *toolbar, bool fullscreen)
{
    if (toolbar == nullptr) {
        return nullptr;
    }

    auto *button = new QToolButton(toolbar);
    button->setAutoRaise(!fullscreen);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setText(QStringLiteral("☰"));
    button->setToolTip(tr("Main menu"));
    button->setMinimumHeight(fullscreen ? FullscreenToolbarButtonHeight : MainToolbarButtonHeight);
    button->setMouseTracking(true);
    button->installEventFilter(this);
    if (!fullscreen) {
        button->setStyleSheet(QStringLiteral(
            "QToolButton {"
            "  border: 1px solid transparent;"
            "  border-radius: 4px;"
            "  min-width: 24px;"
            "  padding: 2px 6px;"
            "}"
            "QToolButton:hover {"
            "  background-color: palette(alternate-base);"
            "  border-color: palette(mid);"
            "}"
            "QToolButton::menu-indicator { image: none; width: 0; }"));
    }

    button->setMenu(createAppMenu(button));
    toolbar->addWidget(button);
    return button;
}

QMenu *MainWindow::createAppMenu(QWidget *parent)
{
    auto *menu = new QMenu(parent);
    menu->addAction(m_openFileAction);
    menu->addAction(m_openFolderAction);
    menu->addSeparator();
    menu->addAction(m_fullscreenAction);
    menu->addAction(m_exifAutorotateAction);
    menu->addMenu(createSortMenu(menu));
    menu->addSeparator();
    menu->addAction(m_preferencesAction);
    menu->addSeparator();
    menu->addAction(m_quitAction);
    return menu;
}

QMenu *MainWindow::createSortMenu(QWidget *parent)
{
    auto *menu = new QMenu(tr("Sort"), parent);
    menu->setObjectName(QStringLiteral("sortMenu"));
    auto addModeAction = [this, menu](core::ImageSortKey key, core::SortDirection direction) {
        const core::ImageSortMode mode { key, direction };
        QAction *action = menu->addAction(sortModeMenuLabel(mode));
        action->setProperty("sortKey", static_cast<int>(key));
        action->setProperty("sortDirection", static_cast<int>(direction));
        connect(action, &QAction::triggered, this, [this, mode]() {
            applyImageSortMode(mode);
        });
    };

    addModeAction(core::ImageSortKey::Name, core::SortDirection::Ascending);
    addModeAction(core::ImageSortKey::Name, core::SortDirection::Descending);
    menu->addSeparator();
    addModeAction(core::ImageSortKey::Type, core::SortDirection::Ascending);
    addModeAction(core::ImageSortKey::Type, core::SortDirection::Descending);
    menu->addSeparator();
    addModeAction(core::ImageSortKey::Size, core::SortDirection::Ascending);
    addModeAction(core::ImageSortKey::Size, core::SortDirection::Descending);
    menu->addSeparator();
    addModeAction(core::ImageSortKey::Date, core::SortDirection::Ascending);
    addModeAction(core::ImageSortKey::Date, core::SortDirection::Descending);
    updateSortMenuSelection(menu, m_imageSortMode);
    return menu;
}

void MainWindow::syncFileSortControls()
{
    const QString toolTip = tr("Sort files: %1").arg(sortModeLabel(m_imageSortMode));

    const QList<QToolButton *> appMenuButtons {
        m_appMenuButton,
        m_fullscreenAppMenuButton,
    };
    for (QToolButton *button : appMenuButtons) {
        if (button == nullptr || button->menu() == nullptr) {
            continue;
        }
        for (QMenu *menu : button->menu()->findChildren<QMenu *>(QStringLiteral("sortMenu"))) {
            menu->menuAction()->setToolTip(toolTip);
            updateSortMenuSelection(menu, m_imageSortMode);
        }
    }
}

void MainWindow::refreshFileList()
{
    clearComparisonSelection();
    m_fileListModel->setEntries(m_controller.entries());
    m_thumbnailListModel->setEntries(m_controller.entries());
    m_fileListView->setEnabled(m_controller.hasCollection());
    m_thumbnailListView->setEnabled(m_controller.hasCollection());
    if (m_fullscreenFileListView != nullptr) {
        m_fullscreenFileListView->setEnabled(m_controller.hasCollection());
    }
    if (m_fullscreenThumbnailListView != nullptr) {
        m_fullscreenThumbnailListView->setEnabled(m_controller.hasCollection());
    }
    m_thumbnailsActive = false;
    m_thumbnailLoader->clearQueue(++m_thumbnailGeneration);
    syncFileListSelection();
    syncFolderTreeSelection();
    updateThumbnailActivation();
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
        syncThumbnailSelection();
        return;
    }

    const QModelIndex modelIndex = m_fileListModel->index(currentIndex, 0);
    selectionModel->setCurrentIndex(
        modelIndex,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current | QItemSelectionModel::Rows);
    m_fileListView->scrollTo(modelIndex, QAbstractItemView::EnsureVisible);
    syncFullscreenFileListSelection();
    syncThumbnailSelection();
}

void MainWindow::syncThumbnailSelection()
{
    if (m_thumbnailListView == nullptr || m_thumbnailListModel == nullptr) {
        return;
    }

    const QScopedValueRollback<bool> guard(m_syncingFileListSelection, true);
    const int currentIndex = m_controller.currentIndex();
    const bool validIndex = currentIndex >= 0 && currentIndex < m_thumbnailListModel->rowCount();
    const QModelIndex modelIndex = validIndex ? m_thumbnailListModel->index(currentIndex, 0) : QModelIndex();

    auto syncView = [modelIndex, validIndex](QListView *view) {
        if (view == nullptr || view->selectionModel() == nullptr) {
            return;
        }
        if (!validIndex) {
            view->selectionModel()->clear();
            view->setCurrentIndex(QModelIndex());
            return;
        }
        view->selectionModel()->setCurrentIndex(
            modelIndex,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current | QItemSelectionModel::Rows);
        view->scrollTo(modelIndex, QAbstractItemView::EnsureVisible);
    };

    syncView(m_thumbnailListView);
    syncView(m_fullscreenThumbnailListView);
}

void MainWindow::syncFilePanelMode(FilePanelMode mode)
{
    m_filePanelMode = mode;
    const int tabIndex = mode == FilePanelMode::Thumbnails ? 1 : 0;
    const QScopedValueRollback<bool> guard(m_syncingFilePanelTabs, true);
    if (m_filePanelTabs != nullptr && m_filePanelTabs->currentIndex() != tabIndex) {
        m_filePanelTabs->setCurrentIndex(tabIndex);
    }
    if (m_fullscreenFilePanelTabs != nullptr && m_fullscreenFilePanelTabs->currentIndex() != tabIndex) {
        m_fullscreenFilePanelTabs->setCurrentIndex(tabIndex);
    }
    updateThumbnailActivation();
}

bool MainWindow::thumbnailsShouldRun() const
{
    if (m_filePanelMode != FilePanelMode::Thumbnails || !m_controller.hasCollection()) {
        return false;
    }

    if (m_fullscreenUiActive || isFullScreen()) {
        return m_fullscreenLeftPanel != nullptr
            && m_fullscreenLeftPanel->isVisible()
            && m_fullscreenFilePanelTabs != nullptr
            && m_fullscreenFilePanelTabs->currentIndex() == 1;
    }

    return m_fileListDock != nullptr
        && m_fileListDock->isVisible()
        && m_filePanelTabs != nullptr
        && m_filePanelTabs->currentIndex() == 1;
}

void MainWindow::updateThumbnailActivation()
{
    const bool shouldRun = thumbnailsShouldRun();
    if (!shouldRun) {
        if (m_thumbnailsActive) {
            m_thumbnailsActive = false;
            m_thumbnailLoader->clearQueue(++m_thumbnailGeneration);
        }
        return;
    }

    if (!m_thumbnailsActive) {
        m_thumbnailsActive = true;
        m_thumbnailLoader->setActiveGeneration(++m_thumbnailGeneration);
    }
    requestVisibleThumbnails();
}

void MainWindow::resetThumbnailWork(bool clearCache)
{
    m_thumbnailsActive = false;
    m_thumbnailLoader->clearQueue(++m_thumbnailGeneration);
    m_thumbnailListModel->clearThumbnails();
    if (clearCache) {
        m_thumbnailLoader->clearCache();
    }
}

core::DecodeRequest MainWindow::thumbnailDecodeRequest() const
{
    core::DecodeRequest request;
    request.viewportSize = QSize(ThumbnailImageSize, ThumbnailImageSize);
    request.devicePixelRatio = std::max(1.0, m_devicePixelRatio);
    request.scaleFactor = 1.0;
    request.rotationDegrees = 0;
    request.fitToWindow = true;
    request.regionRendering = false;
    request.exifAutorotate = m_settings.viewer.exifAutorotate;
    request.scalingQuality = core::ScalingQuality::FastLinear;
    request.colorManagementEnabled = m_settings.viewer.colorManagementEnabled;
    request.maxDecodedBytes = 32LL * 1024LL * 1024LL;
    return request;
}

void MainWindow::requestVisibleThumbnails()
{
    if (!m_thumbnailsActive || !thumbnailsShouldRun() || m_thumbnailListModel == nullptr) {
        return;
    }

    QListView *view = (m_fullscreenUiActive || isFullScreen()) && m_fullscreenLeftPanel != nullptr && m_fullscreenLeftPanel->isVisible()
        ? m_fullscreenThumbnailListView
        : m_thumbnailListView;
    if (view == nullptr || view->viewport() == nullptr || m_thumbnailListModel->rowCount() <= 0) {
        return;
    }

    const int count = m_thumbnailListModel->rowCount();
    int firstRow = 0;
    int lastRow = std::min(count - 1, 32);
    const QRect viewportRect = view->viewport()->rect();
    const QModelIndex firstVisible = view->indexAt(viewportRect.topLeft() + QPoint(2, 2));
    const QModelIndex lastVisible = view->indexAt(viewportRect.bottomRight() - QPoint(2, 2));
    if (firstVisible.isValid()) {
        firstRow = firstVisible.row();
    }
    if (lastVisible.isValid()) {
        lastRow = lastVisible.row();
    } else if (firstVisible.isValid()) {
        lastRow = std::min(count - 1, firstRow + 32);
    }

    firstRow = std::max(0, firstRow - ThumbnailNearbyRows);
    lastRow = std::min(count - 1, lastRow + ThumbnailNearbyRows);

    QVector<int> rows;
    auto addRow = [&rows, count](int row) {
        if (row < 0 || row >= count || rows.contains(row)) {
            return;
        }
        rows.append(row);
    };
    addRow(m_controller.currentIndex());
    for (int row = firstRow; row <= lastRow; ++row) {
        addRow(row);
    }

    const core::DecodeRequest request = thumbnailDecodeRequest();
    for (int row : rows) {
        if (m_thumbnailListModel->hasThumbnail(row) || m_thumbnailListModel->isFailed(row)) {
            continue;
        }
        const core::ImageEntry *entry = m_thumbnailListModel->entryAt(row);
        if (entry == nullptr) {
            continue;
        }
        m_thumbnailLoader->requestThumbnail(
            row,
            *entry,
            request,
            archivePasswordForEntry(*entry),
            m_thumbnailGeneration);
    }
}

void MainWindow::handleThumbnailReady(const ThumbnailResult &result)
{
    if (result.generation != m_thumbnailGeneration || m_thumbnailListModel == nullptr) {
        return;
    }
    m_thumbnailListModel->setThumbnail(result.identityKey, result.image);
}

void MainWindow::handleThumbnailFailed(const ThumbnailResult &result)
{
    if (result.generation != m_thumbnailGeneration || m_thumbnailListModel == nullptr) {
        return;
    }
    m_thumbnailListModel->markFailed(result.identityKey);
}

void MainWindow::updateComparisonSelectionOrder(const QItemSelection &selected, const QItemSelection &deselected)
{
    if (m_syncingFileListSelection) {
        return;
    }

    auto identityForIndex = [this](const QModelIndex &index) -> QString {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_controller.count()) {
            return {};
        }
        return m_controller.entries().at(index.row()).identityKey();
    };

    for (const QModelIndex &index : deselected.indexes()) {
        const QString identity = identityForIndex(index);
        if (!identity.isEmpty()) {
            m_comparisonSelectionOrder.removeAll(identity);
        }
    }

    for (const QModelIndex &index : selected.indexes()) {
        const QString identity = identityForIndex(index);
        if (!identity.isEmpty() && !m_comparisonSelectionOrder.contains(identity)) {
            m_comparisonSelectionOrder.append(identity);
        }
    }
}

QVector<ComparisonSessionItem> MainWindow::comparisonItemsForView(QListView *view) const
{
    QVector<ComparisonSessionItem> items;
    if (view == nullptr || view->selectionModel() == nullptr || !m_controller.hasCollection()) {
        return items;
    }

    QVector<int> selectedRows;
    for (const QModelIndex &index : view->selectionModel()->selectedIndexes()) {
        if (index.isValid() && index.row() >= 0 && index.row() < m_controller.count() && !selectedRows.contains(index.row())) {
            selectedRows.append(index.row());
        }
    }

    auto appendRow = [this, &items, &selectedRows](int row) {
        if (!selectedRows.contains(row)) {
            return;
        }
        const core::ImageEntry &entry = m_controller.entries().at(row);
        items.append(ComparisonSessionItem {
            entry,
            archivePasswordForEntry(entry),
        });
        selectedRows.removeAll(row);
    };

    for (const QString &identity : m_comparisonSelectionOrder) {
        for (int row = 0; row < m_controller.count(); ++row) {
            if (m_controller.entries().at(row).identityKey() == identity) {
                appendRow(row);
                break;
            }
        }
    }

    std::sort(selectedRows.begin(), selectedRows.end());
    for (int row : selectedRows) {
        appendRow(row);
    }
    return items;
}

void MainWindow::showComparisonContextMenu(QListView *view, const QPoint &position)
{
    if (view == nullptr || view->selectionModel() == nullptr) {
        return;
    }

    const QModelIndex clickedIndex = view->indexAt(position);
    if (clickedIndex.isValid() && !view->selectionModel()->isSelected(clickedIndex)) {
        view->selectionModel()->setCurrentIndex(
            clickedIndex,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current | QItemSelectionModel::Rows);
    }

    const QVector<ComparisonSessionItem> items = comparisonItemsForView(view);
    QMenu menu(this);
    QMenu *compareMenu = menu.addMenu(tr("Compare"));

    QAction *addAction = compareMenu->addAction(tr("Add to Compare"));
    int uniqueToAdd = 0;
    for (const ComparisonSessionItem &item : items) {
        const QString identityKey = item.entry.identityKey();
        if (!identityKey.isEmpty()
            && m_comparisonView != nullptr
            && !m_comparisonView->hasItem(identityKey)) {
            ++uniqueToAdd;
        }
    }
    const int currentCount = m_comparisonView == nullptr ? 0 : m_comparisonView->itemCount();
    if (items.isEmpty()) {
        addAction->setEnabled(false);
    } else if (uniqueToAdd <= 0) {
        addAction->setText(tr("Add to Compare (already added)"));
        addAction->setEnabled(false);
    } else if (currentCount + uniqueToAdd > MaxComparisonItems) {
        addAction->setText(tr("Add to Compare (max 8 images)"));
        addAction->setEnabled(false);
    } else if (m_comparisonView != nullptr && !m_comparisonView->canAppendItems(uniqueToAdd)) {
        addAction->setText(tr("Add to Compare (not available in current mode)"));
        addAction->setEnabled(false);
    } else {
        connect(addAction, &QAction::triggered, this, [this, items]() {
            appendToComparison(items);
        });
    }

    QAction *openAction = compareMenu->addAction(tr("Open Compare"));
    openAction->setEnabled(m_comparisonView != nullptr && m_comparisonView->hasSession());
    connect(openAction, &QAction::triggered, this, [this]() {
        openComparisonSession();
    });

    compareMenu->addSeparator();
    QAction *rowAction = compareMenu->addAction(tr("In Row"));
    if (items.size() < 2) {
        rowAction->setText(tr("In Row (select 2-8 images)"));
        rowAction->setEnabled(false);
    } else if (items.size() > MaxComparisonItems) {
        rowAction->setText(tr("In Row (max 8 images)"));
        rowAction->setEnabled(false);
    } else {
        connect(rowAction, &QAction::triggered, this, [this, items]() {
            openComparison(items, ComparisonMode::Row);
        });
    }

    QAction *hoverSwitchAction = compareMenu->addAction(tr("Hover Switch"));
    if (items.size() != 2) {
        hoverSwitchAction->setText(tr("Hover Switch (select exactly 2 images)"));
        hoverSwitchAction->setEnabled(false);
    } else {
        connect(hoverSwitchAction, &QAction::triggered, this, [this, items]() {
            openComparison(items, ComparisonMode::HoverSwitch);
        });
    }
    menu.exec(view->viewport()->mapToGlobal(position));
}

void MainWindow::openComparison(const QVector<ComparisonSessionItem> &items, ComparisonMode mode)
{
    if (items.size() > MaxComparisonItems
        || !ComparisonViewWidget::supportsItemCount(mode, items.size())
        || m_comparisonView == nullptr
        || m_centralTabs == nullptr) {
        return;
    }

    if (!m_fullscreenUiActive && isFullScreen()) {
        showNormal();
    }

    m_comparisonView->setSession(items, comparisonRenderSettings(), mode);
    m_centralTabs->setCurrentWidget(m_comparisonView);
    updateComparisonTabTitle();
    if (m_fullscreenUiActive) {
        m_fullscreenContentMode = FullscreenContentMode::Compare;
        m_comparisonView->setFullscreenPresentation(true);
        setCentralTabBarVisible(false);
        hideFullscreenOverlayPanels();
        updateFullscreenInfoLine();
    }
    m_comparisonStatusMessage = mode == ComparisonMode::HoverSwitch
            ? tr("Compare: 2 images in Hover Switch")
            : tr("Compare: %1 images in Row").arg(items.size());
    updateStatus(m_comparisonStatusMessage);
    updateActionState();
}

void MainWindow::appendToComparison(const QVector<ComparisonSessionItem> &items)
{
    if (items.isEmpty() || m_comparisonView == nullptr || m_centralTabs == nullptr) {
        return;
    }

    int uniqueToAdd = 0;
    for (const ComparisonSessionItem &item : items) {
        const QString identityKey = item.entry.identityKey();
        if (!identityKey.isEmpty() && !m_comparisonView->hasItem(identityKey)) {
            ++uniqueToAdd;
        }
    }
    if (uniqueToAdd <= 0) {
        updateStatus(tr("Compare: selected images already added"));
        return;
    }
    if (m_comparisonView->itemCount() + uniqueToAdd > MaxComparisonItems) {
        updateStatus(tr("Compare: maximum is 8 images"));
        return;
    }
    if (!m_comparisonView->canAppendItems(uniqueToAdd)) {
        updateStatus(m_comparisonView->mode() == ComparisonMode::HoverSwitch
                ? tr("Compare: Hover Switch supports exactly 2 images")
                : tr("Compare: current mode does not support that many images"));
        return;
    }

    int added = 0;
    if (!m_comparisonView->hasSession()) {
        m_comparisonView->setSession(items, comparisonRenderSettings(), ComparisonMode::Row);
        added = m_comparisonView->itemCount();
    } else {
        added = m_comparisonView->appendItems(items);
    }
    updateComparisonTabTitle();
    m_comparisonStatusMessage = tr("Compare: added %1, total %2").arg(added).arg(m_comparisonView->itemCount());
    updateStatus(m_comparisonStatusMessage);
    updateActionState();
}

void MainWindow::openComparisonSession()
{
    if (m_comparisonView == nullptr || m_centralTabs == nullptr || !m_comparisonView->hasSession()) {
        return;
    }

    m_centralTabs->setCurrentWidget(m_comparisonView);
    if (m_fullscreenUiActive) {
        m_fullscreenContentMode = FullscreenContentMode::Compare;
        m_comparisonView->setFullscreenPresentation(true);
        setCentralTabBarVisible(false);
        hideFullscreenOverlayPanels();
        updateFullscreenInfoLine();
    }
    updateStatus(m_comparisonStatusMessage);
    updateActionState();
}

void MainWindow::clearComparisonSelection()
{
    m_comparisonSelectionOrder.clear();
}

void MainWindow::clearComparisonSession()
{
    if (m_comparisonView != nullptr) {
        m_comparisonView->clearSession();
    }
    updateComparisonTabTitle();
}

void MainWindow::updateComparisonTabTitle()
{
    if (m_centralTabs == nullptr || m_comparisonView == nullptr) {
        return;
    }
    const int index = m_centralTabs->indexOf(m_comparisonView);
    if (index < 0) {
        return;
    }
    const int count = m_comparisonView->itemCount();
    m_centralTabs->setTabText(index, count > 0
            ? tr("Compare (%1)").arg(count)
            : tr("Compare"));
}

ComparisonRenderSettings MainWindow::comparisonRenderSettings() const
{
    ComparisonRenderSettings settings;
    settings.exifAutorotate = m_settings.viewer.exifAutorotate;
    settings.scalingQuality = m_settings.viewer.scalingQuality;
    settings.colorManagementEnabled = m_settings.viewer.colorManagementEnabled;
    settings.devicePixelRatio = std::max(1.0, m_devicePixelRatio);
    return settings;
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

    const Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
    if (modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::ShiftModifier)) {
        return;
    }

    if (m_controller.setCurrentIndex(index.row())) {
        reloadAfterImageSwitch();
    }
}

void MainWindow::selectImageFromThumbnailList(const QModelIndex &index)
{
    selectImageFromFileList(index);
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
    return m_fileTreeModel->nodeForFolderPath(folderRelativePath);
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

void MainWindow::exitFullscreenMode()
{
    hideFullscreenOverlayPanels();

    const bool shouldRestoreUi = m_fullscreenUiActive;
    m_fullscreenUiActive = false;
    m_fullscreenContentMode = FullscreenContentMode::None;
    setCentralTabBarVisible(m_preFullscreenUiState.centralTabBarVisible);
    if (m_comparisonView != nullptr) {
        m_comparisonView->setFullscreenPresentation(false);
    }
    updateFullscreenInfoLine();
    if (m_fullscreenAction != nullptr) {
        m_fullscreenAction->setText(tr("Fullscreen"));
    }
    setWindowState(m_preFullscreenUiState.windowState);
    show();

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

    refreshNormalLayoutAfterFullscreen();
    QTimer::singleShot(0, this, [this]() {
        refreshNormalLayoutAfterFullscreen();
    });
    QTimer::singleShot(50, this, [this]() {
        refreshNormalLayoutAfterFullscreen();
    });
}

void MainWindow::refreshNormalLayoutAfterFullscreen()
{
    if (m_fullscreenUiActive || isFullScreen()) {
        return;
    }

    hideFullscreenOverlayPanels();
    updateFullscreenInfoLine();

    if (QLayout *mainLayout = layout()) {
        mainLayout->invalidate();
        mainLayout->activate();
    }

    if (QWidget *currentCentralWidget = centralWidget()) {
        currentCentralWidget->updateGeometry();
        currentCentralWidget->update();
    }

    if (m_fullscreenLeftPanel != nullptr) {
        m_fullscreenLeftPanel->setGeometry(QRect());
    }
    if (m_fullscreenRightPanel != nullptr) {
        m_fullscreenRightPanel->setGeometry(QRect());
    }
    if (m_fullscreenTopPanel != nullptr) {
        m_fullscreenTopPanel->setGeometry(QRect());
    }
    if (m_fullscreenBottomPanel != nullptr) {
        m_fullscreenBottomPanel->setGeometry(QRect());
    }

    statusBar()->setAutoFillBackground(true);
    statusBar()->setAttribute(Qt::WA_StyledBackground, true);
    statusBar()->updateGeometry();
    statusBar()->raise();
    statusBar()->update();
    if (m_statusLabel != nullptr) {
        m_statusLabel->updateGeometry();
        m_statusLabel->update();
    }

    updateGeometry();
    update();
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
    updateThumbnailActivation();
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

    if (m_fullscreenInfoLineLabel != nullptr) {
        m_fullscreenInfoLineLabel->move(FullscreenInfoLineMarginLeft, FullscreenInfoLineMarginTop);
    }
}

void MainWindow::updateFullscreenOverlayVisibility()
{
    if (!m_fullscreenUiActive
        || m_fullscreenContentMode != FullscreenContentMode::Viewer
        || !isFullScreen()
        || m_quickJumpDialogOpen) {
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
    updateThumbnailActivation();
    updateFullscreenInfoLine();
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
        syncThumbnailSelection();
        return;
    }

    const QModelIndex modelIndex = m_fileListModel->index(currentIndex, 0);
    selectionModel->setCurrentIndex(
        modelIndex,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current | QItemSelectionModel::Rows);
    m_fullscreenFileListView->scrollTo(modelIndex, QAbstractItemView::EnsureVisible);
    syncThumbnailSelection();
}

void MainWindow::updateFullscreenStatusText()
{
    if (m_fullscreenStatusLabel != nullptr && m_statusLabel != nullptr) {
        m_fullscreenStatusLabel->setText(m_statusLabel->text());
    }
}

void MainWindow::applyFullscreenInfoLineSettings()
{
    if (m_fullscreenInfoLineLabel == nullptr) {
        return;
    }

    QFont font = m_fullscreenInfoLineLabel->font();
    if (!m_settings.fullscreen.infoLineFontFamily.isEmpty()) {
        font.setFamily(m_settings.fullscreen.infoLineFontFamily);
    } else {
        font.setFamily(QFont().family());
    }
    font.setPointSize(m_settings.fullscreen.infoLineFontPointSize);
    m_fullscreenInfoLineLabel->setFont(font);

    const QColor color(m_settings.fullscreen.infoLineColor);
    const QString colorName = color.isValid()
        ? color.name(QColor::HexRgb)
        : core::AppSettings::defaults().fullscreen.infoLineColor;
    m_fullscreenInfoLineLabel->setStyleSheet(QStringLiteral(
        "#fullscreenInfoLine { color: %1; background: transparent; padding: 0; }").arg(colorName));
}

void MainWindow::updateFullscreenInfoLine()
{
    if (m_fullscreenInfoLineLabel == nullptr) {
        return;
    }

    const bool shouldShow = m_fullscreenUiActive
        && m_fullscreenContentMode == FullscreenContentMode::Viewer
        && isFullScreen()
        && m_settings.fullscreen.infoLineEnabled
        && m_controller.currentEntry() != nullptr;
    if (!shouldShow) {
        m_fullscreenInfoLineLabel->hide();
        return;
    }

    const StatusLineContext context {
        m_controller.currentEntry(),
        &m_currentDecoded,
        m_controller.zoomMode(),
        m_controller.zoomFactor(),
        m_controller.currentIndex(),
        m_controller.count(),
        {},
    };
    m_fullscreenInfoLineLabel->setText(formatFullscreenInfoLine(context));
    m_fullscreenInfoLineLabel->adjustSize();
    m_fullscreenInfoLineLabel->move(FullscreenInfoLineMarginLeft, FullscreenInfoLineMarginTop);
    m_fullscreenInfoLineLabel->show();
}

core::DecodeRequest MainWindow::currentDecodeRequest() const
{
    core::DecodeRequest request;
    request.viewportSize = m_imageView->size();
    request.panOffset = m_controller.panOffset();
    request.devicePixelRatio = m_imageView->devicePixelRatioF();
    request.rotationDegrees = m_controller.rotationDegrees();
    request.fitToWindow = m_controller.zoomMode() == core::ZoomMode::FitToWindow;
    request.regionRendering = m_settings.viewer.regionRenderingEnabled && !request.fitToWindow;
    request.exifAutorotate = m_settings.viewer.exifAutorotate;
    request.scalingQuality = m_settings.viewer.scalingQuality;
    request.colorManagementEnabled = m_settings.viewer.colorManagementEnabled;
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
