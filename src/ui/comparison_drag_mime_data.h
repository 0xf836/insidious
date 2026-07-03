#pragma once

#include "ui/comparison_view_widget.h"

#include <QMimeData>
#include <QVector>

namespace insidious::ui {

class ComparisonDragMimeData final : public QMimeData {
public:
    explicit ComparisonDragMimeData(QVector<ComparisonSessionItem> items);

    [[nodiscard]] const QVector<ComparisonSessionItem> &items() const;
    [[nodiscard]] static QString mimeType();

private:
    QVector<ComparisonSessionItem> m_items;
};

} // namespace insidious::ui
