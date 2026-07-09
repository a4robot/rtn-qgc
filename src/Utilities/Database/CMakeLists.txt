# QGCSqlHelper is a Qt6::Sql-only utility used exclusively by
# QtLocationPlugin/QGCTileCacheDatabase.cpp (the sqlite-backed tile cache).
# Gated with its only consumer behind QGC_ENABLE_TILE_CACHE so Qt6::Sql is not
# required to link headless "no-cache" builds (wave 14 Q7b).
if(QGC_ENABLE_TILE_CACHE)
    qt_add_library(QGCDatabase STATIC
        QGCSqlHelper.cc
        QGCSqlHelper.h
    )

    target_include_directories(QGCDatabase
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(QGCDatabase
        PUBLIC
            Qt6::Core
            Qt6::Sql
    )

    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE QGCDatabase)
endif()
