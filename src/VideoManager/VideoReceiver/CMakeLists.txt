# ============================================================================
# Video Receiver Subsystem
# Video streaming backends (GStreamer and Qt Multimedia)
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME} PRIVATE VideoReceiver.h)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ----------------------------------------------------------------------------
# Video Backend Implementations
# ----------------------------------------------------------------------------
add_subdirectory(GStreamer)

# QtMultimediaReceiver/UVCReceiver — alternate backend to GStreamer, QML-display only
# (see QGC_ENABLE_QT_MULTIMEDIA_BACKEND doc comment in cmake/CustomOptions.cmake). Skipping
# this subdirectory when OFF is what actually drops the Qt6::Multimedia dependency for a
# headless build; QGC_QT_MULTIMEDIA_BACKEND is the matching compile definition consumed by
# VideoManager.cc/VideoManager2.cc to no-op their UVC/QtMultimedia device-query call sites.
if(QGC_ENABLE_QT_MULTIMEDIA_BACKEND)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE QGC_QT_MULTIMEDIA_BACKEND)
    add_subdirectory(QtMultimedia)
else()
    # UVC support is implemented by UVCReceiver inside this backend; compiling the
    # backend out means UVC is unavailable regardless of QGC_ENABLE_UVC. Defining
    # QGC_DISABLE_UVC here keeps every existing `#ifndef QGC_DISABLE_UVC` gate
    # (VideoSettings.cc/VideoSettings2.cc UVC device enumeration) working unchanged —
    # the QtMultimedia/CMakeLists.txt that normally defines it is skipped above.
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE QGC_DISABLE_UVC)
endif()
