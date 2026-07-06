# ============================================================================
# WebBridge Module
# Pure C++ (Qt Core only) bridge logic ported from FlyView QML for non-QML
# frontends. No QtQml/QtQuick dependencies in this module's own sources.
#
# NOTE: This directory is not yet wired into src/CMakeLists.txt; the
# orchestrator adds `add_subdirectory(WebBridge)` and links WebBridgeModule
# into ${CMAKE_PROJECT_NAME}.
# ============================================================================

qt_add_library(WebBridgeModule STATIC
    GuidedActionGate.cc
    GuidedActionGate.h
)

target_link_libraries(WebBridgeModule
    PRIVATE
        # Vehicle.h transitively needs QGeoCoordinate and QtQmlIntegration
        # (header-only QML_ELEMENT macros); neither adds a QtQml runtime
        # dependency to this module's own code.
        Qt6::Positioning
        Qt6::QmlIntegration
    PUBLIC
        Qt6::Core
)

# Headers are included bare (e.g. "Vehicle.h") to match repo convention; the
# owning modules only add their include dirs to ${CMAKE_PROJECT_NAME}, so list
# the ones this module compiles against explicitly.
target_include_directories(WebBridgeModule
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/API                     # QGCCorePlugin.h, QGCOptions.h
        ${CMAKE_SOURCE_DIR}/src/Comms                   # transitively required by Vehicle.h
        ${CMAKE_SOURCE_DIR}/src/FactSystem              # Fact.h
        ${CMAKE_SOURCE_DIR}/src/FirmwarePlugin          # transitively required by Vehicle.h
        ${CMAKE_SOURCE_DIR}/src/MAVLink                 # MAVLinkLib.h, QGCMAVLink.h
        ${CMAKE_SOURCE_DIR}/src/MAVLink/LibEvents       # HealthAndArmingCheckReport.h
        ${CMAKE_SOURCE_DIR}/src/QmlControls             # QmlObjectListModel.h (via HealthAndArmingCheckReport.h)
        ${CMAKE_SOURCE_DIR}/src/Settings                # SettingsManager.h, AppSettings.h
        ${CMAKE_SOURCE_DIR}/src/Utilities/Logging       # QGCLoggingCategory.h
        ${CMAKE_SOURCE_DIR}/src/Vehicle                 # Vehicle.h, VehicleSupports.h
        ${CMAKE_SOURCE_DIR}/src/Vehicle/FactGroups      # VehicleFactGroup.h
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# The mavlink C headers are fetched by src/MAVLink/CMakeLists.txt (CPM) and
# their include dirs are only attached to ${CMAKE_PROJECT_NAME}; the
# orchestrator must forward them (mavlink_BINARY_DIR include paths) when
# wiring this module in.
