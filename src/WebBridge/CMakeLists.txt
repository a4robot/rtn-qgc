# ============================================================================
# WebBridge Module
# Pure C++ (Qt Core only) bridge logic ported from FlyView QML for non-QML
# frontends. No QtQml/QtQuick dependencies in this module's own sources.
# ============================================================================

qt_add_library(WebBridgeModule STATIC
    CommandChannel.cc
    CommandChannel.h
    FactChannel.cc
    FactChannel.h
    GhostVideoSource.cc
    GhostVideoSource.h
    GuidedActionGate.cc
    GuidedActionGate.h
    ImageChannel.cc
    ImageChannel.h
    MissionChannel.cc
    MissionChannel.h
    NotificationChannel.cc
    NotificationChannel.h
    SettingsChannel.cc
    SettingsChannel.h
    TelemetryChannel.cc
    TelemetryChannel.h
    WebBridge.cc
    WebBridge.h
    VideoStreamServer.cc
    VideoStreamServer.h
    WebBridgeServer.cc
    WebBridgeServer.h
    WsTransport.h
)

# ============================================================================
# WS Transport (Q7e, STRANGLER_MILESTONES.md M7): WebBridgeServer's protocol state machine is
# transport-independent (WsTransport.h); exactly one concrete implementation is compiled in,
# selected by QGC_ENABLE_QT_WEBSOCKETS (cmake/CustomOptions.cmake):
#   ON  (default, matches upstream)   -> QtWsTransport.{h,cc}, thin QWebSocketServer wrapper.
#   OFF (ghost-diet build)            -> IxWsTransport.{h,cc}, CPM-vendored IXWebSocket -- see
#                                        IxWsTransport.cc's header comment for the library-choice
#                                        rationale (uWebSockets/libwebsockets/Boost.Beast were
#                                        also evaluated).
# ============================================================================
if(QGC_ENABLE_QT_WEBSOCKETS)
    target_sources(WebBridgeModule PRIVATE
        QtWsTransport.cc
        QtWsTransport.h
    )
    # WebBridgeServer.cc #ifdefs on this to pick QtWsTransport vs IxWsTransport -- keep the
    # macro name identical to the CMake option so there is only one name to track.
    target_compile_definitions(WebBridgeModule PUBLIC QGC_ENABLE_QT_WEBSOCKETS)
    target_link_libraries(WebBridgeModule PUBLIC Qt6::WebSockets)
else()
    # IXWebSocket needs zlib (permessage-deflate support machinery, even though it's disabled at
    # runtime by IxWsTransport -- USE_ZLIB just controls whether the library is *able* to, and
    # find_package(ZLIB) below resolves to the same CPM-vendored zlib QGCCompression already
    # fetches (src/Utilities/Compression/CMakeLists.txt's ZLIB::ZLIB alias), added to the build
    # earlier (Utilities/ is add_subdirectory()'d before WebBridge/ in src/CMakeLists.txt) --
    # no second zlib copy gets built. TLS is left off entirely (USE_TLS OFF): this is a
    # localhost-only bridge per PROTOCOL.md §1's transport table, so no OpenSSL/mbedTLS
    # dependency is pulled in.
    CPMAddPackage(
        NAME ixwebsocket
        VERSION 12.0.1
        GITHUB_REPOSITORY machinezone/IXWebSocket
        GIT_TAG v12.0.1
        OPTIONS
            "BUILD_DEMO OFF"
            "USE_TLS OFF"
            "USE_ZLIB ON"
            "IXWEBSOCKET_INSTALL OFF"
    )
    if(ixwebsocket_ADDED)
        qgc_disable_dependency_warnings(ixwebsocket)
    endif()

    target_sources(WebBridgeModule PRIVATE
        IxWsTransport.cc
        IxWsTransport.h
    )
    target_link_libraries(WebBridgeModule PRIVATE ixwebsocket::ixwebsocket)
endif()

# VideoStreamServer taps the GStreamer pipeline; without GStreamer it compiles
# to inert stubs. GStreamer::GStreamer is created by the VideoManager subtree
# (processed before this directory) when the option is ON.
if(QGC_ENABLE_GST_VIDEOSTREAMING AND TARGET GStreamer::GStreamer)
    target_compile_definitions(WebBridgeModule PRIVATE QGC_GST_STREAMING)
    target_link_libraries(WebBridgeModule PRIVATE GStreamer::GStreamer)
endif()

target_link_libraries(WebBridgeModule
    PRIVATE
        # Vehicle.h transitively needs QGeoCoordinate and QtQmlIntegration
        # (header-only QML_ELEMENT macros); neither adds a QtQml runtime
        # dependency to this module's own code.
        Qt6::Positioning
        Qt6::QmlIntegration
    PUBLIC
        Qt6::Core
        # Qt6::WebSockets (QGC_ENABLE_QT_WEBSOCKETS=ON) or ixwebsocket::ixwebsocket (OFF) is
        # linked above, per the WS Transport block -- WebBridgeServer.h no longer exposes either
        # library's socket type (see WsTransport.h), so nothing here needs it PUBLIC.
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
