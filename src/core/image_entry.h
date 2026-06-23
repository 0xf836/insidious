#pragma once

#include <QDateTime>
#include <QString>

namespace insidious::core {

enum class ImageEntrySource {
    FileSystem,
    Archive,
};

struct ImageEntry {
    ImageEntrySource source = ImageEntrySource::FileSystem;
    QString displayName;
    QString absolutePath;
    QString archivePath;
    QString archiveEntryPath;
    qint64 fileSize = 0;
    QDateTime modifiedTime;
    QString formatHint;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] QString identityKey() const;
};

} // namespace insidious::core
