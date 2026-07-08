#pragma once

#include <QtCore/QSettings>
#ifdef QGC_ENABLE_QML
#include <QtQuick/QQuickItem>
#else
#include <QtCore/QObject>
#endif
#include <QtQmlIntegration/QtQmlIntegration>

#include "QGCMAVLinkTypes.h"

class InstrumentValueData;
class QmlObjectListModel;
class Vehicle;

// QObject does not support a class inheriting from two independent QObject-derived
// branches, so FactValueGrid (the QQuickItem registered with QML, see FactValueGrid.h)
// cannot both derive from QQuickItem and from a QObject-based model class. Instead this
// class itself becomes the QQuickItem in QML-ON builds (matching the pre-split
// FactValueGrid exactly) and a plain QObject in QML-OFF builds, where headless callers
// (SubtitleWriter, QGCCorePlugin::factValueGridCreateDefaultSettings) use it directly
// without pulling in Qt6::Quick.
#ifdef QGC_ENABLE_QML
using FactValueGridModelBase = QQuickItem;
#else
using FactValueGridModelBase = QObject;
#endif

/// Quick-free core of the fact value grid: rows/columns of Fact-bound cells, settings
/// persistence, and active-vehicle tracking. This holds all the logic that used to live
/// directly in FactValueGrid. In QML-ON builds, FactValueGrid (a thin QQuickItem
/// subclass, see FactValueGrid.h) inherits from this class to preserve the "FactValueGrid"
/// QML type registration used by HorizontalFactValueGridTemplate/HorizontalFactValueGrid.qml.
class FactValueGridModel : public FactValueGridModelBase
{
    Q_OBJECT
    Q_MOC_INCLUDE("QmlObjectListModel.h")
public:
    explicit FactValueGridModel(FactValueGridModelBase *parent = nullptr);
    ~FactValueGridModel();

    enum FontSize {
        DefaultFontSize=0,
        SmallFontSize,
        MediumFontSize,
        LargeFontSize,
    };
    Q_ENUM(FontSize)

    Q_PROPERTY(QmlObjectListModel*  columns         MEMBER _columns                                     NOTIFY columnsChanged)
    Q_PROPERTY(int                  rowCount        MEMBER _rowCount                                    NOTIFY rowCountChanged)
    Q_PROPERTY(QStringList          iconNames       READ iconNames                                      CONSTANT)
    Q_PROPERTY(FontSize             fontSize        READ fontSize           WRITE setFontSize           NOTIFY fontSizeChanged)
    Q_PROPERTY(QStringList          fontSizeNames   MEMBER _fontSizeNames                               CONSTANT)

    // The following properties should only be set at initial object creation time
    Q_PROPERTY(QString              settingsGroup           MEMBER _settingsGroup           NOTIFY settingsGroupChanged             REQUIRED)
    Q_PROPERTY(Vehicle *            specificVehicleForCard  MEMBER _specificVehicleForCard  NOTIFY specificVehicleForCardChanged    REQUIRED)   ///< null means track active vehicle, set to specific vehicle to track a single vehicle and share settings with other cards

    Q_INVOKABLE void                resetToDefaults (void);
    Q_INVOKABLE QmlObjectListModel* appendColumn    (void);
    Q_INVOKABLE void                deleteLastColumn(void);
    Q_INVOKABLE void                appendRow       (void);
    Q_INVOKABLE void                deleteLastRow   (void);

    QmlObjectListModel*         columns                 (void) const { return _columns; }
    QString                     settingsGroup           (void) const { return _settingsGroup; }
    FontSize                    fontSize                (void) const { return _fontSize; }
    QStringList                 iconNames               (void) const { return _iconNames; }
    QGCMAVLinkTypes::VehicleClass_t  vehicleClass            (void) const;
    Vehicle*                    currentVehicle          (void) const { return _specificVehicleForCard ? _specificVehicleForCard : _activeVehicle; }
    Vehicle*                    specificVehicleForCard  (void) const { return _specificVehicleForCard; }

    void setFontSize(FontSize fontSize);

    // Called once the settingsGroup/specificVehicleForCard properties are set. In QML-ON
    // builds this overrides QQmlParserStatus::componentComplete (via QQuickItem) so the
    // QML engine calls it automatically once property bindings are applied. Headless
    // callers must call it explicitly after setting settingsGroup.
#ifdef QGC_ENABLE_QML
    void componentComplete(void) final;
#else
    void componentComplete(void);
#endif

    // These settings group names are shared with HorizontalFactValueGrid (the QML-only
    // horizontal layout view, see HorizontalFactValueGrid.h) but live here so headless
    // callers (SubtitleWriter) can reference them without pulling in the QQuickItem-based
    // view class.
    static const QString telemetryBarSettingsGroup;    ///< for activeVehicle telem table
    static const QString vehicleCardSettingsGroup;      ///< for multi-vehicle list telem tables

signals:
    void fontSizeChanged(FontSize fontSize);
    void columnsChanged (QmlObjectListModel* model);
    void rowCountChanged(int rowCount);
    void settingsGroupChanged(QString settingsGroup);
    void specificVehicleForCardChanged(Vehicle* vehicle);

protected:
    Q_DISABLE_COPY(FactValueGridModel)

    QString                     _settingsGroup;
    FontSize                    _fontSize               = DefaultFontSize;
    bool                        _preventSaveSettings    = false;
    QmlObjectListModel*         _columns                = nullptr;
    int                         _rowCount               = 0;
    Vehicle*                    _specificVehicleForCard = nullptr;
    Vehicle*                    _activeVehicle          = nullptr;

private slots:
    void _activeVehicleChanged(Vehicle *activeVehicle);
    void _resetFromSettings(void);

private:
    InstrumentValueData*    _createNewInstrumentValueWorker (QObject* parent);
    void                    _saveSettings                   (void);
    void                    _connectSaveSignals             (InstrumentValueData* value);
    QString                 _pascalCase                     (const QString& text);
    void                    _saveValueData                  (QSettings& settings, InstrumentValueData* value);
    void                    _loadValueData                  (QSettings& settings, InstrumentValueData* value);
    QString                 _settingsKey                    (void);
    void                    _initForNewVehicle              (Vehicle* vehicle);
    void                    _deinitVehicle                  (Vehicle* vehicle);

    // These are user facing string for the various enums.
    static       QStringList _iconNames;
    QStringList _fontSizeNames;

    static constexpr const char* _columnsKey          = "columns";
    static constexpr const char* _rowsKey             = "rows";
    static constexpr const char* _rowCountKey         = "rowCount";
    static constexpr const char* _fontSizeKey         = "fontSize";
    static constexpr const char* _versionKey          = "version";
    static constexpr const char* _factGroupNameKey    = "factGroupName";
    static constexpr const char* _factNameKey         = "factName";
    static constexpr const char* _textKey             = "text";
    static constexpr const char* _showUnitsKey        = "showUnits";
    static constexpr const char* _iconKey             = "icon";
    static constexpr const char* _rangeTypeKey        = "rangeType";
    static constexpr const char* _rangeValuesKey      = "rangeValues";
    static constexpr const char* _rangeColorsKey      = "rangeColors";
    static constexpr const char* _rangeIconsKey       = "rangeIcons";
    static constexpr const char* _rangeOpacitiesKey   = "rangeOpacities";

    static constexpr const char* _deprecatedGroupKey =  "ValuesWidget";

    static QList<FactValueGridModel*> _vehicleCardInstanceList;
};
