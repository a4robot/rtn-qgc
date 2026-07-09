qt_add_library(QGCGeoFormats STATIC
    GeoJsonHelper.cc
    GeoJsonHelper.h
    QGCGeoJsonImport.cc
    QGCGeoJsonImport.h
    ShapeFileHelper.cc
    ShapeFileHelper.h
    SHPFileHelper.cc
    SHPFileHelper.h
)

# No QML_FILES here — this only registers ShapeFileHelper as a
# QML_ELEMENT/QML_SINGLETON for QML callers. Skip module/plugin generation
# entirely so no Qt6::Qml runtime dependency is pulled in.
if(QGC_ENABLE_QML)
    qt_add_qml_module(QGCGeoFormats
        URI QGroundControl.Geo
        VERSION 1.0
        RESOURCE_PREFIX /qml
    )
endif()

# KML plan-file import/export (KMLDomDocument/KMLHelper/KMLSchemaValidator) is
# reachable only from QML UI actions (PlanView.qml Save-to-KML, the KML/SHP
# load dialog on QGCMapPolygon/QGCMapPolyline) — verified by call-graph audit,
# wave 14 Q7c. Not part of the headless ghost's surface, so compile it out of
# QGC_ENABLE_QML=OFF builds; ShapeFileHelper.cc guards its callers with the
# same #ifdef. This does not drop Qt6::Xml this wave (VehicleCameraControl.cc
# still needs it) — see Q8c for that.
if(QGC_ENABLE_QML)
    target_sources(QGCGeoFormats PRIVATE
        KMLDomDocument.cc
        KMLDomDocument.h
        KMLHelper.cc
        KMLHelper.h
        KMLSchemaValidator.cc
        KMLSchemaValidator.h
    )

    qt_add_resources(QGCGeoFormats "kml_schema"
        PREFIX "/kml"
        FILES
            ogckml22.xsd
    )
endif()

CPMAddPackage(
    NAME Shapelib
    VERSION 1.6.3
    GITHUB_REPOSITORY OSGeo/shapelib
    GIT_TAG v1.6.3
    OPTIONS
        "BUILD_SHAPELIB_CONTRIB OFF"
        "BUILD_APPS OFF"
        "BUILD_TESTING OFF"
)

if(Shapelib_ADDED)
    qgc_disable_dependency_warnings(shp)
endif()

target_link_libraries(QGCGeoFormats
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Positioning
        $<$<BOOL:${QGC_ENABLE_QML}>:Qt6::Qml>
        Qt6::QmlIntegration
        Qt6::Xml
    PRIVATE
        shp
        QGCGeoMath
        QGCLogging
        QGCParsing             # JsonParsing::possibleNaNJsonValue / isJsonFile
)

target_include_directories(QGCGeoFormats
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_precompile_headers(QGCGeoFormats PRIVATE
    <QtCore/QObject>
    <QtCore/QString>
    <QtCore/QJsonObject>
    <QtPositioning/QGeoCoordinate>
    <QtQmlIntegration/QtQmlIntegration>
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE QGCGeoFormats)
