#pragma once

#include <QtCore/QList>
#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QtTypes>

namespace QGC
{
    float limitAngleToPMPIf(double angle);
    double limitAngleToPMPId(double angle);

    /// Returns true if the two values are equal or close. Correctly handles 0 and NaN values.
    bool fuzzyCompare(double value1, double value2);
    bool fuzzyCompare(float value1, float value2);
    bool fuzzyCompare(double value1, double value2, double tolerance);
    bool fuzzyCompare(float value1, float value2, float tolerance);

    quint32 crc32(const quint8 *src, unsigned len, unsigned state);

    /// Axis-aligned bounding box of a point list. Gui-free replacement for
    /// QPolygonF::boundingRect() (QPolygonF lives in Qt6::Gui). See
    /// STRANGLER_MILESTONES.md Q8g (wave 17).
    QRectF polygonBoundingRect(const QList<QPointF> &points);

    /// Point-in-polygon test using the even-odd (crossing number) rule --
    /// same semantics as QPolygonF::containsPoint(pt, Qt::OddEvenFill),
    /// which is Qt6::Gui-only. See STRANGLER_MILESTONES.md Q8g (wave 17).
    bool polygonContainsPoint(const QList<QPointF> &points, const QPointF &point);
}
