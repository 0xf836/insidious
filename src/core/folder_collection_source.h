#pragma once

#include "core/image_entry.h"

#include <QVector>

class QFileInfo;

namespace insidious::core {

enum class ScanErrorReason {
    None,
    Generic,
    PasswordRequired,
    InvalidPassword,
};

struct ScanResult {
    QVector<ImageEntry> entries;
    int selectedIndex = -1;
    QString errorMessage;
    ScanErrorReason errorReason = ScanErrorReason::None;

    [[nodiscard]] bool ok() const;
};

[[nodiscard]] ImageEntry imageEntryFromFileInfo(const QFileInfo &fileInfo);
[[nodiscard]] ScanResult scanFolder(const QString &folderPath, const QString &selectedPath = {});
[[nodiscard]] ScanResult singleImageCollection(const QString &filePath);

} // namespace insidious::core
