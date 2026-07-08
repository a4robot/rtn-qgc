#pragma once

#include <QtQuick/QQuickItem>
#include <QtQmlIntegration/QtQmlIntegration>

#include "FactValueGridModel.h"

/// QML-registered view for FactValueGridModel. All grid logic (rows/columns/Fact wiring,
/// settings persistence) lives in the Quick-free FactValueGridModel base so headless
/// callers (SubtitleWriter, QGCCorePlugin::factValueGridCreateDefaultSettings) can use it
/// without linking Qt6::Quick. This class exists only to preserve the QQuickItem-ness and
/// "FactValueGrid" QML type registration that HorizontalFactValueGrid (and
/// HorizontalFactValueGrid.qml's HorizontalFactValueGridTemplate root) depend on.
class FactValueGrid : public FactValueGridModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
public:
    explicit FactValueGrid(QQuickItem *parent = nullptr);

private:
    Q_DISABLE_COPY(FactValueGrid)
};

QML_DECLARE_TYPE(FactValueGrid)
