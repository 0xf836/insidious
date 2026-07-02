#include "ui/thumbnail_loader.h"

#include "core/vips_image_decoder.h"

#include <QtConcurrent>

namespace insidious::ui {
namespace {

constexpr qint64 ThumbnailCacheBudgetBytes = 64LL * 1024LL * 1024LL;

} // namespace

ThumbnailLoader::ThumbnailLoader(QObject *parent)
    : QObject(parent)
{
    m_threadPool.setMaxThreadCount(1);
    connect(&m_watcher, &QFutureWatcher<ThumbnailResult>::finished, this, &ThumbnailLoader::handleFinished);
}

void ThumbnailLoader::setActiveGeneration(quint64 generation)
{
    m_activeGeneration = generation;
}

void ThumbnailLoader::clearQueue(quint64 generation)
{
    m_activeGeneration = generation;
    m_queue.clear();
    m_queuedCacheKeys.clear();
}

void ThumbnailLoader::clearCache()
{
    m_cache.clear();
    m_cacheBytes = 0;
}

void ThumbnailLoader::requestThumbnail(
    int row,
    const core::ImageEntry &entry,
    const core::DecodeRequest &request,
    const QString &archivePassword,
    quint64 generation)
{
    if (generation != m_activeGeneration) {
        return;
    }

    const QString cacheKey = core::cacheKeyFor(entry, request);
    QImage cached;
    if (tryGetCached(cacheKey, &cached)) {
        ThumbnailResult result;
        result.generation = generation;
        result.row = row;
        result.identityKey = entry.identityKey();
        result.cacheKey = cacheKey;
        result.image = cached;
        emit thumbnailReady(result);
        return;
    }

    if (cacheKey == m_activeCacheKey || m_queuedCacheKeys.contains(cacheKey)) {
        return;
    }

    Request queued;
    queued.generation = generation;
    queued.row = row;
    queued.identityKey = entry.identityKey();
    queued.cacheKey = cacheKey;
    queued.entry = entry;
    queued.decodeRequest = request;
    queued.archivePassword = archivePassword;
    m_queue.enqueue(queued);
    m_queuedCacheKeys.insert(cacheKey);
    startNext();
}

void ThumbnailLoader::startNext()
{
    if (m_watcher.isRunning()) {
        return;
    }

    while (!m_queue.isEmpty()) {
        const Request request = m_queue.dequeue();
        m_queuedCacheKeys.remove(request.cacheKey);
        if (request.generation != m_activeGeneration) {
            continue;
        }

        m_activeCacheKey = request.cacheKey;
        m_watcher.setFuture(QtConcurrent::run(&m_threadPool, [request]() {
            ThumbnailResult result;
            result.generation = request.generation;
            result.row = request.row;
            result.identityKey = request.identityKey;
            result.cacheKey = request.cacheKey;

            core::VipsImageDecoder decoder;
            const core::DecodedImage decoded = decoder.decode(
                request.entry,
                request.decodeRequest,
                request.archivePassword);
            if (decoded.ok()) {
                result.image = decoded.image;
            } else {
                result.errorMessage = decoded.errorMessage;
            }
            return result;
        }));
        return;
    }

    m_activeCacheKey.clear();
}

void ThumbnailLoader::handleFinished()
{
    const QFuture<ThumbnailResult> future = m_watcher.future();
    m_activeCacheKey.clear();
    if (!future.isCanceled() && future.resultCount() > 0 && future.isResultReadyAt(0)) {
        const ThumbnailResult result = future.result();
        if (result.generation == m_activeGeneration) {
            if (result.ok()) {
                insertCached(result.cacheKey, result.image);
                emit thumbnailReady(result);
            } else {
                emit thumbnailFailed(result);
            }
        }
    }
    startNext();
}

bool ThumbnailLoader::tryGetCached(const QString &cacheKey, QImage *image)
{
    auto it = m_cache.find(cacheKey);
    if (it == m_cache.end()) {
        return false;
    }

    it->lastUsed = ++m_cacheClock;
    if (image != nullptr) {
        *image = it->image;
    }
    return true;
}

void ThumbnailLoader::insertCached(const QString &cacheKey, const QImage &image)
{
    if (cacheKey.isEmpty() || image.isNull()) {
        return;
    }

    auto existing = m_cache.find(cacheKey);
    if (existing != m_cache.end()) {
        m_cacheBytes -= existing->bytes;
        m_cache.erase(existing);
    }

    CacheItem item;
    item.image = image;
    item.bytes = image.sizeInBytes();
    item.lastUsed = ++m_cacheClock;
    m_cache.insert(cacheKey, item);
    m_cacheBytes += item.bytes;
    evictCacheIfNeeded();
}

void ThumbnailLoader::evictCacheIfNeeded()
{
    while (m_cacheBytes > ThumbnailCacheBudgetBytes && !m_cache.isEmpty()) {
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

} // namespace insidious::ui
