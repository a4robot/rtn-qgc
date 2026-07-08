# ============================================================================
# Qt Location Plugin Module
# Custom map tile providers and caching engine
#
# Split into two targets:
#  - QGCLocationCore: Location-free static lib (providers, URL/request
#    building, tile-cache engine/worker/database). Always built; this is what
#    the headless terrain path (src/Terrain/TerrainTileFetcher) uses.
#  - QGCLocation: the QGeoServiceProvider tile-map plugin for the QML map.
#    Only built with QGC_ENABLE_QML; the only part that links Qt6::Location.
# ============================================================================

qt_add_library(QGCLocationCore STATIC
    Providers/BingMapProvider.cpp
    Providers/BingMapProvider.h
    Providers/ElevationMapProvider.cpp
    Providers/ElevationMapProvider.h
    Providers/EsriMapProvider.cpp
    Providers/EsriMapProvider.h
    Providers/GenericMapProvider.cpp
    Providers/GenericMapProvider.h
    Providers/GoogleMapProvider.cpp
    Providers/GoogleMapProvider.h
    Providers/MapboxMapProvider.cpp
    Providers/MapboxMapProvider.h
    Providers/MapProvider.cpp
    Providers/MapProvider.h
    Providers/TianDiTuProvider.cpp
    Providers/TianDiTuProvider.h
    QGCCachedTileSet.cpp
    QGCCachedTileSet.h
    QGCCacheTile.h
    QGCMapEngine.cpp
    QGCMapEngine.h
    QGCMapEngineManager.cc
    QGCMapEngineManager.h
    QGCMapTaskBase.h
    QGCMapTasks.cpp
    QGCMapTasks.h
    QGCMapUrlEngine.cpp
    QGCMapUrlEngine.h
    QGCTile.h
    QGCTileCacheDatabase.cpp
    QGCTileCacheDatabase.h
    QGCTileCacheFetcher.cpp
    QGCTileCacheFetcher.h
    QGCTileCacheTypes.h
    QGCTileCacheWorker.cpp
    QGCTileCacheWorker.h
    QGCTileSet.h
)

if(IOS)
    target_compile_definitions(QGCLocationCore PRIVATE QGC_NO_GOOGLE_MAPS)
endif()

target_link_libraries(QGCLocationCore
    PRIVATE
        Qt6::Positioning
        QGCCompression
        QGCDatabase
        QGCFileSystem
        QGCFormat
        QGCLogging
        QGCNetwork
        $<$<BOOL:${QGC_ENABLE_QML}>:Qt6::Qml>
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::QmlIntegration
)

target_include_directories(QGCLocationCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Providers
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../FactSystem
        ${CMAKE_CURRENT_SOURCE_DIR}/../QmlControls
        ${CMAKE_CURRENT_SOURCE_DIR}/../Terrain
        ${CMAKE_CURRENT_SOURCE_DIR}/../Terrain/Providers
        ${CMAKE_CURRENT_SOURCE_DIR}/../Settings
        ${CMAKE_CURRENT_SOURCE_DIR}/../Utilities
)

target_precompile_headers(QGCLocationCore PRIVATE
    <QtCore/QDir>
    <QtCore/QLoggingCategory>
    <QtCore/QObject>
    <QtCore/QString>
    <QtCore/QStringList>
    <QtCore/QVariant>
    <QtNetwork/QNetworkRequest>
    <QtPositioning/QGeoCoordinate>
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE QGCLocationCore)

if(QGC_ENABLE_QML)
    qt_add_plugin(QGCLocation
        STATIC
        CLASS_NAME QGeoServiceProviderFactoryQGC
        OUTPUT_TARGETS QGCLocation_targets
        QGeoFileTileCacheQGC.cpp
        QGeoFileTileCacheQGC.h
        QGeoMapReplyQGC.cpp
        QGeoMapReplyQGC.h
        QGeoServiceProviderPluginQGC.cpp
        QGeoServiceProviderPluginQGC.h
        QGeoTiledMappingManagerEngineQGC.cpp
        QGeoTiledMappingManagerEngineQGC.h
        QGeoTiledMapQGC.cpp
        QGeoTiledMapQGC.h
        QGeoTileFetcherQGC.cpp
        QGeoTileFetcherQGC.h
    )

    target_link_libraries(QGCLocation
        PRIVATE
            Qt6::Positioning
            QGCFileSystem
            QGCLogging
            QGCNetwork
        PUBLIC
            Qt6::Core
            Qt6::Location
            Qt6::LocationPrivate
            Qt6::Network
            QGCLocationCore
    )

    target_include_directories(QGCLocation
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/..
            ${CMAKE_CURRENT_SOURCE_DIR}/../FactSystem
            ${CMAKE_CURRENT_SOURCE_DIR}/../QmlControls
            ${CMAKE_CURRENT_SOURCE_DIR}/../Terrain
            ${CMAKE_CURRENT_SOURCE_DIR}/../Terrain/Providers
            ${CMAKE_CURRENT_SOURCE_DIR}/../Settings
            ${CMAKE_CURRENT_SOURCE_DIR}/../Utilities
    )

    target_precompile_headers(QGCLocation PRIVATE
        <QtCore/QDir>
        <QtCore/QLoggingCategory>
        <QtCore/QObject>
        <QtCore/QString>
        <QtCore/QStringList>
        <QtCore/QVariant>
        <QtNetwork/QNetworkRequest>
        <QtPositioning/QGeoCoordinate>
        <QtQml/QQmlEngine>
    )

    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE QGCLocation)

    qt_add_resources(${CMAKE_PROJECT_NAME} qtlocationplugin_resources
        PREFIX "/res"
        BASE "${CMAKE_SOURCE_DIR}/resources"
        FILES
            "${CMAKE_SOURCE_DIR}/resources/notile.png"
    )
endif()
