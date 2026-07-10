# ============================================================================
# QGroundControl Build Configuration Options
# All options can be overridden by custom builds via CustomOverrides.cmake
# ============================================================================

include(CMakeDependentOption)

# Load centralized build configuration from .github/build-config.json
include(BuildConfig)

# ============================================================================
# Application Metadata
# ============================================================================

set(QGC_APP_NAME "QGroundControl" CACHE STRING "Application name")
string(TIMESTAMP _copyright_year "%Y")
set(QGC_APP_COPYRIGHT "Copyright (c) ${_copyright_year} QGroundControl. All rights reserved." CACHE STRING "Copyright notice")
set(QGC_APP_DESCRIPTION "Open Source Ground Control App" CACHE STRING "Application description")
set(QGC_ORG_NAME "QGroundControl" CACHE STRING "Organization name")
set(QGC_ORG_DOMAIN "qgroundcontrol.com" CACHE STRING "Organization domain")
set(QGC_PACKAGE_NAME "org.mavlink.qgroundcontrol" CACHE STRING "Package identifier")

# Settings version - increment to clear stored settings on next boot after incompatible changes
set(QGC_SETTINGS_VERSION "9" CACHE STRING "Settings schema version")

# ============================================================================
# Build Configuration
# ============================================================================

option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)
option(QGC_STABLE_BUILD "Stable release build (disables daily build features)" OFF)
option(QGC_USE_CACHE "Enable compiler caching (ccache/sccache)" ON)
option(QGC_UNITY_BUILD "Enable unity builds for faster compilation" OFF)
option(QGC_BUILD_INSTALLER "Build platform installers/packages" ON)
option(QGC_ENABLE_WERROR "Treat compiler warnings as errors for QGC source code" ON)
option(QGC_DISABLE_NEW_VERSION_CHECK "Disable checking for newer versions of QGC on startup" OFF)

# Debug-dependent options
# Note: CMAKE_BUILD_TYPE is empty on multi-config generators (VS, Ninja Multi-Config).
# Multi-config generators always get _QGC_DEBUG_BUILD=TRUE because Debug is selected
# at build time, not configure time. Release-only CI jobs should pass
# -DQGC_BUILD_TESTING=OFF explicitly to skip test compilation.
if(CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_QGC_DEBUG_BUILD TRUE)
else()
    set(_QGC_DEBUG_BUILD FALSE)
endif()
cmake_dependent_option(QGC_BUILD_TESTING "Enable unit tests" ON "_QGC_DEBUG_BUILD" OFF)
cmake_dependent_option(QGC_DEBUG_QML "Enable QML debugging/profiling" ON "_QGC_DEBUG_BUILD" OFF)
cmake_dependent_option(QGC_ENABLE_COVERAGE "Enable code coverage instrumentation" OFF "_QGC_DEBUG_BUILD" OFF)
cmake_dependent_option(QT_QML_NO_CACHEGEN "Skip qmlcachegen (faster Debug builds, slower QML startup)" ON "_QGC_DEBUG_BUILD" OFF)
option(QGC_ENABLE_CLANG_TIDY "Enable clang-tidy static analysis during build" OFF)
option(QGC_TIME_TRACE "Emit per-TU Clang -ftime-trace JSON for build profiling (Clang only)" OFF)
option(QGC_SPLIT_DWARF "Use -gsplit-dwarf + --gdb-index for faster Debug links (Linux/Android ELF only; marginal win with mold)" OFF)

# Git options
option(GIT_SUBMODULE "Update submodules during configuration" OFF)

# Link parallelism (Ninja only)
set(QGC_LINK_PARALLEL_LEVEL 2 CACHE STRING "Maximum parallel link jobs (prevents OOM during LTO)")

# Coverage thresholds
set(QGC_COVERAGE_LINE_THRESHOLD 30 CACHE STRING "Minimum line coverage percentage")
set(QGC_COVERAGE_BRANCH_THRESHOLD 20 CACHE STRING "Minimum branch coverage percentage")

# Valgrind options
set(QGC_VALGRIND_TIMEOUT_MULTIPLIER 20 CACHE STRING "Timeout multiplier for Valgrind")

# ============================================================================
# Compression Format Options
# ============================================================================
# Core formats (gzip, xz, zstd, zip) are always enabled.
# These optional formats are rarely used in the drone ecosystem.

option(QGC_ENABLE_BZIP2 "Enable BZip2 decompression support" OFF)
option(QGC_ENABLE_LZ4 "Enable LZ4 decompression support" OFF)

# ============================================================================
# Communication Options
# ============================================================================

option(QGC_NO_SERIAL_LINK "Disable serial port communication" OFF)

# MockLink is normally Debug-only (developer/test tooling). This option embeds a
# simulated PX4 vehicle in Release builds too, so the headless ghost can serve a
# real vehicle over the web bridge without external SITL.
option(QGC_ENABLE_MOCKLINK "Build MockLink (simulated vehicle) into Release builds for ghost/bridge development" OFF)

# Bluetooth is a real vehicle link TYPE (BluetoothLink/BluetoothConfiguration in
# LinkManager/LinkConfiguration), reachable headless — disabling it removes GCS
# Bluetooth connectivity capability, not just headless dead weight. Default ON
# preserves current behavior; wave-12 diet builds opt out explicitly.
option(QGC_ENABLE_BLUETOOTH "Enable Bluetooth communication links" ON)

# ============================================================================
# UI Options
# ============================================================================

option(QGC_ENABLE_QML "Enable the QML UI (OFF builds the headless ghost core only)" ON)

# ============================================================================
# Sensor / Audio Options
# ============================================================================

# Ambient temperature/pressure/compass readings from local GCS-host hardware
# (QtSensors), used unconditionally in Vehicle/APMFirmwarePlugin baro
# compensation. Real capability on a GCS host with sensor hardware (e.g. a
# field laptop); dead weight on a headless server with none. Default ON
# preserves current behavior.
option(QGC_ENABLE_SENSORS "Enable local GCS-host sensor readings (ambient temp/pressure/compass) via QtSensors" ON)

# Text-to-speech audio warnings (QtTextToSpeech), called unconditionally from
# core Vehicle/VehicleLinkManager/QGCApplication boot code (both normal and
# headless). In the web-cockpit architecture, comm-status announcements are
# better surfaced by the browser UI than a speaker on the GCS-core host — but
# that's a wave-13 routing change, not this cut. Default ON preserves current
# behavior.
option(QGC_ENABLE_TEXTTOSPEECH "Enable text-to-speech audio output (QtTextToSpeech)" ON)

# ============================================================================
# Video Streaming Options
# ============================================================================

option(QGC_ENABLE_UVC "Enable UVC (USB Video Class) device support" ON)
option(QGC_ENABLE_GST_VIDEOSTREAMING "Enable GStreamer video backend" ON)
option(QGC_ENABLE_QT_VIDEOSTREAMING "Enable QtMultimedia video backend" OFF)
# Gates whether QtMultimediaReceiver/UVCReceiver (VideoReceiver/QtMultimedia/) compile at
# all, and therefore whether Qt6::Multimedia is required as an alternate-backend dependency.
# This is a separate capability from QGC_ENABLE_QT_VIDEOSTREAMING (which only selects it as
# the *active* backend) and QGC_ENABLE_UVC (which only toggles the UVC camera feature within
# it) — both of those are no-ops when this is OFF. The ghost's real video path is 100% raw
# GStreamer (GstVideoReceiver -> WebBridge's VideoStreamServer H.264 tap, see
# src/WebBridge/GhostVideoSource.*), and QtMultimediaReceiver/UVCReceiver render exclusively
# through QVideoSink/QQuickVideoOutput for the QML UI — dead weight on a headless build.
# Default ON preserves current (QML) desktop behavior.
option(QGC_ENABLE_QT_MULTIMEDIA_BACKEND "Enable the QtMultimedia video backend (QtMultimediaReceiver + UVCReceiver — alternate to GStreamer, QML-display only)" ON)

# ============================================================================
# Map/Terrain Tile Cache Options
# ============================================================================

# The sqlite-backed map/terrain tile cache (QGCTileCacheDatabase/QGCTileCacheWorker
# in QtLocationPlugin/, QGCSqlHelper in Utilities/Database/) is the only Qt6::Sql
# user in the tree. Disabling it degrades the cache-first tile/terrain fetch path
# (QGCTileCacheFetcher, driven by TerrainTileFetcher and the map tile fetcher in
# QGCMapEngine) to network-only: lookups always miss and cacheTile() is a no-op,
# no sqlite databases are opened. Default ON preserves current behavior; wave-14
# diet builds opt out explicitly.
option(QGC_ENABLE_TILE_CACHE "Enable the sqlite-backed map/terrain tile cache (Qt6::Sql)" ON)

# ============================================================================
# WebBridge Transport Options
# ============================================================================

# src/WebBridge/'s WS transport (STRANGLER_MILESTONES.md Q7e): QWebSocketServer/QWebSocket
# (Qt6::WebSockets) by default, matching upstream. OFF swaps in IXWebSocket (CPM-vendored, see
# src/WebBridge/CMakeLists.txt and IxWsTransport.cc's rationale comment) instead, dropping
# libQt6WebSockets from the ghost link -- the whole surface this touches is src/WebBridge/'s own
# code (100% additive-era strangler code, no upstream file), gated behind WsTransport.h so the
# protocol state machine in WebBridgeServer is identical either way. Default ON preserves current
# behavior; the ghost-diet build opts out explicitly.
option(QGC_ENABLE_QT_WEBSOCKETS "Use Qt's QWebSocketServer/QWebSocket for the WebBridge transport (OFF uses CPM-vendored IXWebSocket instead, dropping Qt6::WebSockets)" ON)

# ============================================================================
# State Machine Options
# ============================================================================

# src/Utilities/StateMachine/'s QGCStateMachine framework (STRANGLER_MILESTONES.md M8
# Q8b): QState/QStateMachine (Qt6::StateMachine) by default, matching upstream. OFF swaps
# in src/Utilities/StateMachine/portable/, a QObject-based reimplementation of the subset
# of the framework AUDIT.md (tools/ghost/statemachine-port/AUDIT.md) found actually used by
# the 5 real consumer machines (InitialConnectStateMachine, ComponentInformationManager,
# RequestMetaDataTypeStateMachine, and ParameterManager's two ad-hoc PARAM_SET/
# PARAM_REQUEST_READ machines). This is the highest-leverage remaining ghost-diet job:
# Qt6::StateMachine INTERFACE-links Qt6::Gui (and transitively Qt6::DBus) at the Qt package
# level regardless of whether anything actually uses Gui symbols (wave-14 Q7d finding), so as
# long as anything links the StateMachine Qt module -- and QGCStateMachine is the only thing
# in this repo that does -- libQt6Gui/libQt6DBus stay in the ghost's link graph even in the
# headless/no-QML build. Selected via include-path shadowing + a disjoint target_sources()
# list (see src/Utilities/StateMachine/CMakeLists.txt), not per-file #ifdef: the 90-file
# framework's ON path is untouched, so upstream sync is unaffected. Default ON preserves
# current behavior; the ghost-diet build opts out explicitly.
option(QGC_ENABLE_QT_STATEMACHINE "Use Qt's QState/QStateMachine for QGCStateMachine (OFF uses a portable QObject-based reimplementation instead, dropping Qt6::StateMachine and transitively Qt6::Gui/Qt6::DBus)" ON)

# ============================================================================
# Serial Port Options
# ============================================================================

# src/Comms/SerialLink.{h,cc}'s serial link data path (STRANGLER_MILESTONES.md M8
# Q8e): QSerialPort/QSerialPortInfo (Qt6::SerialPort) by default, matching upstream.
# OFF swaps in src/Comms/portable/, a termios(2)+libudev reimplementation of the
# narrow QSerialPort/QSerialPortInfo API surface actually used across the tree
# (SerialLink's worker-thread data path, QGCSerialPortInfo's board/bootloader
# detection, LinkManager's direct NMEA QSerialPort use, GPS/GPSProvider's blocking
# PX4-GPSDrivers callback bridge, and Vehicle/VehicleSetup/Bootloader.cc's firmware
# upload protocol — all four remaining real QSerialPort consumers besides the
# QML-only FirmwareUpgradeController board-scan UI). Selected via include-path
# shadowing of the `<QtSerialPort/QSerialPort>` / `<QtSerialPort/QSerialPortInfo>`
# module-style includes (see src/Comms/CMakeLists.txt and src/Comms/portable/
# QtSerialPort/), not per-file #ifdef: every consumer's `#include <QtSerialPort/...>`
# resolves correctly either way with zero consumer-side changes. libserialport (the
# obvious off-the-shelf C alternative) was considered and rejected: it's
# autotools-only (no upstream CMakeLists, so CPM vendoring would need hand-rolled
# glob/OBJECT-library glue anyway — see src/GPS/CMakeLists.txt's px4-gpsdrivers for
# how much that costs) and LGPL-3.0-licensed (a new copyleft-vendoring obligation
# this tree has otherwise avoided: IXWebSocket is BSD, PX4-GPSDrivers is BSD, the
# StateMachine portable/ port is in-house). termios+libudev needs no new external
# dependency — libudev.so is already the transitive backend Qt6::SerialPort itself
# uses for enumeration on Linux, so this replaces one abstraction over the same OS
# facility with a thinner one this tree controls directly. Default ON preserves
# current behavior; the ghost-diet build opts out explicitly.
option(QGC_ENABLE_QT_SERIALPORT "Use Qt's QSerialPort/QSerialPortInfo for the serial link layer (OFF uses a portable termios(2)+libudev backend instead, dropping Qt6::SerialPort)" ON)

# ============================================================================
# MAVLink Configuration
# ============================================================================

set(QGC_MAVLINK_GIT_REPO "https://github.com/mavlink/mavlink.git" CACHE STRING "MAVLink repository URL")
set(QGC_MAVLINK_GIT_TAG "b1fb5a1a32c41c6e46fea70600d626a0b5a8edbe" CACHE STRING "MAVLink repository commit/tag")
set(QGC_MAVLINK_DIALECT "all" CACHE STRING "MAVLink dialect")
set(QGC_MAVLINK_VERSION "2.0" CACHE STRING "MAVLink protocol version")

# ============================================================================
# Autopilot Plugin Configuration
# ============================================================================

# ArduPilot (APM) Plugin
option(QGC_DISABLE_APM_MAVLINK "Disable ArduPilot MAVLink dialect" OFF)
option(QGC_DISABLE_APM_PLUGIN "Disable ArduPilot plugin" OFF)
option(QGC_DISABLE_APM_PLUGIN_FACTORY "Disable ArduPilot plugin factory" OFF)

# PX4 Plugin
option(QGC_DISABLE_PX4_PLUGIN "Disable PX4 plugin" OFF)
option(QGC_DISABLE_PX4_PLUGIN_FACTORY "Disable PX4 plugin factory" OFF)

# ============================================================================
# Platform-Specific Configuration
# ============================================================================

# ----------------------------------------------------------------------------
# Android Platform
# ----------------------------------------------------------------------------
set(QGC_QT_ANDROID_COMPILE_SDK_VERSION "${QGC_CONFIG_ANDROID_PLATFORM}" CACHE STRING "Android compile SDK version")
set(QGC_QT_ANDROID_TARGET_SDK_VERSION "${QGC_CONFIG_ANDROID_PLATFORM}" CACHE STRING "Android target SDK version")
set(QGC_QT_ANDROID_MIN_SDK_VERSION "${QGC_CONFIG_ANDROID_MIN_SDK}" CACHE STRING "Android minimum SDK version")
set(QGC_ANDROID_PACKAGE_NAME "${QGC_PACKAGE_NAME}" CACHE STRING "Android package identifier")
set(QGC_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android" CACHE PATH "Android package source directory")
set(QT_ANDROID_DEPLOYMENT_TYPE "" CACHE STRING "Android deployment type (empty or Release)")
option(QT_ANDROID_SIGN_APK "Enable APK signing" OFF)
option(QT_ANDROID_SIGN_AAB "Enable AAB signing" OFF)
option(QT_USE_TARGET_ANDROID_BUILD_DIR "Use target-specific Android build directory" OFF)

# ----------------------------------------------------------------------------
# macOS Platform
# ----------------------------------------------------------------------------
set(QGC_MACOS_PLIST_PATH "${CMAKE_SOURCE_DIR}/deploy/macos/MacOSXBundleInfo.plist.in" CACHE FILEPATH "macOS Info.plist template path")
set(QGC_MACOS_BUNDLE_ID "${QGC_PACKAGE_NAME}" CACHE STRING "macOS bundle identifier")
set(QGC_MACOS_ICON_PATH "${CMAKE_SOURCE_DIR}/deploy/macos/qgroundcontrol.icns" CACHE FILEPATH "macOS application icon path")
set(QGC_MACOS_ENTITLEMENTS_PATH "${CMAKE_SOURCE_DIR}/deploy/macos/qgroundcontrol.entitlements" CACHE FILEPATH "macOS entitlements file path")
option(QGC_MACOS_UNIVERSAL_BUILD "Build macOS universal binary (x86_64h + arm64)" ON)

# ----------------------------------------------------------------------------
# iOS Platform
# ----------------------------------------------------------------------------
set(QGC_IOS_DEPLOYMENT_TARGET "${QGC_CONFIG_IOS_DEPLOYMENT_TARGET}" CACHE STRING "iOS minimum deployment target")
set(QGC_IOS_TARGETED_DEVICE_FAMILY "1,2" CACHE STRING "iOS targeted device family (1=iPhone, 2=iPad)")

# ----------------------------------------------------------------------------
# Linux Platform
# ----------------------------------------------------------------------------
option(QGC_CREATE_APPIMAGE "Create AppImage package after build" ON)
set(QGC_APPIMAGE_ICON_256_PATH "${CMAKE_SOURCE_DIR}/deploy/linux/QGroundControl_256.png" CACHE FILEPATH "AppImage 256x256 icon path")
set(QGC_APPIMAGE_ICON_SCALABLE_PATH "${CMAKE_SOURCE_DIR}/deploy/linux/QGroundControl.svg" CACHE FILEPATH "AppImage SVG icon path")
set(QGC_APPIMAGE_APPRUN_PATH "${CMAKE_SOURCE_DIR}/deploy/linux/AppRun" CACHE FILEPATH "AppImage AppRun script path")
set(QGC_APPIMAGE_DESKTOP_ENTRY_PATH "${CMAKE_SOURCE_DIR}/deploy/linux/org.mavlink.qgroundcontrol.desktop.in" CACHE FILEPATH "AppImage desktop entry path")
set(QGC_APPIMAGE_METADATA_PATH "${CMAKE_SOURCE_DIR}/deploy/linux/org.mavlink.qgroundcontrol.appdata.xml.in" CACHE FILEPATH "AppImage metadata path")
set(QGC_APPIMAGE_APPDATA_DEVELOPER "qgroundcontrol" CACHE STRING "AppImage developer name")

# ----------------------------------------------------------------------------
# Windows Platform
# ----------------------------------------------------------------------------
set(QGC_WINDOWS_INSTALL_HEADER_PATH "${CMAKE_SOURCE_DIR}/deploy/windows/installheader.bmp" CACHE FILEPATH "Windows installer header image")
set(QGC_WINDOWS_ICON_PATH "${CMAKE_SOURCE_DIR}/deploy/windows/WindowsQGC.ico" CACHE FILEPATH "Windows application icon")
set(QGC_WINDOWS_RESOURCE_FILE_PATH "${CMAKE_SOURCE_DIR}/deploy/windows/QGroundControl.rc" CACHE FILEPATH "Windows resource file")

# ============================================================================
# Qt Configuration
# ============================================================================

set(QGC_QT_MINIMUM_VERSION "${QGC_CONFIG_QT_MINIMUM_VERSION}" CACHE STRING "Minimum supported Qt version")
set(QGC_QT_MAXIMUM_VERSION "${QGC_CONFIG_QT_VERSION}" CACHE STRING "Maximum supported Qt version")

set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml" CACHE PATH "QML output directory")
set(QML_IMPORT_PATH "${QT_QML_OUTPUT_DIRECTORY}" CACHE STRING "Additional QML import paths")

option(QT_SILENCE_MISSING_DEPENDENCY_TARGET_WARNING "Silence missing dependency warnings" OFF)
option(QT_ENABLE_VERBOSE_DEPLOYMENT "Enable verbose deployment output" OFF)
option(QT_DEBUG_FIND_PACKAGE "Print search paths when package not found" ON)
# qmlls.ini writes into the source tree dirty the CI checkout.
if(DEFINED ENV{CI})
    set(_qmlls_ini_default OFF)
else()
    set(_qmlls_ini_default ON)
endif()
option(QT_QML_GENERATE_QMLLS_INI "Generate qmlls.ini for QML language server" ${_qmlls_ini_default})
unset(_qmlls_ini_default)
option(QT_QMLLINT_CONTEXT_PROPERTY_DUMP "Emit qmllint context property data (Qt 6.11+; no-op on older)" ON)
option(QT_QML_GENERATE_QMLLINT "Run qmllint at build time" OFF)

set(QGC_QT_DISABLE_DEPRECATED_UP_TO "0x060A00" CACHE STRING "Disable Qt APIs deprecated before this version")
set(QGC_QT_ENABLE_STRICT_MODE_UP_TO "0x060A00" CACHE STRING "Enable strict Qt API mode up to this version")

# Debug environment variables (uncomment to enable)
# set(ENV{QT_DEBUG_PLUGINS} "1")
# set(ENV{QML_IMPORT_TRACE} "1")

# ============================================================================
# CMake Package Manager (CPM)
# ============================================================================

# Uncomment to use named cache directories for better organization
# set(CPM_USE_NAMED_CACHE_DIRECTORIES ON CACHE BOOL "Use package name subdirectories in CPM cache")

# ============================================================================
# CMake Configuration
# ============================================================================

# Uncomment for verbose package finding
# option(CMAKE_FIND_DEBUG_MODE "Print search paths when finding packages" OFF)
