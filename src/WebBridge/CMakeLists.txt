# ============================================================================
# WebBridge Module
# Pure C++ (Qt Core only) bridge logic ported from FlyView QML for non-QML
# frontends. No QtQml/QtQuick dependencies in this module's own sources.
# ============================================================================

qt_add_library(WebBridgeModule STATIC
    GuidedActionGate.cc
    GuidedActionGate.h
    WebBridge.cc
    WebBridge.h
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

# Headers are included bare (e.g. "Vehicle.h") to match repo convention, and
# Vehicle.h pulls in most of the app's subsystem headers transitively (Comms,
# FactSystem, FirmwarePlugin, MAVLink incl. the CPM-fetched C headers and the
# generated MAVLinkEnums.h in CMAKE_BINARY_DIR, ...). Those dirs are only
# attached to ${CMAKE_PROJECT_NAME}, so mirror its include set wholesale
# instead of chasing the transitive closure by hand.
target_include_directories(WebBridgeModule
    PRIVATE
        $<TARGET_PROPERTY:${CMAKE_PROJECT_NAME},INCLUDE_DIRECTORIES>
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(TARGET mavlink)
    # Ensure the CPM fetch and the generated headers (MAVLinkEnums.h,
    # MAVLinkInstanceFields.h) exist before this module compiles or mocs.
    add_dependencies(WebBridgeModule mavlink mavlink_enums mavlink_instance_fields)
    set_property(
        TARGET WebBridgeModule
        APPEND
        PROPERTY AUTOGEN_TARGET_DEPENDS mavlink_enums
    )
endif()
