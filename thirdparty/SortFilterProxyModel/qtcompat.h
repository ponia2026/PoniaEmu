#ifndef QQSFPM_QTCOMPAT_H
#define QQSFPM_QTCOMPAT_H

#include <QtGlobal>
#include <QVariant>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtCore5Compat/QRegExp>
#else
#include <QRegExp>
#endif

namespace qqsfpm {

// Qt 6 changed the size type of QQmlListProperty callbacks
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
using ListSizeType = qsizetype;
#else
using ListSizeType = int;
#endif

// Qt 6 removed the relational operators of QVariant.
// Returns a negative number, zero or a positive number like strcmp;
// values that cannot be ordered compare as equal.
inline int compareVariants(const QVariant& lhs, const QVariant& rhs)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPartialOrdering order = QVariant::compare(lhs, rhs);
    if (order == QPartialOrdering::Less)
        return -1;
    if (order == QPartialOrdering::Greater)
        return 1;
    return 0;
#else
    if (lhs < rhs)
        return -1;
    if (lhs > rhs)
        return 1;
    return 0;
#endif
}

}

#endif // QQSFPM_QTCOMPAT_H
