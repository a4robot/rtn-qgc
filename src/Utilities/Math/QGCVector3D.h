/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

/// @file
/// @brief Gui-free replacement for QVector3D.
///
/// QVector3D lives in Qt6::Gui. Using it anywhere in the headless (QML-OFF)
/// ghost build pulls libQt6Gui.so (and transitively libQt6DBus.so) into the
/// binary even though nothing is ever rendered there. This class provides
/// the minimal subset of QVector3D's API actually needed outside of
/// QML/rendering code (Joystick sensor data, ActuatorGeometry positions) so
/// those headless-reachable call sites no longer require Qt6::Gui.
/// See STRANGLER_MILESTONES.md Q8g (wave 17).

#include <QtCore/QDebug>
#include <QtCore/QMetaType>

class QGCVector3D
{
public:
    constexpr QGCVector3D() = default;
    constexpr QGCVector3D(float x, float y, float z) : _x(x), _y(y), _z(z) { }

    constexpr float x() const { return _x; }
    constexpr float y() const { return _y; }
    constexpr float z() const { return _z; }

    constexpr void setX(float x) { _x = x; }
    constexpr void setY(float y) { _y = y; }
    constexpr void setZ(float z) { _z = z; }

    constexpr float &operator[](int i) { return (i == 0) ? _x : ((i == 1) ? _y : _z); }
    constexpr float operator[](int i) const { return (i == 0) ? _x : ((i == 1) ? _y : _z); }

    bool operator==(const QGCVector3D &other) const = default;

private:
    float _x = 0.f;
    float _y = 0.f;
    float _z = 0.f;
};

Q_DECLARE_METATYPE(QGCVector3D)

inline QDebug operator<<(QDebug debug, const QGCVector3D &v)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "QGCVector3D(" << v.x() << ", " << v.y() << ", " << v.z() << ')';
    return debug;
}
