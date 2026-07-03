#pragma once

#include "core/app_settings.h"
#include "core/decoded_image.h"
#include "core/image_entry.h"

#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QPoint>
#include <QPointer>
#include <QScrollArea>
#include <QThreadPool>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QComboBox;
class QLabel;
class QStackedLayout;
class QVBoxLayout;

namespace insidious::ui {

struct ComparisonSessionItem {
    core::ImageEntry entry;
    QString archivePassword;
};

struct ComparisonRenderSettings {
    bool exifAutorotate = true;
    core::ScalingQuality scalingQuality = core::ScalingQuality::HighLanczos3;
    bool colorManagementEnabled = true;
    double devicePixelRatio = 1.0;
};

enum class ComparisonMode {
    Row,
    HoverSwitch,
};

class ComparisonTileWidget : public QWidget {
    Q_OBJECT

public:
    explicit ComparisonTileWidget(QWidget *parent = nullptr);

    void setIndex(int index);
    void setTitle(const QString &title);
    void setMessage(const QString &message);
    void setDecodedImage(const core::DecodedImage &decoded);
    void clearImage();
    void resetView();

signals:
    void reorderDropRequested(int sourceIndex, QPoint globalPosition);
    void focused(int index);
    void toggleFullscreenRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    [[nodiscard]] QSizeF imageLogicalSize() const;
    [[nodiscard]] QRectF imageRect() const;

    int m_index = -1;
    QString m_title;
    QString m_message;
    core::DecodedImage m_decoded;
    double m_zoomFactor = 1.0;
    QPointF m_panOffset;
    QPoint m_lastMousePosition;
    QPoint m_pressPosition;
    bool m_panning = false;
    bool m_reorderDragging = false;
};

class ComparisonHoverSwitchWidget : public QWidget {
    Q_OBJECT

public:
    explicit ComparisonHoverSwitchWidget(QWidget *parent = nullptr);

    void setTitles(const QString &firstTitle, const QString &secondTitle);
    void setMessage(int index, const QString &message);
    void setDecodedImage(int index, const core::DecodedImage &decoded);
    void clearImages();
    void resetView();
    [[nodiscard]] QSize targetImageSize() const;

signals:
    void toggleFullscreenRequested();

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    [[nodiscard]] int activeIndex() const;
    [[nodiscard]] QRectF imageAreaRect() const;
    [[nodiscard]] QRectF hoverSwitchRect() const;
    [[nodiscard]] QSizeF imageLogicalSize(int index) const;
    [[nodiscard]] QRectF imageRect(int index) const;
    void updateHoverState(QPointF position);

    QString m_titles[2];
    QString m_messages[2];
    core::DecodedImage m_decoded[2];
    double m_zoomFactor = 1.0;
    QPointF m_panOffset;
    QPoint m_lastMousePosition;
    QPointF m_lastHoverPosition;
    bool m_cursorInside = false;
    bool m_hovered = false;
    bool m_panning = false;
};

class ComparisonViewWidget : public QWidget {
    Q_OBJECT

public:
    explicit ComparisonViewWidget(QWidget *parent = nullptr);

    void setSession(
        QVector<ComparisonSessionItem> items,
        const ComparisonRenderSettings &settings,
        ComparisonMode mode = ComparisonMode::Row);
    void clearSession();
    int appendItems(QVector<ComparisonSessionItem> items);
    bool setMode(ComparisonMode mode);
    void setFullscreenPresentation(bool fullscreen);
    [[nodiscard]] bool hasSession() const;
    [[nodiscard]] int itemCount() const;
    [[nodiscard]] bool hasItem(const QString &identityKey) const;
    [[nodiscard]] ComparisonMode mode() const;
    [[nodiscard]] bool canAppendItems(int uniqueItemCount) const;
    [[nodiscard]] static bool supportsItemCount(ComparisonMode mode, int count);

signals:
    void closeRequested();
    void statusMessageChanged(const QString &message);
    void toggleFullscreenRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    struct DecodeResult {
        quint64 generation = 0;
        int index = -1;
        QString cacheKey;
        core::DecodedImage decoded;
    };

    struct CacheItem {
        core::DecodedImage decoded;
        qint64 bytes = 0;
        quint64 lastUsed = 0;
    };

    void rebuildTiles();
    void rebuildPresentation();
    ComparisonTileWidget *createTile(int index);
    void scheduleReload();
    void reloadVisibleSession();
    void startDecode(int index, const QSize &targetSize);
    void handleDecodeFinished(QFutureWatcher<DecodeResult> *watcher);
    [[nodiscard]] core::DecodeRequest decodeRequestForSize(const QSize &targetSize) const;
    [[nodiscard]] QSize targetTileImageSize() const;
    [[nodiscard]] QSize targetHoverImageSize() const;
    [[nodiscard]] bool tryGetCached(const QString &cacheKey, core::DecodedImage *decoded);
    void insertCached(const QString &cacheKey, const core::DecodedImage &decoded);
    void evictCacheIfNeeded();
    void reorderTile(int sourceIndex, QPoint globalPosition);
    void refreshTileIndexes();

    QVector<ComparisonSessionItem> m_items;
    ComparisonRenderSettings m_settings;
    ComparisonMode m_mode = ComparisonMode::Row;
    QWidget *m_headerWidget = nullptr;
    QLabel *m_headerLabel = nullptr;
    QComboBox *m_modeComboBox = nullptr;
    QVBoxLayout *m_mainLayout = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_contentWidget = nullptr;
    QStackedLayout *m_contentLayout = nullptr;
    QWidget *m_rowWidget = nullptr;
    QHBoxLayout *m_rowLayout = nullptr;
    ComparisonHoverSwitchWidget *m_hoverWidget = nullptr;
    QVector<ComparisonTileWidget *> m_tiles;
    QTimer m_reloadTimer;
    QThreadPool m_threadPool;
    QHash<QFutureWatcher<DecodeResult> *, int> m_decodeWatchers;
    QHash<QString, CacheItem> m_cache;
    qint64 m_cacheBytes = 0;
    quint64 m_cacheClock = 0;
    quint64 m_generation = 0;
};

} // namespace insidious::ui
