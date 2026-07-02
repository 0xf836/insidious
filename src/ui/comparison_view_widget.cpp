#include "ui/comparison_view_widget.h"

#include "core/vips_image_decoder.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace insidious::ui {
namespace {

constexpr int TileMinimumWidth = 24;
constexpr int TileMargin = 10;
constexpr int TileTitleHeight = 28;
constexpr int TileSpacing = 10;
constexpr qint64 ComparisonCacheBudgetBytes = 384LL * 1024LL * 1024LL;

QString itemTitle(const ComparisonSessionItem &item)
{
    return item.entry.displayName.isEmpty() ? item.entry.sourceRelativePath : item.entry.displayName;
}

double sanitizedDevicePixelRatio(double dpr)
{
    if (!std::isfinite(dpr) || dpr <= 0.0) {
        return 1.0;
    }
    return std::clamp(dpr, 0.25, 8.0);
}

} // namespace

ComparisonTileWidget::ComparisonTileWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(TileMinimumWidth, 160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void ComparisonTileWidget::setIndex(int index)
{
    m_index = index;
}

void ComparisonTileWidget::setTitle(const QString &title)
{
    m_title = title;
    update();
}

void ComparisonTileWidget::setMessage(const QString &message)
{
    m_message = message;
    update();
}

void ComparisonTileWidget::setDecodedImage(const core::DecodedImage &decoded)
{
    const bool preserveView = m_decoded.ok() && decoded.ok();
    QPointF normalizedCenter(0.5, 0.5);
    if (preserveView) {
        const QRectF area = rect().adjusted(TileMargin, TileTitleHeight, -TileMargin, -TileMargin);
        const QRectF oldRect = imageRect();
        if (!area.isEmpty() && !oldRect.isEmpty() && m_zoomFactor > 0.0) {
            const QPointF imagePoint = (area.center() - oldRect.topLeft()) / m_zoomFactor;
            const QSizeF oldSize = imageLogicalSize();
            if (!oldSize.isEmpty()) {
                normalizedCenter = QPointF(
                    imagePoint.x() / oldSize.width(),
                    imagePoint.y() / oldSize.height());
            }
        }
    }

    m_decoded = decoded;
    m_message.clear();
    if (!preserveView) {
        resetView();
        return;
    }

    const QRectF area = rect().adjusted(TileMargin, TileTitleHeight, -TileMargin, -TileMargin);
    const QSizeF newSize = imageLogicalSize();
    if (!area.isEmpty() && !newSize.isEmpty()) {
        const QSizeF drawSize(newSize.width() * m_zoomFactor, newSize.height() * m_zoomFactor);
        const QPointF defaultTopLeft(
            area.x() + (area.width() - drawSize.width()) / 2.0,
            area.y() + (area.height() - drawSize.height()) / 2.0);
        const QPointF desiredTopLeft = area.center() - QPointF(
            normalizedCenter.x() * newSize.width() * m_zoomFactor,
            normalizedCenter.y() * newSize.height() * m_zoomFactor);
        m_panOffset = desiredTopLeft - defaultTopLeft;
    }
    update();
}

void ComparisonTileWidget::clearImage()
{
    m_decoded = {};
    resetView();
    update();
}

void ComparisonTileWidget::resetView()
{
    m_zoomFactor = 1.0;
    m_panOffset = QPointF();
    m_panning = false;
    m_reorderDragging = false;
    update();
}

void ComparisonTileWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));

    const QRect border = rect().adjusted(0, 0, -1, -1);
    painter.setPen(QPen(palette().color(hasFocus() ? QPalette::Highlight : QPalette::Mid), 1));
    painter.drawRect(border);

    const QRect titleRect(TileMargin, 0, width() - TileMargin * 2, TileTitleHeight);
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(
        titleRect,
        Qt::AlignVCenter | Qt::AlignLeft,
        painter.fontMetrics().elidedText(m_title, Qt::ElideRight, titleRect.width()));

    const QRect imageArea = rect().adjusted(TileMargin, TileTitleHeight, -TileMargin, -TileMargin);
    if (!m_decoded.ok()) {
        painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        painter.drawText(imageArea, Qt::AlignCenter | Qt::TextWordWrap, m_message);
        return;
    }

    painter.save();
    painter.setClipRect(imageArea);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(imageRect(), m_decoded.image);
    painter.restore();
}

void ComparisonTileWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_decoded.ok()) {
        event->ignore();
        return;
    }

    const double multiplier = event->angleDelta().y() > 0 ? 1.25 : 0.8;
    m_zoomFactor = std::clamp(m_zoomFactor * multiplier, 1.0, 16.0);
    if (qFuzzyCompare(m_zoomFactor, 1.0)) {
        m_panOffset = QPointF();
    }
    update();
    event->accept();
}

void ComparisonTileWidget::mousePressEvent(QMouseEvent *event)
{
    emit focused(m_index);
    setFocus(Qt::MouseFocusReason);
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_pressPosition = event->pos();
    m_lastMousePosition = event->pos();
    if (m_decoded.ok() && m_zoomFactor > 1.0) {
        m_panning = true;
    } else {
        m_reorderDragging = true;
    }
    event->accept();
}

void ComparisonTileWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastMousePosition;
        m_lastMousePosition = event->pos();
        m_panOffset += QPointF(delta);
        update();
        event->accept();
        return;
    }

    if (m_reorderDragging && (event->pos() - m_pressPosition).manhattanLength() > QApplication::startDragDistance()) {
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void ComparisonTileWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_reorderDragging) {
        unsetCursor();
        m_reorderDragging = false;
        if ((event->pos() - m_pressPosition).manhattanLength() > QApplication::startDragDistance()) {
            emit reorderDropRequested(m_index, event->globalPosition().toPoint());
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

QSizeF ComparisonTileWidget::imageLogicalSize() const
{
    if (!m_decoded.ok()) {
        return QSizeF();
    }
    const QSizeF size = m_decoded.image.deviceIndependentSize();
    return QSizeF(std::ceil(size.width()), std::ceil(size.height()));
}

QRectF ComparisonTileWidget::imageRect() const
{
    const QRectF imageArea = rect().adjusted(TileMargin, TileTitleHeight, -TileMargin, -TileMargin);
    const QSizeF baseSize = imageLogicalSize();
    if (baseSize.isEmpty()) {
        return QRectF();
    }

    const QSizeF drawSize(baseSize.width() * m_zoomFactor, baseSize.height() * m_zoomFactor);
    return QRectF(
        imageArea.x() + (imageArea.width() - drawSize.width()) / 2.0 + m_panOffset.x(),
        imageArea.y() + (imageArea.height() - drawSize.height()) / 2.0 + m_panOffset.y(),
        drawSize.width(),
        drawSize.height());
}

ComparisonViewWidget::ComparisonViewWidget(QWidget *parent)
    : QWidget(parent)
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(8, 8, 8, 8);
    m_mainLayout->setSpacing(8);

    m_headerWidget = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    m_headerLabel = new QLabel(tr("No comparison session"), m_headerWidget);
    headerLayout->addWidget(m_headerLabel, 1);

    auto *closeButton = new QPushButton(tr("Close Compare"), m_headerWidget);
    connect(closeButton, &QPushButton::clicked, this, [this]() {
        clearSession();
        emit closeRequested();
    });
    headerLayout->addWidget(closeButton);
    m_mainLayout->addWidget(m_headerWidget);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_rowWidget = new QWidget(m_scrollArea);
    m_rowWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_rowLayout = new QHBoxLayout(m_rowWidget);
    m_rowLayout->setContentsMargins(0, 0, 0, 0);
    m_rowLayout->setSpacing(TileSpacing);
    m_scrollArea->setWidget(m_rowWidget);
    m_mainLayout->addWidget(m_scrollArea, 1);

    m_threadPool.setMaxThreadCount(2);
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(120);
    connect(&m_reloadTimer, &QTimer::timeout, this, &ComparisonViewWidget::reloadVisibleSession);
}

void ComparisonViewWidget::setSession(
    QVector<ComparisonSessionItem> items,
    const ComparisonRenderSettings &settings)
{
    clearSession();
    m_items = std::move(items);
    m_settings = settings;
    ++m_generation;
    rebuildTiles();
    scheduleReload();
    emit statusMessageChanged(tr("Compare: %1 images in row").arg(m_items.size()));
}

void ComparisonViewWidget::clearSession()
{
    ++m_generation;
    for (auto it = m_decodeWatchers.begin(); it != m_decodeWatchers.end(); ++it) {
        QFutureWatcher<DecodeResult> *watcher = it.key();
        disconnect(watcher, nullptr, this, nullptr);
        watcher->cancel();
        watcher->deleteLater();
    }
    m_decodeWatchers.clear();
    m_cache.clear();
    m_cacheBytes = 0;
    while (QLayoutItem *item = m_rowLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            delete widget;
        }
        delete item;
    }
    m_items.clear();
    m_tiles.clear();
    m_headerLabel->setText(tr("No comparison session"));
    emit statusMessageChanged(QString());
}

bool ComparisonViewWidget::hasSession() const
{
    return !m_items.isEmpty();
}

void ComparisonViewWidget::setFullscreenPresentation(bool fullscreen)
{
    if (m_headerWidget != nullptr) {
        m_headerWidget->setVisible(!fullscreen);
    }
    if (m_mainLayout != nullptr) {
        m_mainLayout->setContentsMargins(fullscreen ? QMargins() : QMargins(8, 8, 8, 8));
        m_mainLayout->setSpacing(fullscreen ? 0 : 8);
    }
    scheduleReload();
}

void ComparisonViewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (hasSession()) {
        scheduleReload();
    }
}

void ComparisonViewWidget::rebuildTiles()
{
    while (QLayoutItem *item = m_rowLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            delete widget;
        }
        delete item;
    }
    m_tiles.clear();

    m_headerLabel->setText(tr("Compare: Row (%1 images)").arg(m_items.size()));
    for (int index = 0; index < m_items.size(); ++index) {
        auto *tile = new ComparisonTileWidget(m_rowWidget);
        tile->setIndex(index);
        tile->setTitle(itemTitle(m_items.at(index)));
        tile->setMessage(tr("Loading..."));
        connect(tile, &ComparisonTileWidget::reorderDropRequested, this, &ComparisonViewWidget::reorderTile);
        m_tiles.append(tile);
        m_rowLayout->addWidget(tile, 1);
    }
}

void ComparisonViewWidget::scheduleReload()
{
    m_reloadTimer.start();
}

void ComparisonViewWidget::reloadVisibleSession()
{
    if (m_items.isEmpty()) {
        return;
    }

    ++m_generation;
    const QSize targetSize = targetTileImageSize();
    for (int index = 0; index < m_items.size(); ++index) {
        startDecode(index, targetSize);
    }
}

void ComparisonViewWidget::startDecode(int index, const QSize &targetSize)
{
    if (index < 0 || index >= m_items.size() || index >= m_tiles.size()) {
        return;
    }

    const core::DecodeRequest request = decodeRequestForSize(targetSize);
    const ComparisonSessionItem item = m_items.at(index);
    const QString cacheKey = core::cacheKeyFor(item.entry, request);
    core::DecodedImage cached;
    if (tryGetCached(cacheKey, &cached)) {
        m_tiles.at(index)->setDecodedImage(cached);
        return;
    }

    m_tiles.at(index)->setMessage(tr("Loading..."));
    const quint64 generation = m_generation;
    auto *watcher = new QFutureWatcher<DecodeResult>(this);
    m_decodeWatchers.insert(watcher, index);
    connect(watcher, &QFutureWatcher<DecodeResult>::finished, this, [this, watcher]() {
        handleDecodeFinished(watcher);
    });
    watcher->setFuture(QtConcurrent::run(&m_threadPool, [generation, index, cacheKey, item, request]() {
        core::VipsImageDecoder decoder;
        DecodeResult result;
        result.generation = generation;
        result.index = index;
        result.cacheKey = cacheKey;
        result.decoded = decoder.decode(item.entry, request, item.archivePassword);
        return result;
    }));
}

void ComparisonViewWidget::handleDecodeFinished(QFutureWatcher<DecodeResult> *watcher)
{
    if (watcher == nullptr) {
        return;
    }

    m_decodeWatchers.remove(watcher);
    const QFuture<DecodeResult> future = watcher->future();
    watcher->deleteLater();
    if (future.isCanceled() || future.resultCount() == 0 || !future.isResultReadyAt(0)) {
        return;
    }

    const DecodeResult result = future.result();
    if (result.generation != m_generation || result.index < 0 || result.index >= m_tiles.size()) {
        return;
    }

    if (result.decoded.ok()) {
        insertCached(result.cacheKey, result.decoded);
        m_tiles.at(result.index)->setDecodedImage(result.decoded);
    } else {
        m_tiles.at(result.index)->setMessage(result.decoded.errorMessage.isEmpty()
                ? tr("Could not decode image.")
                : result.decoded.errorMessage);
    }
}

core::DecodeRequest ComparisonViewWidget::decodeRequestForSize(const QSize &targetSize) const
{
    core::DecodeRequest request;
    request.viewportSize = targetSize.expandedTo(QSize(1, 1));
    request.devicePixelRatio = sanitizedDevicePixelRatio(m_settings.devicePixelRatio);
    request.fitToWindow = true;
    request.regionRendering = false;
    request.exifAutorotate = m_settings.exifAutorotate;
    request.scalingQuality = m_settings.scalingQuality;
    request.colorManagementEnabled = m_settings.colorManagementEnabled;
    request.maxDecodedBytes = 384LL * 1024LL * 1024LL;
    return request;
}

QSize ComparisonViewWidget::targetTileImageSize() const
{
    if (m_scrollArea == nullptr || m_scrollArea->viewport() == nullptr) {
        return QSize(240, 360);
    }

    const QSize viewportSize = m_scrollArea->viewport()->size();
    const int count = std::max(1, static_cast<int>(m_items.size()));
    const int totalSpacing = TileSpacing * std::max(0, count - 1);
    const int tileWidth = std::max(
        1,
        (viewportSize.width() - totalSpacing) / count);
    return QSize(
        std::max(1, tileWidth - TileMargin * 2),
        std::max(1, viewportSize.height() - TileTitleHeight - TileMargin * 2));
}

bool ComparisonViewWidget::tryGetCached(const QString &cacheKey, core::DecodedImage *decoded)
{
    auto it = m_cache.find(cacheKey);
    if (it == m_cache.end()) {
        return false;
    }

    it->lastUsed = ++m_cacheClock;
    if (decoded != nullptr) {
        *decoded = it->decoded;
    }
    return true;
}

void ComparisonViewWidget::insertCached(const QString &cacheKey, const core::DecodedImage &decoded)
{
    if (cacheKey.isEmpty() || !decoded.ok()) {
        return;
    }

    auto existing = m_cache.find(cacheKey);
    if (existing != m_cache.end()) {
        m_cacheBytes -= existing->bytes;
        m_cache.erase(existing);
    }

    CacheItem item;
    item.decoded = decoded;
    item.bytes = decoded.image.sizeInBytes();
    item.lastUsed = ++m_cacheClock;
    m_cache.insert(cacheKey, item);
    m_cacheBytes += item.bytes;
    evictCacheIfNeeded();
}

void ComparisonViewWidget::evictCacheIfNeeded()
{
    while (m_cacheBytes > ComparisonCacheBudgetBytes && !m_cache.isEmpty()) {
        auto oldest = m_cache.begin();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
            if (it->lastUsed < oldest->lastUsed) {
                oldest = it;
            }
        }
        m_cacheBytes -= oldest->bytes;
        m_cache.erase(oldest);
    }
}

void ComparisonViewWidget::reorderTile(int sourceIndex, QPoint globalPosition)
{
    if (sourceIndex < 0 || sourceIndex >= m_items.size()) {
        return;
    }

    int targetIndex = sourceIndex;
    for (int index = 0; index < m_tiles.size(); ++index) {
        ComparisonTileWidget *tile = m_tiles.at(index);
        if (tile != nullptr && tile->rect().contains(tile->mapFromGlobal(globalPosition))) {
            targetIndex = index;
            break;
        }
    }

    if (targetIndex == sourceIndex) {
        return;
    }

    m_items.move(sourceIndex, targetIndex);
    m_tiles.move(sourceIndex, targetIndex);
    while (QLayoutItem *item = m_rowLayout->takeAt(0)) {
        delete item;
    }
    for (ComparisonTileWidget *tile : std::as_const(m_tiles)) {
        m_rowLayout->addWidget(tile, 1);
    }
    refreshTileIndexes();
    scheduleReload();
}

void ComparisonViewWidget::refreshTileIndexes()
{
    for (int index = 0; index < m_tiles.size(); ++index) {
        m_tiles.at(index)->setIndex(index);
    }
}

} // namespace insidious::ui
