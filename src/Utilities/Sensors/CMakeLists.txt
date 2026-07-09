# Wave-12: local GCS-host sensor readings, opt-out via QGC_ENABLE_SENSORS
# (default ON — see cmake/CustomOptions.cmake). Its two callers
# (Vehicle::Vehicle, APMFirmwarePlugin::startCompensatingBaro) guard their
# QGCSensors.h include and call sites on the QGC_ENABLE_SENSORS compile
# definition (set globally in root CMakeLists.txt), so no target exists here
# at all when the option is OFF.
if(QGC_ENABLE_SENSORS)
    qt_add_library(QGCSensors STATIC
        QGCSensors.cc
        QGCSensors.h
    )

    target_link_libraries(QGCSensors
        PUBLIC
            Qt6::Core
            Qt6::Positioning
            Qt6::Sensors
        PRIVATE
            QGCLogging
    )

    target_include_directories(QGCSensors
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE QGCSensors)

    target_precompile_headers(QGCSensors PRIVATE
        <QtCore/QObject>
        <QtCore/QString>
        <QtPositioning/QGeoPositionInfo>
        <QtSensors/QAmbientTemperatureSensor>
        <QtSensors/QCompass>
        <QtSensors/QPressureSensor>
    )
endif()
