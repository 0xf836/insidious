#pragma once

#include "core/decoded_image.h"
#include "core/image_entry.h"

#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QThreadPool>

namespace insidious::ui {

struct ThumbnailResult {
    quint64 generation = 0;
    int row = -1;
    QString identityKey;
    QString cacheKey;
    QImage image;
    QString errorMessage;

    [[nodiscard]] bool ok() const { return errorMessage.isEmpty() && !image.isNull(); }
};

class ThumbnailLoader : public QObject {
    Q_OBJECT

public:
    explicit ThumbnailLoader(QObject *parent = nullptr);

    void setActiveGeneration(quint64 generation);
    void clearQueue(quint64 generation);
    void clearCache();
    void requestThumbnail(
        int row,
        const core::ImageEntry &entry,
        const core::DecodeRequest &request,
        const QString &archivePassword,
        quint64 generation);

signals:
    void thumbnailReady(const insidious::ui::ThumbnailResult &result);
    void thumbnailFailed(const insidious::ui::ThumbnailResult &result);

private:
    struct Request {
        quint64 generation = 0;
        int row = -1;
        QString identityKey;
        QString cacheKey;
        core::ImageEntry entry;
        core::DecodeRequest decodeRequest;
        QString archivePassword;
    };

    struct CacheItem {
        QImage image;
        qint64 bytes = 0;
        quint64 lastUsed = 0;
    };

    void startNext();
    void handleFinished();
    [[nodiscard]] bool tryGetCached(const QString &cacheKey, QImage *image);
    void insertCached(const QString &cacheKey, const QImage &image);
    void evictCacheIfNeeded();

    QThreadPool m_threadPool;
    QFutureWatcher<ThumbnailResult> m_watcher;
    QQueue<Request> m_queue;
    QSet<QString> m_queuedCacheKeys;
    QString m_activeCacheKey;
    quint64 m_activeGeneration = 0;
    QHash<QString, CacheItem> m_cache;
    qint64 m_cacheBytes = 0;
    quint64 m_cacheClock = 0;
};

} // namespace insidious::ui
