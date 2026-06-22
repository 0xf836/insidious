#include "core/memory_image_cache.h"

#include <QMutexLocker>

#include <algorithm>

namespace insidious::core {

MemoryImageCache::MemoryImageCache(qint64 byteBudget)
    : m_byteBudget(byteBudget)
{
}

bool MemoryImageCache::contains(const QString &key) const
{
    QMutexLocker locker(&m_mutex);
    return m_items.contains(key);
}

bool MemoryImageCache::tryGet(const QString &key, DecodedImage *image)
{
    QMutexLocker locker(&m_mutex);
    auto it = m_items.find(key);
    if (it == m_items.end()) {
        return false;
    }
    it->lastUsed = ++m_clock;
    if (image != nullptr) {
        *image = it->image;
    }
    return true;
}

void MemoryImageCache::insert(const QString &key, const DecodedImage &image)
{
    if (!image.ok()) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    const qint64 bytes = image.image.sizeInBytes();

    auto existing = m_items.find(key);
    if (existing != m_items.end()) {
        m_bytesUsed -= existing->bytes;
        m_items.erase(existing);
    }

    CacheItem item;
    item.image = image;
    item.bytes = bytes;
    item.lastUsed = ++m_clock;
    m_items.insert(key, item);
    m_bytesUsed += bytes;

    evictIfNeeded();
}

void MemoryImageCache::clear()
{
    QMutexLocker locker(&m_mutex);
    m_items.clear();
    m_bytesUsed = 0;
}

qint64 MemoryImageCache::byteBudget() const
{
    QMutexLocker locker(&m_mutex);
    return m_byteBudget;
}

qint64 MemoryImageCache::bytesUsed() const
{
    QMutexLocker locker(&m_mutex);
    return m_bytesUsed;
}

int MemoryImageCache::size() const
{
    QMutexLocker locker(&m_mutex);
    return m_items.size();
}

void MemoryImageCache::evictIfNeeded()
{
    while (m_bytesUsed > m_byteBudget && !m_items.isEmpty()) {
        auto oldest = m_items.begin();
        for (auto it = m_items.begin(); it != m_items.end(); ++it) {
            if (it->lastUsed < oldest->lastUsed) {
                oldest = it;
            }
        }
        m_bytesUsed -= oldest->bytes;
        m_items.erase(oldest);
    }
}

} // namespace insidious::core
