# ============================================================================
# Communications Unit Tests
# Tests for serial port detection and communication links
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        LinkConfigurationTest.cc
        LinkConfigurationTest.h
        QGCSerialPortInfoTest.cc
        QGCSerialPortInfoTest.h
        TCPLinkTest.cc
        TCPLinkTest.h
        UDPLinkTest.cc
        UDPLinkTest.h
)

# Serial data-path round-trip (Wave 17 / Q8e): needs a real serial link layer, so it's
# excluded the same way the rest of this directory implicitly is under QGC_NO_SERIAL_LINK
# (Android/iOS builds don't build test/ at all, but keep this explicit and self-documenting).
if(NOT QGC_NO_SERIAL_LINK)
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            SerialPortDataPathTest.cc
            SerialPortDataPathTest.h
    )
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(Bluetooth)
