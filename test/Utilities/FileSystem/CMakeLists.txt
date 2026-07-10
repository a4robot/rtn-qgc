# ============================================================================
# FileSystem Utilities Unit Tests
# Tests for file download and filesystem operations
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        QGCArchiveWatcherTest.cc
        QGCArchiveWatcherTest.h
        QGCFileHelperTest.cc
        QGCFileHelperTest.h
        QGCFileWatcherTest.cc
        QGCFileWatcherTest.h
)

# Q8f: QGCFileDownload/QGCCachedFileDownload (the subjects) only exist with
# QGC_ENABLE_QT_NETWORK=ON (see src/Utilities/Network/CMakeLists.txt).
if(QGC_ENABLE_QT_NETWORK)
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            QGCCachedFileDownloadTest.cc
            QGCCachedFileDownloadTest.h
            QGCFileDownloadTest.cc
            QGCFileDownloadTest.h
    )
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
