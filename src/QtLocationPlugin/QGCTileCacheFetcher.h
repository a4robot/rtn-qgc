#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <atomic>

class QGCFetchTileTask;

/// Location-free facade over the QGC disk tile-cache pipeline
/// (QGCMapEngine / QGCCacheWorker): task creation, tile persistence and
/// cache-directory/database bootstrap. Shared by the QtLocation-based map
/// plugin (QGeoFileTileCacheQGC, used for QML map rendering) and by
/// non-QtLocation consumers (TerrainTileFetcher in src/Terrain), so nothing
/// in here may depend on QtLocation.
class QGCTileCacheFetcher
{
public:
    static QGCFetchTileTask *createFetchTileTask(const QString &type, int x, int y, int z);
    static void cacheTile(const QString &type, int x, int y, int z, const QByteArray &image, const QString &format, qulonglong set = UINT64_MAX);
    static void cacheTile(const QString &type, const QString &hash, const QByteArray &image, const QString &format, qulonglong set = UINT64_MAX);

    /// Computes (once) the cache directory and database file path, wiping old
    /// incompatible caches. Idempotent and thread-safe.
    static void initPaths();

    /// initPaths() plus QGCMapEngine::init() with the resolved database path,
    /// so the cache worker thread is ready to service tasks. Idempotent.
    /// Called from the QtLocation map engine (QML builds) and from
    /// TerrainTileFetcher (headless terrain queries).
    static void ensureCacheDatabaseInitialized();

    static quint32 getMaxDiskCacheSetting();
    static QString getDatabaseFilePath() { initPaths(); return _databaseFilePath; }
    static QString getCachePath() { initPaths(); return _cachePath; }

private:
    static void _initCache();
    static bool _wipeDirectory(const QString &dirPath);
    static void _wipeOldCaches();

    // Initialized once via std::call_once in initPaths()
    static QString _databaseFilePath;
    static QString _cachePath;
    static std::atomic<bool> _cacheWasReset;
};
