# ============================================================================
# Communications Module
# Handles all communication links (TCP, UDP, Serial, Bluetooth, etc.)
# ============================================================================

# ----------------------------------------------------------------------------
# Core Communication Sources
# ----------------------------------------------------------------------------
target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        LinkConfiguration.cc
        LinkConfiguration.h
        LinkInterface.cc
        LinkInterface.h
        LinkManager.cc
        LinkManager.h
        LogReplayLink.cc
        LogReplayLink.h
        LogReplayLinkController.cc
        LogReplayLinkController.h
        MAVLinkProtocol.cc
        MAVLinkProtocol.h
        TCPLink.cc
        TCPLink.h
        UdpIODevice.cc
        UdpIODevice.h
        UDPLink.cc
        UDPLink.h
)

# USB board information JSON data
qt_add_resources(${CMAKE_PROJECT_NAME} json_comm
    PREFIX "/json"
    FILES USBBoardInfo.json
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Uncomment to enable Qt socket debugging
# target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE QABSTRACTSOCKET_DEBUG)

# ----------------------------------------------------------------------------
# Network Link Layer (Q8f, STRANGLER_MILESTONES.md M8)
# ----------------------------------------------------------------------------
# Selected by QGC_ENABLE_QT_NETWORK (cmake/CustomOptions.cmake):
#   ON  (default, matches upstream) -> Qt6::Network provides the `<QtNetwork/...>` module
#                                       headers every consumer (UDPLink, TCPLink,
#                                       UdpIODevice, LinkManager's NMEA UDP socket,
#                                       ADSBTCPLink, VideoManager's Viewpro TCP command
#                                       socket, MAVLinkSigningKeys' QPasswordDigestor use,
#                                       QGCApplication's --bridge-host QHostAddress)
#                                       already includes.
#   OFF (ghost-diet build)          -> portable/, a POSIX-socket + QtCore-HMAC
#                                       reimplementation of the same narrow API surface,
#                                       selected via include-path shadowing of the
#                                       `<QtNetwork/...>` module-style includes -- zero
#                                       consumer-side #ifdef, same pattern as
#                                       QGC_ENABLE_QT_SERIALPORT/QGC_ENABLE_QT_STATEMACHINE.
#                                       See cmake/CustomOptions.cmake's QGC_ENABLE_QT_NETWORK
#                                       comment for what is (and, deliberately, is not)
#                                       covered by this port.
if(QGC_ENABLE_QT_NETWORK)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Network)
else()
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            portable/QGCNetworkCompat.cc
            portable/QGCNetworkCompat.h
            portable/QtNetwork/QAbstractSocket
            portable/QtNetwork/QHostAddress
            portable/QtNetwork/QHostInfo
            portable/QtNetwork/QNetworkDatagram
            portable/QtNetwork/QNetworkInterface
            portable/QtNetwork/QNetworkProxy
            portable/QtNetwork/QPasswordDigestor
            portable/QtNetwork/QTcpServer
            portable/QtNetwork/QTcpSocket
            portable/QtNetwork/QUdpSocket
    )

    # The redirector headers are extension-less (they must be, to shadow Qt's module-style
    # <QtNetwork/QUdpSocket> include form) -- mark them header-only so CMake never tries to
    # guess a source language for them.
    set_source_files_properties(
        portable/QtNetwork/QAbstractSocket
        portable/QtNetwork/QHostAddress
        portable/QtNetwork/QHostInfo
        portable/QtNetwork/QNetworkDatagram
        portable/QtNetwork/QNetworkInterface
        portable/QtNetwork/QNetworkProxy
        portable/QtNetwork/QPasswordDigestor
        portable/QtNetwork/QTcpServer
        portable/QtNetwork/QTcpSocket
        portable/QtNetwork/QUdpSocket
        PROPERTIES HEADER_FILE_ONLY ON
    )

    # portable/ is prepended to the include path so consumers' unqualified
    # `#include <QtNetwork/QUdpSocket>` resolves to our redirector headers instead of Qt's
    # module -- Qt6::Network is never linked in this branch, so there is no real
    # `QtNetwork/QUdpSocket` on the include path to shadow/conflict with. Applies tree-wide
    # because everything shares the single ${CMAKE_PROJECT_NAME} target (same mechanism the
    # SerialPort/StateMachine ports use). Shares the directory with the QGC_ENABLE_QT_SERIALPORT
    # port (portable/QtSerialPort/) -- adding it here unconditionally would be harmless (an
    # empty QtNetwork/ subdir shadows nothing when ON), but it's only actually needed, so it's
    # only added, in this branch.
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/portable)
endif()

# ============================================================================
# Optional Communication Features
# ============================================================================

# ----------------------------------------------------------------------------
# Serial Port Communication
# ----------------------------------------------------------------------------

if(QGC_NO_SERIAL_LINK)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE QGC_NO_SERIAL_LINK)
else()
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            QGCSerialPortInfo.cc
            QGCSerialPortInfo.h
            SerialLink.cc
            SerialLink.h
    )

    # Selected by QGC_ENABLE_QT_SERIALPORT (cmake/CustomOptions.cmake):
    #   ON  (default, matches upstream) -> Qt6::SerialPort provides the
    #                                       `<QtSerialPort/QSerialPort>` and
    #                                       `<QtSerialPort/QSerialPortInfo>`
    #                                       module headers every consumer
    #                                       (SerialLink, QGCSerialPortInfo,
    #                                       LinkManager's NMEA port,
    #                                       GPS/GPSProvider, VehicleSetup/
    #                                       Bootloader) already includes.
    #   OFF (ghost-diet build)          -> portable/, a termios(2)+libudev
    #                                       reimplementation of the same
    #                                       QSerialPort/QSerialPortInfo class
    #                                       names/API surface, selected via
    #                                       include-path shadowing of the
    #                                       `<QtSerialPort/...>` module-style
    #                                       includes -- zero consumer-side
    #                                       #ifdef, same pattern as
    #                                       src/Utilities/StateMachine's
    #                                       QGC_ENABLE_QT_STATEMACHINE port.
    #                                       Android is untouched: its
    #                                       `#ifdef Q_OS_ANDROID` include
    #                                       branch (qtandroidserialport/)
    #                                       takes priority over both.
    if(QGC_ENABLE_QT_SERIALPORT)
        if(NOT ANDROID)
            target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::SerialPort)
        endif()
    elseif(NOT ANDROID)
        target_sources(${CMAKE_PROJECT_NAME}
            PRIVATE
                portable/QGCSerialPortCompat.cc
                portable/QGCSerialPortCompat.h
                portable/QtSerialPort/QSerialPort
                portable/QtSerialPort/QSerialPortInfo
        )

        # The two redirector headers are extension-less (they must be, to shadow Qt's
        # module-style <QtSerialPort/QSerialPort> include form) -- mark them header-only
        # so CMake never tries to guess a source language for them.
        set_source_files_properties(
            portable/QtSerialPort/QSerialPort
            portable/QtSerialPort/QSerialPortInfo
            PROPERTIES HEADER_FILE_ONLY ON
        )

        # portable/ (and its QtSerialPort/ subdir) is prepended to the include
        # path so consumers' unqualified `#include <QtSerialPort/QSerialPort>`
        # resolves to our redirector headers instead of Qt's module -- Qt6::
        # SerialPort is never linked in this branch, so there is no real
        # `QtSerialPort/QSerialPort` on the include path to shadow/conflict
        # with. Applies tree-wide because everything shares the single
        # ${CMAKE_PROJECT_NAME} target (same mechanism the StateMachine port
        # uses for Helpers/ visibility).
        target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/portable)

        find_package(PkgConfig REQUIRED)
        # GLOBAL: the imported target is created in this subdirectory's scope but consumed
        # by the top-level ${CMAKE_PROJECT_NAME} target -- without it ninja fails with
        # "'PkgConfig::LIBUDEV' ... missing and no known rule to make it".
        pkg_check_modules(LIBUDEV REQUIRED IMPORTED_TARGET GLOBAL libudev)
        target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE PkgConfig::LIBUDEV)
    endif()
endif()

# ----------------------------------------------------------------------------
# Bluetooth Communication
# ----------------------------------------------------------------------------
add_subdirectory(Bluetooth)

# ============================================================================
# Communication Link Subdirectories
# ============================================================================

add_subdirectory(MockLink)
