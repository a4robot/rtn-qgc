# ============================================================================
# Network Utilities Unit Tests
# Tests for networking helper functions
# ============================================================================

# Q8f: QGCNetworkHelper's request/reply/SSL tiers (most of what this test exercises,
# via NetworkReplyFixture) only exist with QGC_ENABLE_QT_NETWORK=ON; the URL/status
# helpers it also covers are identical code in both configs.
if(QGC_ENABLE_QT_NETWORK)
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            QGCNetworkHelperTest.cc
            QGCNetworkHelperTest.h
    )
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
