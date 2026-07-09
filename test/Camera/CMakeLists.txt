# ============================================================================
# Camera Unit Tests
# Tests for camera management and control
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        QGCCameraManagerTest.cc
        QGCCameraManagerTest.h
        VehicleCameraControlTest.cc
        VehicleCameraControlTest.h
        QGCVideoStreamInfoTest.cc
        QGCVideoStreamInfoTest.h
        VideoManagerTest.cc
        VideoManagerTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Absolute path to the camera-definition XML fixture used by
# VehicleCameraControlTest::_testCameraDefinitionParsing() to characterize the
# camera-definition XML parser (see VehicleCameraControl.cc). set_source_files_properties()
# is directory-scoped to where ${CMAKE_PROJECT_NAME} was *created* (top-level
# CMakeLists.txt), not where this source is added via target_sources() here, so
# use a target-wide compile definition instead.
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    CAMERA_DEFINITION_EXAMPLE_XML_PATH="${CMAKE_SOURCE_DIR}/src/Camera/camera_definition_example.xml"
)
