#pragma once

#include "core/decoded_image.h"

#include <QHash>
#include <QMutex>
#include <QString>
#include <QVector>

namespace insidious::core {

class MemoryImageCache {
public:
    explicit MemoryImageCache(qint64 byteBudget = 512LL * 1024LL * 1024LL);

    [[nodiscard]] bool contains(const QString &key) const;
    [[nodiscard]] bool tryGet(const QString &key, DecodedImage *image);
    void insert(const QString &key, const DecodedImage &image);
    [[nodiscard]] bool insertIfGeneration(const QString &key, const DecodedImage &image, quint64 expectedGeneration);
    void clear();

    [[nodiscard]] qint64 byteBudget() const;
    [[nodiscard]] qint64 bytesUsed() const;
    [[nodiscard]] int size() const;
    [[nodiscard]] quint64 generation() const;

private:
    struct CacheItem {
        DecodedImage image;
        qint64 bytes = 0;
        quint64 lastUsed = 0;
    };

    void evictIfNeeded();
    void insertLocked(const QString &key, const DecodedImage &image);

    mutable QMutex m_mutex;
    QHash<QString, CacheItem> m_items;
    qint64 m_byteBudget = 0;
    qint64 m_bytesUsed = 0;
    quint64 m_clock = 0;
    quint64 m_generation = 0;
};

} // namespace insidious::core
