#pragma once

#include <QtQmlIntegration/QtQmlIntegration>

#include "FactValueGrid.h"

class InstrumentValueData;

class HorizontalFactValueGrid : public FactValueGrid
{
    Q_OBJECT
    QML_NAMED_ELEMENT(HorizontalFactValueGridTemplate)

public:
    explicit HorizontalFactValueGrid(QQuickItem *parent = nullptr);

    // telemetryBarSettingsGroup/vehicleCardSettingsGroup are inherited from
    // FactValueGridModel (they live there so headless callers can reference them
    // without pulling in this QQuickItem-based view class).
    Q_PROPERTY(QString telemetryBarSettingsGroup    MEMBER telemetryBarSettingsGroup    CONSTANT)
    Q_PROPERTY(QString vehicleCardSettingsGroup     MEMBER vehicleCardSettingsGroup     CONSTANT)

private:
    Q_DISABLE_COPY(HorizontalFactValueGrid)
};

QML_DECLARE_TYPE(HorizontalFactValueGrid)
