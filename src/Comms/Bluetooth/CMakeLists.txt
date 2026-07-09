# Wave-12: real vehicle link TYPE, opt-out via QGC_ENABLE_BLUETOOTH (default
# ON — see cmake/CustomOptions.cmake). QGC_NO_BLUETOOTH_LINK is defined
# globally (root CMakeLists.txt) when the option is OFF; LinkManager/
# LinkConfiguration/QGCNetworkHelper guard their Bluetooth call sites on it.
if(QGC_ENABLE_BLUETOOTH)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Bluetooth)

    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            BluetoothBleWorker.cc
            BluetoothBleWorker.h
            BluetoothClassicWorker.cc
            BluetoothClassicWorker.h
            BluetoothConfiguration.cc
            BluetoothConfiguration.h
            BluetoothLink.cc
            BluetoothLink.h
            BluetoothWorker.cc
            BluetoothWorker.h
    )

    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
