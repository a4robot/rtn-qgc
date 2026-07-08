#include "QmlObjectListModel.h"
#include "FactValueGridModel.h"
#include "InstrumentValueData.h"
#include "AppMessages.h"
#include "QGCCorePlugin.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QSettings>
#include <QtCore/QDir>

QStringList FactValueGridModel::_iconNames;

QList<FactValueGridModel*> FactValueGridModel::_vehicleCardInstanceList;

// for activeVehicle telem table
const QString FactValueGridModel::telemetryBarSettingsGroup(QStringLiteral("TelemetryBarUserSettings"));

// for multi-vehicle list telem tables
const QString FactValueGridModel::vehicleCardSettingsGroup(QStringLiteral("VehicleCardUserSettings"));

FactValueGridModel::FactValueGridModel(FactValueGridModelBase* parent)
    : FactValueGridModelBase(parent)
    , _columns      (new QmlObjectListModel(this))
    , _fontSizeNames({
        QCoreApplication::translate("FactValueGrid", "Default"),
        QCoreApplication::translate("FactValueGrid", "Small"),
        QCoreApplication::translate("FactValueGrid", "Medium"),
        QCoreApplication::translate("FactValueGrid", "Large"),
    })
{
    if (_iconNames.isEmpty()) {
        QDir iconDir(":/InstrumentValueIcons/");
        _iconNames = iconDir.entryList();
    }
}

void FactValueGridModel::componentComplete(void)
{
#ifdef QGC_ENABLE_QML
    FactValueGridModelBase::componentComplete();
#endif

    connect(this, &FactValueGridModel::fontSizeChanged, this, &FactValueGridModel::_saveSettings);

    if (_specificVehicleForCard) {
        _vehicleCardInstanceList.append(this);
        _initForNewVehicle(_specificVehicleForCard);
    } else {
        // We are not tracking a specific vehicle so we need to track the active vehicle or offline editing vehicle if not active vehicle
        auto multiVehicleManager = MultiVehicleManager::instance();
        connect(multiVehicleManager, &MultiVehicleManager::activeVehicleChanged, this, &FactValueGridModel::_activeVehicleChanged);
        _activeVehicle = multiVehicleManager->activeVehicle();
        if (!_activeVehicle) {
            _activeVehicle = multiVehicleManager->offlineEditingVehicle();
        }
        _initForNewVehicle(_activeVehicle);
    }
}

void FactValueGridModel::_initForNewVehicle(Vehicle* vehicle)
{
    if (!vehicle) {
        qCritical() << "FactValueGridModel::_initForNewVehicle: vehicle is NULL";
        return;
    }

    connect(vehicle, &Vehicle::vehicleTypeChanged, this, &FactValueGridModel::_resetFromSettings);
    _resetFromSettings();
}

void FactValueGridModel::_deinitVehicle(Vehicle* vehicle)
{
    disconnect(vehicle, &Vehicle::vehicleTypeChanged, this, &FactValueGridModel::_resetFromSettings);
}

void FactValueGridModel::_activeVehicleChanged(Vehicle* activeVehicle)
{
    if (!_activeVehicle) {
        qCritical() << "FactValueGridModel::_activeVehicleChanged: _activeVehicle is NULL";
    }

    if (_activeVehicle) {
        _deinitVehicle(_activeVehicle);
        _activeVehicle = nullptr;
    }

    if (!activeVehicle) {
        activeVehicle = MultiVehicleManager::instance()->offlineEditingVehicle();
    }
    _activeVehicle = activeVehicle;
    _initForNewVehicle(activeVehicle);
}

FactValueGridModel::~FactValueGridModel()
{
    _vehicleCardInstanceList.removeAll(this);
}

QGCMAVLinkTypes::VehicleClass_t FactValueGridModel::vehicleClass(void) const
{
    return QGCMAVLink::vehicleClass(currentVehicle()->vehicleType());
}

void FactValueGridModel::resetToDefaults(void)
{
    QSettings settings;
    settings.remove(_settingsGroup);
    _resetFromSettings();
}

QString FactValueGridModel::_pascalCase(const QString& text)
{
    return text[0].toUpper() + text.right(text.length() - 1);
}

void FactValueGridModel::setFontSize(FontSize fontSize)
{
    if (fontSize != _fontSize) {
        _fontSize = fontSize;
        emit fontSizeChanged(fontSize);
    }
}

void FactValueGridModel::_saveValueData(QSettings& settings, InstrumentValueData* value)
{
    settings.setValue(_textKey,         value->text());
    settings.setValue(_showUnitsKey,    value->showUnits());
    settings.setValue(_iconKey,         value->icon());
    settings.setValue(_rangeTypeKey,    value->rangeType());

    if (value->rangeType() != InstrumentValueData::NoRangeInfo) {
        settings.setValue(_rangeValuesKey, value->rangeValues());
    }

    switch (value->rangeType()) {
    case InstrumentValueData::NoRangeInfo:
        break;
    case InstrumentValueData::ColorRange:
        settings.setValue(_rangeColorsKey,      value->rangeColors());
        break;
    case InstrumentValueData::OpacityRange:
        settings.setValue(_rangeOpacitiesKey,   value->rangeOpacities());
        break;
    case InstrumentValueData::IconSelectRange:
        settings.setValue(_rangeIconsKey,       value->rangeIcons());
        break;
    }

    settings.setValue(_factGroupNameKey,    value->factGroupName());
    settings.setValue(_factNameKey,         value->factName());
}

void FactValueGridModel::_loadValueData(QSettings& settings, InstrumentValueData* value)
{
    QString factName = settings.value(_factNameKey).toString();
    if (!factName.isEmpty()) {
        value->setFact(settings.value(_factGroupNameKey).toString(), factName);
    }

    value->setText      (settings.value(_textKey).toString());
    value->setShowUnits (settings.value(_showUnitsKey, true).toBool());
    value->setIcon      (settings.value(_iconKey).toString());
    value->setRangeType (settings.value(_rangeTypeKey, InstrumentValueData::NoRangeInfo).value<InstrumentValueData::RangeType>());

    if (value->rangeType() != InstrumentValueData::NoRangeInfo) {
        value->setRangeValues(settings.value(_rangeValuesKey).value<QVariantList>());
    }
    switch (value->rangeType()) {
    case InstrumentValueData::NoRangeInfo:
        break;
    case InstrumentValueData::ColorRange:
        value->setRangeColors(settings.value(_rangeColorsKey).value<QVariantList>());
        break;
    case InstrumentValueData::OpacityRange:
        value->setRangeOpacities(settings.value(_rangeOpacitiesKey).value<QVariantList>());
        break;
    case InstrumentValueData::IconSelectRange:
        value->setRangeIcons(settings.value(_rangeIconsKey).value<QVariantList>());
        break;
    }
}

void FactValueGridModel::_connectSaveSignals(InstrumentValueData* value)
{
    connect(value, &InstrumentValueData::factNameChanged,       this, &FactValueGridModel::_saveSettings);
    connect(value, &InstrumentValueData::factGroupNameChanged,  this, &FactValueGridModel::_saveSettings);
    connect(value, &InstrumentValueData::textChanged,           this, &FactValueGridModel::_saveSettings);
    connect(value, &InstrumentValueData::showUnitsChanged,      this, &FactValueGridModel::_saveSettings);
    connect(value, &InstrumentValueData::iconChanged,           this, &FactValueGridModel::_saveSettings);
    connect(value, &InstrumentValueData::rangeTypeChanged,      this, &FactValueGridModel::_saveSettings);
    connect(value, &InstrumentValueData::rangeValuesChanged,    this, &FactValueGridModel::_saveSettings);
    connect(value, &InstrumentValueData::rangeColorsChanged,    this, &FactValueGridModel::_saveSettings);
    connect(value, &InstrumentValueData::rangeOpacitiesChanged, this, &FactValueGridModel::_saveSettings);
    connect(value, &InstrumentValueData::rangeIconsChanged,     this, &FactValueGridModel::_saveSettings);
}

void FactValueGridModel::appendRow(void)
{
    for (int colIndex=0; colIndex<_columns->count(); colIndex++) {
        QmlObjectListModel* list = _columns->value<QmlObjectListModel*>(colIndex);
        list->append(_createNewInstrumentValueWorker(list));

    }
    _rowCount++;
    emit rowCountChanged(_rowCount);
    _saveSettings();
}

void FactValueGridModel::deleteLastRow(void)
{
    if (_rowCount <= 1) {
        return;
    }
    for (int colIndex=0; colIndex<_columns->count(); colIndex++) {
        QmlObjectListModel* list = _columns->value<QmlObjectListModel*>(colIndex);
        list->removeAt(list->count() - 1)->deleteLater();
    }
    _rowCount--;
    emit rowCountChanged(_rowCount);
    _saveSettings();
}

QmlObjectListModel* FactValueGridModel::appendColumn(void)
{
    QmlObjectListModel* newList = new QmlObjectListModel(_columns);
    _columns->append(newList);

    // If this is the first row then we automatically add the first column as well
    int cRowsToAdd = qMax(_rowCount, 1);
    for (int i=0; i<cRowsToAdd; i++) {
        newList->append(_createNewInstrumentValueWorker(newList));
    }

    if (cRowsToAdd != _rowCount) {
        _rowCount = cRowsToAdd;
        emit rowCountChanged(_rowCount);
    }

    _saveSettings();

    return newList;
}

void FactValueGridModel::deleteLastColumn(void)
{
    if (_columns->count() > 1) {
        _columns->removeAt(_columns->count() - 1)->deleteLater();
        _saveSettings();
    }
}

InstrumentValueData* FactValueGridModel::_createNewInstrumentValueWorker(QObject* parent)
{
    InstrumentValueData* value = new InstrumentValueData(this, parent);
    value->setFact(InstrumentValueData::vehicleFactGroupName, "AltitudeRelative");
    value->setText(value->fact()->shortDescription());
    _connectSaveSignals(value);
    return value;

}

void FactValueGridModel::_saveSettings(void)
{
    if (_preventSaveSettings) {
        return;
    }

    QSettings   settings;
    QString     groupNameFormat("%1-%2");

    settings.beginGroup(_settingsKey());
    settings.remove(""); // Remove any previous settings

    settings.setValue(_versionKey,  1);
    settings.setValue(_fontSizeKey, _fontSize);
    settings.setValue(_rowCountKey, _rowCount);

    settings.beginWriteArray(_columnsKey);
    for (int colIndex=0; colIndex<_columns->count(); colIndex++) {
        QmlObjectListModel* columns = _columns->value<QmlObjectListModel*>(colIndex);

        settings.setArrayIndex(colIndex);
        settings.beginWriteArray(_rowsKey);

        for (int rowIndex=0; rowIndex<columns->count(); rowIndex++) {
            InstrumentValueData* value = columns->value<InstrumentValueData*>(rowIndex);
            settings.setArrayIndex(rowIndex);
            _saveValueData(settings, value);
        }

        settings.endArray();
    }
    settings.endArray();

    // If this settings change was set from a Vehicle card, this makes so the changes are
    // immediately applied to the other Vehicle cards.
    if (_specificVehicleForCard) {
        for (FactValueGridModel* obj : _vehicleCardInstanceList) {
            if (obj != this) {
                obj->_resetFromSettings();
            }
        }
    }
}

QString FactValueGridModel::_settingsKey(void)
{
    return QStringLiteral("%1-%2").arg(_settingsGroup).arg(vehicleClass());
}

void FactValueGridModel::_resetFromSettings(void)
{
    _preventSaveSettings = true;

    _columns->deleteLater();

    _columns    = new QmlObjectListModel(this);
    _rowCount   = 0;

    QSettings   settings;
    QString     groupNameFormat("%1-%2");

    if (settings.childGroups().contains(_settingsKey())) {
        // Load from settings
        settings.beginGroup(_settingsKey());

        int version = settings.value(_versionKey, 0).toInt();
        if (version != 1) {
            QGC::showAppMessage(tr("Settings version %1 for %2 is not supported. Setup will be reset to defaults.").arg(version).arg(_settingsGroup), tr("Load Settings"));
            settings.remove("");
            QGCCorePlugin::instance()->factValueGridCreateDefaultSettings(this);
        }
        _fontSize = settings.value(_fontSizeKey, DefaultFontSize).value<FontSize>();

        // Initial setup of empty items
        int cRows       = settings.value(_rowCountKey).toInt();
        int cModelLists = settings.beginReadArray(_columnsKey);
        if (cModelLists && cRows) {
            appendColumn();
            for (int rowIndex=1; rowIndex<cRows; rowIndex++) {
                appendRow();
            }
            for (int colIndex=1; colIndex<cModelLists; colIndex++) {
                appendColumn();
            }
        }

        // Fill in the items from settings
        for (int colIndex=0; colIndex<cModelLists; colIndex++) {
            settings.setArrayIndex(colIndex);
            int cItems = settings.beginReadArray(_rowsKey);
            for (int itemIndex=0; itemIndex<cItems; itemIndex++) {
                QmlObjectListModel* list = _columns->value<QmlObjectListModel*>(colIndex);
                InstrumentValueData* value = list->value<InstrumentValueData*>(itemIndex);
                settings.setArrayIndex(itemIndex);
                _loadValueData(settings, value);
            }
            settings.endArray();
        }
        settings.endArray();
    } else {
        // Default settings are added directly to this FactValueGridModel
        QGCCorePlugin::instance()->factValueGridCreateDefaultSettings(this);
    }

    emit columnsChanged(_columns);

    _preventSaveSettings = false;
}
