/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QObject>

#ifdef QGC_ENABLE_QML
#include <QQmlEngine>
#endif

/// Compatibility shim for core code that touches the QML engine.
/// With QGC_ENABLE_QML=OFF the core builds without Qt6::Qml, so QML engine
/// calls in non-UI code go through this header instead of QQmlEngine directly.

/// Marks a QObject as C++-owned so the QML engine never garbage-collects it.
/// No-op in headless (QGC_ENABLE_QML=OFF) builds where no QML engine exists.
inline void qgcSetCppOwnership(QObject *obj)
{
#ifdef QGC_ENABLE_QML
    QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership);
#else
    (void)obj;
#endif
}
