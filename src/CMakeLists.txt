# ============================================================================
# QGroundControl Module Library
# ============================================================================

qt_add_library(QGroundControlModule STATIC)

# ----------------------------------------------------------------------------
# QML Resources
# ----------------------------------------------------------------------------
if(QGC_ENABLE_QML)
    set_source_files_properties(MainWindow/MainWindow.qml
        PROPERTIES
            QT_RESOURCE_ALIAS MainWindow.qml
    )

    qt_add_qml_module(QGroundControlModule
        URI QGroundControl
        VERSION 1.0
        RESOURCE_PREFIX /qml
        QML_FILES
            MainWindow/MainWindow.qml
            MainWindow/MainWindowSavedState.qml
        IMPORTS
            QGC
        NO_PLUGIN
    )
endif()

# iOS has no Qt SerialPort module — force no-serial-link before subdirectories read it.
if(IOS)
    set(QGC_NO_SERIAL_LINK ON CACHE BOOL "Disable serial port communication" FORCE)
endif()

# ============================================================================
# Subdirectories
# ============================================================================

# Core subsystems
add_subdirectory(ADSB)
if(QGC_ENABLE_QML)
    add_subdirectory(AnalyzeView)
endif()
add_subdirectory(API)
add_subdirectory(AutoPilotPlugins)
add_subdirectory(Camera)
add_subdirectory(Comms)
add_subdirectory(FactSystem)
add_subdirectory(FirmwarePlugin)
if(QGC_ENABLE_QML)
    add_subdirectory(FlyView)
    add_subdirectory(FlightMap)
endif()
add_subdirectory(FollowMe)
add_subdirectory(Gimbal)
add_subdirectory(GPS)
add_subdirectory(Utilities)
# Must be added after Utilities — links QGCLogging/QGCFileSystem/QGCNetwork/QGCCompression.
add_subdirectory(LogManager)
add_subdirectory(Joystick)
add_subdirectory(MAVLink)
add_subdirectory(MissionManager)
if(QGC_ENABLE_QML)
    add_subdirectory(PlanView)
endif()
add_subdirectory(PositionManager)
# QmlControls is NOT gated: FactPanelController/QmlObjectListModel/QGCPalette
# and friends are C++ dependencies of core code (Vehicle, Joystick, MAVLink,
# MissionManager, API, ...).
add_subdirectory(QmlControls)
add_subdirectory(Settings)
add_subdirectory(Terrain)
if(QGC_ENABLE_QML)
    add_subdirectory(AppSettings)
    add_subdirectory(FirstRunPromptDialogs)
    add_subdirectory(Toolbar)
endif()
add_subdirectory(Vehicle)
add_subdirectory(VideoManager)
if(QGC_ENABLE_QML)
    add_subdirectory(Viewer3D)
endif()
# Non-QML frontend bridge (websocket ghost API).
add_subdirectory(WebBridge)

# Platform-specific
add_subdirectory(Android)

# Qt plugins
add_subdirectory(QtLocationPlugin)

# ============================================================================
# Main Application Sources
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        main.cc
        pch.h
        QGCApplication.cc
        QGCApplication.h
)

if(QGC_DISABLE_NEW_VERSION_CHECK)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE QGC_DISABLE_NEW_VERSION_CHECK)
endif()

# ----------------------------------------------------------------------------
# Include Directories
# ----------------------------------------------------------------------------
target_include_directories(${CMAKE_PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}
)

# ============================================================================
# Link Libraries
# ============================================================================

target_link_libraries(${CMAKE_PROJECT_NAME}
    PRIVATE
        # Qt6 Core
        # QtGraphs backs AnalyzeView's MAVLinkChart/LogViewer charts and (only
        # via QQuickItem) QmlControls/TerrainProfile — both QML-only. Its .so
        # itself hard-NEEDs Quick3D/Quick3DRuntimeRender/Quick3DUtils, so
        # gating it also drops those from a headless link.
        $<$<BOOL:${QGC_ENABLE_QML}>:Qt6::Graphs>
        Qt6::Concurrent
        Qt6::Core
        Qt6::CorePrivate
        Qt6::Gui
        Qt6::HttpServer
        Qt6::Network
        Qt6::StateMachine
        Qt6::Svg
        Qt6::Xml

        # Qt6 QML/Quick (Qt6::QmlIntegration stays unconditional — header-only QML_ELEMENT macros)
        $<$<BOOL:${QGC_ENABLE_QML}>:Qt6::Qml>
        Qt6::QmlIntegration
        $<$<BOOL:${QGC_ENABLE_QML}>:Qt6::Quick>
        $<$<BOOL:${QGC_ENABLE_QML}>:Qt6::Quick3D>
        $<$<BOOL:${QGC_ENABLE_QML}>:Qt6::QuickControls2>
        $<$<BOOL:${QGC_ENABLE_QML}>:Qt6::QuickVectorImage>

        # Qt6 Location
        Qt6::Location
        Qt6::LocationPrivate
        Qt6::Positioning
        Qt6::PositioningPrivate
        Qt6::Sql

        # Qt6 Multimedia
        Qt6::Multimedia
        $<$<BOOL:${QGC_ENABLE_QML}>:Qt6::MultimediaQuickPrivate>

        # QGC Modules
        $<$<BOOL:${QGC_ENABLE_QML}>:AnalyzeViewModule>
        $<$<BOOL:${QGC_ENABLE_QML}>:AppSettingsModule>
        $<$<BOOL:${QGC_ENABLE_QML}>:AutoPilotPluginsCommonModule>
        $<$<BOOL:${QGC_ENABLE_QML}>:FactControlsModule>
        $<$<BOOL:${QGC_ENABLE_QML}>:FirstRunPromptDialogsModule>
        $<$<BOOL:${QGC_ENABLE_QML}>:FlyViewModule>
        $<$<BOOL:${QGC_ENABLE_QML}>:FlightMapModule>
        $<$<BOOL:${QGC_ENABLE_QML}>:PlanViewModule>
        $<$<BOOL:${QGC_ENABLE_QML}>:QGroundControlControlsModule>
        QGroundControlModule
        $<$<BOOL:${QGC_ENABLE_QML}>:ToolbarModule>
        $<$<BOOL:${QGC_ENABLE_QML}>:VehicleSetupModule>
        WebBridgeModule
)

# ----------------------------------------------------------------------------
# Optional/Conditional Modules
# ----------------------------------------------------------------------------
# AutoPilotPluginsAPM/PX4Module hold only QML config-page UI (see their own
# CMakeLists) — no target exists for them when QGC_ENABLE_QML is OFF.
if(NOT QGC_DISABLE_APM_PLUGIN AND QGC_ENABLE_QML)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE AutoPilotPluginsAPMModule)
endif()

if(NOT QGC_DISABLE_PX4_PLUGIN AND QGC_ENABLE_QML)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE AutoPilotPluginsPX4Module)
endif()

if(QGC_ENABLE_QML)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Viewer3DModule)
endif()

# ============================================================================
# Compile Definitions
# ============================================================================

# Application metadata (version macros provided via generated qgc_version.h)
target_compile_definitions(${CMAKE_PROJECT_NAME}
    PRIVATE
        $<$<NOT:$<BOOL:${QGC_STABLE_BUILD}>>:QGC_DAILY_BUILD>
        $<$<BOOL:${QGC_DISABLE_APM_MAVLINK}>:QGC_NO_ARDUPILOT_DIALECT>
)

# Build configuration-specific definitions
target_compile_definitions(${CMAKE_PROJECT_NAME}
    PRIVATE
        QT_DISABLE_DEPRECATED_UP_TO=${QGC_QT_DISABLE_DEPRECATED_UP_TO}
        QT_ENABLE_STRICT_MODE_UP_TO=${QGC_QT_ENABLE_STRICT_MODE_UP_TO}
)

target_compile_definitions(${CMAKE_PROJECT_NAME}
    PRIVATE
        $<$<CONFIG:Release>:QT_MESSAGELOGCONTEXT>
        $<$<AND:$<NOT:$<CONFIG:Release>>,$<BOOL:${QGC_DEBUG_QML}>,$<BOOL:${QGC_ENABLE_QML}>>:QT_QML_DEBUG>
)

# ============================================================================
# Target Properties
# ============================================================================

set_target_properties(${CMAKE_PROJECT_NAME}
    PROPERTIES
        OUTPUT_NAME ${CMAKE_PROJECT_NAME}
)

if(QGC_ENABLE_QML)
    set_target_properties(${CMAKE_PROJECT_NAME}
        PROPERTIES
            QT_RESOURCE_PREFIX "/qml"
    )
endif()

# Helper STATIC libs declare their own tailored PCH; REUSE_FROM is impractical
# because clang's PCH validator requires matching link-set-derived defines.
target_precompile_headers(${CMAKE_PROJECT_NAME} PRIVATE pch.h)

# Side-effect-free (no qt_add_qml_module / qt_add_resources / static-init registration) — link-interface pruning is safe.
set_target_properties(QGCFileSystem QGCSensors QGCAudio
    PROPERTIES OPTIMIZE_DEPENDENCIES ON
)

# ============================================================================
# Custom Build Configuration
# ============================================================================

if(QGC_CUSTOM_BUILD)
    if(CUSTOM_QT_COMPONENTS)
        find_package(Qt6 REQUIRED COMPONENTS ${CUSTOM_QT_COMPONENTS})
    endif()

    if(CUSTOM_SOURCES)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${CUSTOM_SOURCES})
    endif()

    if(CUSTOM_LIBRARIES)
        target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${CUSTOM_LIBRARIES})
    endif()

    if(CUSTOM_INCLUDE_DIRECTORIES)
        target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CUSTOM_INCLUDE_DIRECTORIES})
    endif()

    if(CUSTOM_DEFINITIONS)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ${CUSTOM_DEFINITIONS})
    endif()

    message(STATUS "QGC: Custom build configuration applied")
endif()
