#include "core/natural_sort.h"

#include <algorithm>

namespace insidious::core {
namespace {

bool isAsciiDigit(QChar value)
{
    return value >= QLatin1Char('0') && value <= QLatin1Char('9');
}

int compareNumberChunks(const QString &left, qsizetype &leftIndex, const QString &right, qsizetype &rightIndex)
{
    const qsizetype leftStart = leftIndex;
    const qsizetype rightStart = rightIndex;

    while (leftIndex < left.size() && isAsciiDigit(left.at(leftIndex))) {
        ++leftIndex;
    }
    while (rightIndex < right.size() && isAsciiDigit(right.at(rightIndex))) {
        ++rightIndex;
    }

    qsizetype leftSignificant = leftStart;
    qsizetype rightSignificant = rightStart;
    while (leftSignificant < leftIndex && left.at(leftSignificant) == QLatin1Char('0')) {
        ++leftSignificant;
    }
    while (rightSignificant < rightIndex && right.at(rightSignificant) == QLatin1Char('0')) {
        ++rightSignificant;
    }

    const qsizetype leftLength = leftIndex - leftSignificant;
    const qsizetype rightLength = rightIndex - rightSignificant;
    if (leftLength != rightLength) {
        return leftLength < rightLength ? -1 : 1;
    }

    for (qsizetype offset = 0; offset < leftLength; ++offset) {
        const QChar leftChar = left.at(leftSignificant + offset);
        const QChar rightChar = right.at(rightSignificant + offset);
        if (leftChar != rightChar) {
            return leftChar < rightChar ? -1 : 1;
        }
    }

    const qsizetype leftRawLength = leftIndex - leftStart;
    const qsizetype rightRawLength = rightIndex - rightStart;
    if (leftRawLength != rightRawLength) {
        return leftRawLength < rightRawLength ? -1 : 1;
    }

    return 0;
}

} // namespace

int naturalCompare(const QString &left, const QString &right)
{
    const QString leftFolded = left.toCaseFolded();
    const QString rightFolded = right.toCaseFolded();

    qsizetype leftIndex = 0;
    qsizetype rightIndex = 0;

    while (leftIndex < leftFolded.size() && rightIndex < rightFolded.size()) {
        const bool leftDigit = isAsciiDigit(leftFolded.at(leftIndex));
        const bool rightDigit = isAsciiDigit(rightFolded.at(rightIndex));

        if (leftDigit && rightDigit) {
            const int numberResult = compareNumberChunks(leftFolded, leftIndex, rightFolded, rightIndex);
            if (numberResult != 0) {
                return numberResult;
            }
            continue;
        }

        const QChar leftChar = leftFolded.at(leftIndex);
        const QChar rightChar = rightFolded.at(rightIndex);
        if (leftChar != rightChar) {
            return leftChar < rightChar ? -1 : 1;
        }
        ++leftIndex;
        ++rightIndex;
    }

    if (leftIndex != leftFolded.size() || rightIndex != rightFolded.size()) {
        return leftFolded.size() < rightFolded.size() ? -1 : 1;
    }

    return QString::compare(left, right, Qt::CaseSensitive);
}

bool naturalLess(const QString &left, const QString &right)
{
    return naturalCompare(left, right) < 0;
}

} // namespace insidious::core
