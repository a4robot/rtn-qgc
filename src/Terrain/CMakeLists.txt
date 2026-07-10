# ============================================================================
# Terrain Module
# Provides terrain elevation data and queries using Copernicus data
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        Providers/TerrainTileCopernicus.cc
        Providers/TerrainTileCopernicus.h
        TerrainQuery.cc
        TerrainQuery.h
        TerrainQueryInterface.cc
        TerrainQueryInterface.h
        TerrainTile.cc
        TerrainTile.h
        TerrainTileManager.cc
        TerrainTileManager.h
)

# Q8f (STRANGLER_MILESTONES.md M8): TerrainTileFetcher (the network-fallback half of the
# tile cache -- TerrainTileManager's cache-hit path and TerrainTileCopernicus's byte
# decoding are unconditional above) and Providers/TerrainQueryCopernicus (a dead
# TerrainOnlineQuery subclass, never instantiated -- see TerrainQueryInterface.h's comment)
# only build when QGC_ENABLE_QT_NETWORK=ON; TerrainTileManager.cc's own `#ifdef
# QGC_ENABLE_QT_NETWORK` guards degrade a tile-cache miss to "unavailable" instead of a
# network fetch when OFF (see cmake/CustomOptions.cmake's QGC_ENABLE_QT_NETWORK comment).
if(QGC_ENABLE_QT_NETWORK)
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            Providers/TerrainQueryCopernicus.cc
            Providers/TerrainQueryCopernicus.h
            TerrainTileFetcher.cc
            TerrainTileFetcher.h
    )
endif()

target_include_directories(${CMAKE_PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Providers
)
