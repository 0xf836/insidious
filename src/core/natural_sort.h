#pragma once

#include <QString>

namespace insidious::core {

[[nodiscard]] int naturalCompare(const QString &left, const QString &right);
[[nodiscard]] bool naturalLess(const QString &left, const QString &right);

} // namespace insidious::core
