#pragma once

#include <QDateTime>
#include <QString>

namespace insidious::core {

struct ImageEntry {
    QString displayName;
    QString absolutePath;
    qint64 fileSize = 0;
    QDateTime modifiedTime;
    QString formatHint;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] QString identityKey() const;
};

} // namespace insidious::core
