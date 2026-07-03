#include "ui/comparison_drag_mime_data.h"

#include <utility>

namespace insidious::ui {

ComparisonDragMimeData::ComparisonDragMimeData(QVector<ComparisonSessionItem> items)
    : m_items(std::move(items))
{
    setData(mimeType(), QByteArrayLiteral("1"));
}

const QVector<ComparisonSessionItem> &ComparisonDragMimeData::items() const
{
    return m_items;
}

QString ComparisonDragMimeData::mimeType()
{
    return QStringLiteral("application/x-insidious-comparison-items");
}

} // namespace insidious::ui
