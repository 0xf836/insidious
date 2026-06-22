#pragma once

#include <QString>
#include <QStringList>

class QFileInfo;

namespace insidious::core {

[[nodiscard]] bool isSupportedImageExtension(QString suffix);
[[nodiscard]] bool isSupportedImageFile(const QFileInfo &fileInfo);
[[nodiscard]] QString formatHintFromSuffix(QString suffix);
[[nodiscard]] QStringList supportedImageNameFilters();

} // namespace insidious::core
