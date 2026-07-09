# ============================================================================
# Platform Subsystem
# Platform abstractions, single-instance guard, and signal handling
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        Platform.cc
        Platform.h
        RunGuard.cc
        RunGuard.h
)

# Signal handling uses POSIX signals, not available on mobile platforms
if(NOT ANDROID AND NOT IOS)
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            SignalHandler.cc
            SignalHandler.h
    )
endif()

# iOS-specific screen management
if(IOS)
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            MobileScreenMgr.mm
            MobileScreenMgr.h
    )
endif()

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        QGCKeychain.cc
        QGCKeychain.h
)

# ----------------------------------------------------------------------------
# QtKeychain - secure credential storage
# ----------------------------------------------------------------------------
# Backends: macOS Keychain, Windows Credential Manager, iOS Keychain, Android Keystore,
# Linux libsecret/KWallet. QGCKeychain falls back to QSettings at runtime when no backend
# daemon is reachable (headless / minimal sessions).
#
# qtkeychain's Linux backend unconditionally requires Qt6::DBus (KWallet talks to it
# over a generated D-Bus interface; there's no upstream CMake option to build without
# it -- confirmed against qtkeychain's own CMakeLists.txt, wave 17 Q8g). A headless
# ghost has no D-Bus session/keychain daemon to talk to anyway, so QGCKeychain.cc's
# runtime fallback (QSettings-backed, already written for exactly this "headless
# environment" case) is used directly instead of linking qt6keychain (and therefore
# Qt6::DBus) at all when QGC_ENABLE_QML=OFF.
if(QGC_ENABLE_QML)
    CPMAddPackage(
        NAME qtkeychain
        GITHUB_REPOSITORY frankosterfeld/qtkeychain
        VERSION 0.15.0-dev
        GIT_TAG 53a8e6a31f7d24bc58d91515cc7fcbb34af554db
        SYSTEM YES
        OPTIONS
            "BUILD_WITH_QT6 ON"
            "BUILD_TEST_APPLICATION OFF"
            "BUILD_TESTING OFF"
            "BUILD_TRANSLATIONS OFF"
            "BUILD_SHARED_LIBS OFF"
    )
    qgc_disable_dependency_warnings(qt6keychain)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE qt6keychain)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE QGC_HAS_KEYCHAIN)
endif()

# ----------------------------------------------------------------------------
# libgcrypt early init (Linux)
# ----------------------------------------------------------------------------
# qtkeychain's libsecret backend uses libgcrypt. libgcrypt requires the application
# to call gcry_check_version() before any thread is started; Platform::initialize()
# does that, so we link it directly to make the call site reachable.

if(LINUX)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBGCRYPT IMPORTED_TARGET GLOBAL libgcrypt)
        if(LIBGCRYPT_FOUND)
            target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE PkgConfig::LIBGCRYPT)
            target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE QGC_HAS_LIBGCRYPT)
        endif()
    endif()
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
