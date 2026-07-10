# ============================================================================
# Terrain Unit Tests
# Tests for terrain elevation data and tile management
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        TerrainQueryTest.cc
        TerrainQueryTest.h
        TerrainTileTest.cc
        TerrainTileTest.h
)

# Q8f: TerrainTileFetcher (the subject) and its QNetworkAccessManager plumbing only exist
# with QGC_ENABLE_QT_NETWORK=ON (see src/Terrain/CMakeLists.txt).
if(QGC_ENABLE_QT_NETWORK)
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            TerrainTileFetcherNetworkTest.cc
            TerrainTileFetcherNetworkTest.h
    )
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
