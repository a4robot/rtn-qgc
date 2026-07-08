qt_add_library(QGCLogManager STATIC
    LogEntry.cc
    LogEntry.h
    LogEntryTableModel.cc
    LogEntryTableModel.h
    LogFormatter.cc
    LogFormatter.h
    LogModel.cc
    LogModel.h
    LogManager.cc
    LogManager.h
)

# LoggingCategoriesDialog.qml is the only UI surface here; LogEntry/LogModel/
# LogFormatter/LogManager itself are core (main.cc, QGCApplication.cc call
# LogManager::instance() unconditionally). Skip module/plugin generation
# when headless so no Qt6::Qml/Quick runtime dependency is pulled in.
if(QGC_ENABLE_QML)
    qt_add_qml_module(QGCLogManager
        URI QGroundControl.LogManager
        VERSION 1.0
        RESOURCE_PREFIX /qml
        DEPENDENCIES
            QGroundControl.Controls
            QGroundControl.Logging
        QML_FILES
            LoggingCategoriesDialog.qml
    )
endif()

target_include_directories(QGCLogManager
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/Settings
        ${CMAKE_SOURCE_DIR}/src/FactSystem
        ${CMAKE_SOURCE_DIR}/src/Utilities
)

target_link_libraries(QGCLogManager
    PUBLIC
        Qt6::Core
        $<$<BOOL:${QGC_ENABLE_QML}>:Qt6::Qml>
        Qt6::QmlIntegration
    PRIVATE
        Qt6::Concurrent
        $<$<BOOL:${QGC_ENABLE_QML}>:Qt6::Quick>
        QGCFileSystem
        QGCLogging
)

target_precompile_headers(QGCLogManager PRIVATE
    <QtCore/QObject>
    <QtCore/QString>
    <QtCore/QDateTime>
    <QtCore/QAbstractTableModel>
    <QtQmlIntegration/QtQmlIntegration>
)

# AppLogging.qml is loaded via dynamic URL (SettingsPages.json), so the QML
# import scanner can't see QGroundControl.LogManager. Force-link the plugin
# so its type registrations end up in the executable (QML builds only — no
# plugin target exists when QGC_ENABLE_QML is OFF).
target_link_libraries(${CMAKE_PROJECT_NAME}
    PRIVATE
        QGCLogManager
        $<$<BOOL:${QGC_ENABLE_QML}>:QGCLogManagerplugin>
)
