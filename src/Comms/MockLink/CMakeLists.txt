if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT QGC_ENABLE_MOCKLINK)
    return()
endif()

# Lets non-MockLink sources (e.g. QGCApplication's headless boot) detect at compile time
# whether MockLink is available, mirroring how gstqgc/CMakeLists.txt defines QGC_GST_STREAMING.
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE QGC_ENABLE_MOCKLINK)

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        MockConfiguration.cc
        MockConfiguration.h
        MockLink.cc
        MockLink.h
        MockLinkCamera.cc
        MockLinkCamera.h
        MockLinkFTP.cc
        MockLinkFTP.h
        MockLinkGimbal.cc
        MockLinkGimbal.h
        MockLinkWorker.cc
        MockLinkWorker.h
        MockLinkMissionItemHandler.cc
        MockLinkMissionItemHandler.h
)

# Add resource files
set_source_files_properties(MockLink.General.MetaData.json PROPERTIES QT_RESOURCE_ALIAS General.MetaData.json)
set_source_files_properties(MockLink.General.MetaData.json.xz PROPERTIES QT_RESOURCE_ALIAS General.MetaData.json.xz)
set_source_files_properties(MockLink.Parameter.MetaData.json.xz PROPERTIES QT_RESOURCE_ALIAS Parameter.MetaData.json.xz)
set_source_files_properties(MockLink.Parameter.MetaData.json PROPERTIES QT_RESOURCE_ALIAS Parameter.MetaData.json)
qt_add_resources(${CMAKE_PROJECT_NAME} mocklink_resources
    PREFIX "/MockLink"
    FILES
        PX4MockLink.params
        MockLink.General.MetaData.json
        MockLink.General.MetaData.json.xz
        MockLink.Parameter.MetaData.json.xz
        MockLink.Parameter.MetaData.json
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_precompile_headers(${CMAKE_PROJECT_NAME} PRIVATE "MockLink.h")
