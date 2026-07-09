# ============================================================================
# QtLocationPlugin Unit Tests
# Tests for tile cache database operations
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        MapProviderTest.cc
        MapProviderTest.h
        QGCCachedTileSetTest.cc
        QGCCachedTileSetTest.h
        QGCTileSetTest.cc
        QGCTileSetTest.h
        UrlFactoryTest.cc
        UrlFactoryTest.h
)

# These exercise the real sqlite-backed tile cache persistence (save-then-fetch,
# archive import success path) — they don't hold against the QGC_ENABLE_TILE_CACHE=OFF
# stub (QGCTileCacheDatabase.cpp), which is always-empty by design. Gate with
# their subject (wave 14 Q7b).
if(QGC_ENABLE_TILE_CACHE)
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            QGCMapEngineManagerArchiveTest.cc
            QGCMapEngineManagerArchiveTest.h
            QGCCacheWorkerTest.cc
            QGCCacheWorkerTest.h
            QGCTileCacheDatabaseTest.cc
            QGCTileCacheDatabaseTest.h
    )
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
