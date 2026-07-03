#include "ui/comparison_view_widget.h"

#include "core/vips_image_decoder.h"

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedLayout>
#include <QStyle>
#include <QVariant>
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

QString comparisonModeName(ComparisonMode mode)
{
    switch (mode) {
    case ComparisonMode::Row:
        return QObject::tr("Row");
    case ComparisonMode::HoverSwitch:
        return QObject::tr("Hover Switch");
    }
    return QObject::tr("Row");
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

void ComparisonTileWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_panning = false;
        m_reorderDragging = false;
        unsetCursor();
        emit toggleFullscreenRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
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

ComparisonHoverSwitchWidget::ComparisonHoverSwitchWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(TileMinimumWidth, 160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void ComparisonHoverSwitchWidget::setTitles(const QString &firstTitle, const QString &secondTitle)
{
    m_titles[0] = firstTitle;
    m_titles[1] = secondTitle;
    update();
}

void ComparisonHoverSwitchWidget::setMessage(int index, const QString &message)
{
    if (index < 0 || index > 1) {
        return;
    }
    m_messages[index] = message;
    update();
}

void ComparisonHoverSwitchWidget::setDecodedImage(int index, const core::DecodedImage &decoded)
{
    if (index < 0 || index > 1) {
        return;
    }

    const bool preserveView = m_decoded[index].ok() && decoded.ok();
    m_decoded[index] = decoded;
    m_messages[index].clear();
    if (!preserveView && !m_decoded[1 - index].ok()) {
        resetView();
        return;
    }
    update();
}

void ComparisonHoverSwitchWidget::clearImages()
{
    m_decoded[0] = {};
    m_decoded[1] = {};
    m_messages[0].clear();
    m_messages[1].clear();
    resetView();
}

void ComparisonHoverSwitchWidget::resetView()
{
    m_zoomFactor = 1.0;
    m_panOffset = QPointF();
    m_panning = false;
    update();
}

QSize ComparisonHoverSwitchWidget::targetImageSize() const
{
    const QSize area = rect()
            .adjusted(TileMargin, TileTitleHeight, -TileMargin, -TileMargin)
            .size();
    return QSize(std::max(1, area.width()), std::max(1, area.height()));
}

void ComparisonHoverSwitchWidget::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void ComparisonHoverSwitchWidget::leaveEvent(QEvent *event)
{
    m_hovered = false;
    m_panning = false;
    update();
    QWidget::leaveEvent(event);
}

void ComparisonHoverSwitchWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));

    const QRect border = rect().adjusted(0, 0, -1, -1);
    painter.setPen(QPen(palette().color(hasFocus() ? QPalette::Highlight : QPalette::Mid), 1));
    painter.drawRect(border);

    const int index = activeIndex();
    const QRect titleRect(TileMargin, 0, width() - TileMargin * 2, TileTitleHeight);
    const QString prefix = index == 0 ? tr("Base") : tr("Hover");
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(
        titleRect,
        Qt::AlignVCenter | Qt::AlignLeft,
        painter.fontMetrics().elidedText(
            tr("%1: %2").arg(prefix, m_titles[index]),
            Qt::ElideRight,
            titleRect.width()));

    const QRect imageArea = rect().adjusted(TileMargin, TileTitleHeight, -TileMargin, -TileMargin);
    if (!m_decoded[index].ok()) {
        painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        painter.drawText(imageArea, Qt::AlignCenter | Qt::TextWordWrap, m_messages[index]);
        return;
    }

    painter.save();
    painter.setClipRect(imageArea);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(imageRect(index), m_decoded[index].image);
    painter.restore();
}

void ComparisonHoverSwitchWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_decoded[activeIndex()].ok()) {
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

void ComparisonHoverSwitchWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_lastMousePosition = event->pos();
    if (m_decoded[activeIndex()].ok() && m_zoomFactor > 1.0) {
        m_panning = true;
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ComparisonHoverSwitchWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_panning) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint delta = event->pos() - m_lastMousePosition;
    m_lastMousePosition = event->pos();
    m_panOffset += QPointF(delta);
    update();
    event->accept();
}

void ComparisonHoverSwitchWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ComparisonHoverSwitchWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_panning = false;
        emit toggleFullscreenRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

int ComparisonHoverSwitchWidget::activeIndex() const
{
    return m_hovered ? 1 : 0;
}

QSizeF ComparisonHoverSwitchWidget::imageLogicalSize(int index) const
{
    if (index < 0 || index > 1 || !m_decoded[index].ok()) {
        return QSizeF();
    }
    const QSizeF size = m_decoded[index].image.deviceIndependentSize();
    return QSizeF(std::ceil(size.width()), std::ceil(size.height()));
}

QRectF ComparisonHoverSwitchWidget::imageRect(int index) const
{
    const QRectF imageArea = rect().adjusted(TileMargin, TileTitleHeight, -TileMargin, -TileMargin);
    const QSizeF baseSize = imageLogicalSize(index);
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

    m_modeComboBox = new QComboBox(m_headerWidget);
    m_modeComboBox->addItem(comparisonModeName(ComparisonMode::Row), static_cast<int>(ComparisonMode::Row));
    m_modeComboBox->addItem(comparisonModeName(ComparisonMode::HoverSwitch), static_cast<int>(ComparisonMode::HoverSwitch));
    m_modeComboBox->setEnabled(false);
    connect(m_modeComboBox, &QComboBox::currentIndexChanged, this, [this](int index) {
        const ComparisonMode requestedMode = static_cast<ComparisonMode>(m_modeComboBox->itemData(index).toInt());
        if (!setMode(requestedMode)) {
            const QSignalBlocker blocker(m_modeComboBox);
            m_modeComboBox->setCurrentIndex(m_modeComboBox->findData(static_cast<int>(m_mode)));
        }
    });
    headerLayout->addWidget(m_modeComboBox);

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
    m_contentWidget = new QWidget(m_scrollArea);
    m_contentWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_contentLayout = new QStackedLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);

    m_rowWidget = new QWidget(m_contentWidget);
    m_rowWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_rowLayout = new QHBoxLayout(m_rowWidget);
    m_rowLayout->setContentsMargins(0, 0, 0, 0);
    m_rowLayout->setSpacing(TileSpacing);
    m_contentLayout->addWidget(m_rowWidget);

    m_hoverWidget = new ComparisonHoverSwitchWidget(m_contentWidget);
    connect(m_hoverWidget, &ComparisonHoverSwitchWidget::toggleFullscreenRequested, this, &ComparisonViewWidget::toggleFullscreenRequested);
    m_contentLayout->addWidget(m_hoverWidget);
    m_contentLayout->setCurrentWidget(m_rowWidget);

    m_scrollArea->setWidget(m_contentWidget);
    m_mainLayout->addWidget(m_scrollArea, 1);

    m_threadPool.setMaxThreadCount(2);
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(120);
    connect(&m_reloadTimer, &QTimer::timeout, this, &ComparisonViewWidget::reloadVisibleSession);
}

void ComparisonViewWidget::setSession(
    QVector<ComparisonSessionItem> items,
    const ComparisonRenderSettings &settings,
    ComparisonMode mode)
{
    clearSession();
    if (!supportsItemCount(mode, items.size())) {
        mode = ComparisonMode::Row;
    }
    m_items = std::move(items);
    m_settings = settings;
    m_mode = mode;
    ++m_generation;
    rebuildPresentation();
    scheduleReload();
    emit statusMessageChanged(tr("Compare: %1 images in %2").arg(m_items.size()).arg(comparisonModeName(m_mode)));
}

int ComparisonViewWidget::appendItems(QVector<ComparisonSessionItem> items)
{
    if (items.isEmpty()) {
        return 0;
    }

    int added = 0;
    if (m_items.isEmpty()) {
        m_settings = ComparisonRenderSettings {};
    }

    for (const ComparisonSessionItem &item : std::as_const(items)) {
        const QString identityKey = item.entry.identityKey();
        if (identityKey.isEmpty() || hasItem(identityKey)) {
            continue;
        }
        const int index = m_items.size();
        m_items.append(item);
        ComparisonTileWidget *tile = createTile(index);
        m_tiles.append(tile);
        m_rowLayout->addWidget(tile, 1);
        ++added;
    }

    if (added > 0) {
        rebuildPresentation();
        refreshTileIndexes();
        scheduleReload();
        emit statusMessageChanged(tr("Compare: %1 images in %2").arg(m_items.size()).arg(comparisonModeName(m_mode)));
    }
    return added;
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
    m_mode = ComparisonMode::Row;
    m_headerLabel->setText(tr("No comparison session"));
    if (m_modeComboBox != nullptr) {
        const QSignalBlocker blocker(m_modeComboBox);
        m_modeComboBox->setCurrentIndex(m_modeComboBox->findData(static_cast<int>(m_mode)));
        m_modeComboBox->setEnabled(false);
        m_modeComboBox->setItemData(
            m_modeComboBox->findData(static_cast<int>(ComparisonMode::HoverSwitch)),
            0,
            Qt::UserRole - 1);
    }
    if (m_contentLayout != nullptr && m_rowWidget != nullptr) {
        m_contentLayout->setCurrentWidget(m_rowWidget);
    }
    if (m_hoverWidget != nullptr) {
        m_hoverWidget->clearImages();
    }
    emit statusMessageChanged(QString());
}

bool ComparisonViewWidget::setMode(ComparisonMode mode)
{
    if (!supportsItemCount(mode, m_items.size())) {
        emit statusMessageChanged(mode == ComparisonMode::HoverSwitch
                ? tr("Compare: Hover Switch supports exactly 2 images")
                : tr("Compare: mode is not available for this session"));
        return false;
    }
    if (m_mode == mode) {
        return true;
    }

    m_mode = mode;
    ++m_generation;
    rebuildPresentation();
    scheduleReload();
    emit statusMessageChanged(tr("Compare: %1 images in %2").arg(m_items.size()).arg(comparisonModeName(m_mode)));
    return true;
}

bool ComparisonViewWidget::hasSession() const
{
    return !m_items.isEmpty();
}

int ComparisonViewWidget::itemCount() const
{
    return m_items.size();
}

bool ComparisonViewWidget::hasItem(const QString &identityKey) const
{
    if (identityKey.isEmpty()) {
        return false;
    }
    for (const ComparisonSessionItem &item : m_items) {
        if (item.entry.identityKey() == identityKey) {
            return true;
        }
    }
    return false;
}

ComparisonMode ComparisonViewWidget::mode() const
{
    return m_mode;
}

bool ComparisonViewWidget::canAppendItems(int uniqueItemCount) const
{
    if (uniqueItemCount <= 0) {
        return true;
    }
    return supportsItemCount(m_mode, m_items.size() + uniqueItemCount);
}

bool ComparisonViewWidget::supportsItemCount(ComparisonMode mode, int count)
{
    switch (mode) {
    case ComparisonMode::Row:
        return count >= 1;
    case ComparisonMode::HoverSwitch:
        return count == 2;
    }
    return false;
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

    for (int index = 0; index < m_items.size(); ++index) {
        ComparisonTileWidget *tile = createTile(index);
        m_tiles.append(tile);
        m_rowLayout->addWidget(tile, 1);
    }
}

void ComparisonViewWidget::rebuildPresentation()
{
    if (m_modeComboBox != nullptr) {
        const QSignalBlocker blocker(m_modeComboBox);
        m_modeComboBox->setEnabled(hasSession());
        m_modeComboBox->setCurrentIndex(m_modeComboBox->findData(static_cast<int>(m_mode)));
        const bool hoverSwitchEnabled = supportsItemCount(ComparisonMode::HoverSwitch, m_items.size());
        m_modeComboBox->setItemData(
            m_modeComboBox->findData(static_cast<int>(ComparisonMode::HoverSwitch)),
            hoverSwitchEnabled ? QVariant() : QVariant(0),
            Qt::UserRole - 1);
    }

    m_headerLabel->setText(hasSession()
            ? tr("Compare: %1 (%2 images)").arg(comparisonModeName(m_mode)).arg(m_items.size())
            : tr("No comparison session"));

    if (m_mode == ComparisonMode::Row) {
        if (m_contentLayout != nullptr && m_rowWidget != nullptr) {
            m_contentLayout->setCurrentWidget(m_rowWidget);
        }
        rebuildTiles();
        return;
    }

    while (QLayoutItem *item = m_rowLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            delete widget;
        }
        delete item;
    }
    m_tiles.clear();

    if (m_hoverWidget != nullptr && m_items.size() == 2) {
        m_hoverWidget->clearImages();
        m_hoverWidget->setTitles(itemTitle(m_items.at(0)), itemTitle(m_items.at(1)));
        m_hoverWidget->setMessage(0, tr("Loading..."));
        m_hoverWidget->setMessage(1, tr("Loading..."));
    }
    if (m_contentLayout != nullptr && m_hoverWidget != nullptr) {
        m_contentLayout->setCurrentWidget(m_hoverWidget);
    }
}

ComparisonTileWidget *ComparisonViewWidget::createTile(int index)
{
    auto *tile = new ComparisonTileWidget(m_rowWidget);
    tile->setIndex(index);
    tile->setTitle(itemTitle(m_items.at(index)));
    tile->setMessage(tr("Loading..."));
    connect(tile, &ComparisonTileWidget::reorderDropRequested, this, &ComparisonViewWidget::reorderTile);
    connect(tile, &ComparisonTileWidget::toggleFullscreenRequested, this, &ComparisonViewWidget::toggleFullscreenRequested);
    return tile;
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
    if (m_mode == ComparisonMode::HoverSwitch) {
        if (m_items.size() != 2) {
            return;
        }
        const QSize targetSize = targetHoverImageSize();
        startDecode(0, targetSize);
        startDecode(1, targetSize);
        return;
    }

    const QSize targetSize = targetTileImageSize();
    for (int index = 0; index < m_items.size(); ++index) {
        startDecode(index, targetSize);
    }
}

void ComparisonViewWidget::startDecode(int index, const QSize &targetSize)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }
    if (m_mode == ComparisonMode::Row && index >= m_tiles.size()) {
        return;
    }
    if (m_mode == ComparisonMode::HoverSwitch && (m_hoverWidget == nullptr || index > 1)) {
        return;
    }

    const core::DecodeRequest request = decodeRequestForSize(targetSize);
    const ComparisonSessionItem item = m_items.at(index);
    const QString cacheKey = core::cacheKeyFor(item.entry, request);
    core::DecodedImage cached;
    if (tryGetCached(cacheKey, &cached)) {
        if (m_mode == ComparisonMode::HoverSwitch) {
            m_hoverWidget->setDecodedImage(index, cached);
        } else {
            m_tiles.at(index)->setDecodedImage(cached);
        }
        return;
    }

    if (m_mode == ComparisonMode::HoverSwitch) {
        m_hoverWidget->setMessage(index, tr("Loading..."));
    } else {
        m_tiles.at(index)->setMessage(tr("Loading..."));
    }
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
    if (result.generation != m_generation || result.index < 0 || result.index >= m_items.size()) {
        return;
    }
    if (m_mode == ComparisonMode::Row && result.index >= m_tiles.size()) {
        return;
    }
    if (m_mode == ComparisonMode::HoverSwitch && (m_hoverWidget == nullptr || result.index > 1)) {
        return;
    }

    if (result.decoded.ok()) {
        insertCached(result.cacheKey, result.decoded);
        if (m_mode == ComparisonMode::HoverSwitch) {
            m_hoverWidget->setDecodedImage(result.index, result.decoded);
        } else {
            m_tiles.at(result.index)->setDecodedImage(result.decoded);
        }
    } else {
        const QString message = result.decoded.errorMessage.isEmpty()
                ? tr("Could not decode image.")
                : result.decoded.errorMessage;
        if (m_mode == ComparisonMode::HoverSwitch) {
            m_hoverWidget->setMessage(result.index, message);
        } else {
            m_tiles.at(result.index)->setMessage(message);
        }
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

QSize ComparisonViewWidget::targetHoverImageSize() const
{
    if (m_hoverWidget == nullptr) {
        return QSize(240, 360);
    }
    return m_hoverWidget->targetImageSize();
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
