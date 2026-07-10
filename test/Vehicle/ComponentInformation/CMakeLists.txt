# ============================================================================
# Component Information Unit Tests
# Tests for vehicle component metadata caching and translation
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        ComponentInformationCacheTest.cc
        ComponentInformationCacheTest.h
        ComponentInformationManagerTest.cc
        ComponentInformationManagerTest.h
        RequestMetaDataTypeStateMachineTest.cc
        RequestMetaDataTypeStateMachineTest.h
)

# Q8f: ComponentInformationTranslation's download path uses QGCCachedFileDownload, which
# only exists with QGC_ENABLE_QT_NETWORK=ON (see src/Utilities/Network/CMakeLists.txt);
# this test constructs one directly.
if(QGC_ENABLE_QT_NETWORK)
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            ComponentInformationTranslationTest.cc
            ComponentInformationTranslationTest.h
    )
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ----------------------------------------------------------------------------
# Test Data Resources
# ----------------------------------------------------------------------------
qt_add_resources(${CMAKE_PROJECT_NAME} "VehicleTest_ComponentInformation_res"
    PREFIX "/unittest"
    FILES
        TranslationTest.json
        TranslationTest_de_DE.ts
)
