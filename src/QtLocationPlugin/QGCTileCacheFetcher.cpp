#include "QGCTileCacheFetcher.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QStandardPaths>

#include <mutex>

#include "AppMessages.h"
#include "AppSettings.h"
#include "MapsSettings.h"
#include "QGCCacheTile.h"
#include "QGCFileHelper.h"
#include "QGCLoggingCategory.h"
#include "QGCMapEngine.h"
#include "QGCMapTasks.h"
#include "QGCMapUrlEngine.h"
#include "SettingsManager.h"

QGC_LOGGING_CATEGORY(QGCTileCacheFetcherLog, "QtLocationPlugin.QGCTileCacheFetcher")

QString QGCTileCacheFetcher::_databaseFilePath;
QString QGCTileCacheFetcher::_cachePath;
std::atomic<bool> QGCTileCacheFetcher::_cacheWasReset = false;

QGCFetchTileTask *QGCTileCacheFetcher::createFetchTileTask(const QString &type, int x, int y, int z)
{
    const QString hash = UrlFactory::getTileHash(type, x, y, z);
    return new QGCFetchTileTask(hash);
}

void QGCTileCacheFetcher::cacheTile(const QString &type, int x, int y, int z, const QByteArray &image, const QString &format, qulonglong set)
{
    const QString hash = UrlFactory::getTileHash(type, x, y, z);
    cacheTile(type, hash, image, format, set);
}

void QGCTileCacheFetcher::cacheTile(const QString &type, const QString &hash, const QByteArray &image, const QString &format, qulonglong set)
{
    AppSettings * const appSettings = SettingsManager::instance()->appSettings();
    if (!appSettings->disableAllPersistence()->rawValue().toBool()) {
        QGCCacheTile *tile = new QGCCacheTile(hash, image, format, type, set);
        QGCSaveTileTask *task = new QGCSaveTileTask(tile);
        if (!getQGCMapEngine()->addTask(task)) {
            task->deleteLater();
        }
    }
}

void QGCTileCacheFetcher::initPaths()
{
    static std::once_flag cacheInit;
    std::call_once(cacheInit, []() {
        _initCache();
    });
}

void QGCTileCacheFetcher::ensureCacheDatabaseInitialized()
{
    initPaths();
    getQGCMapEngine()->init(_databaseFilePath);
}

quint32 QGCTileCacheFetcher::getMaxDiskCacheSetting()
{
    return SettingsManager::instance()->mapsSettings()->maxCacheDiskSize()->rawValue().toUInt();
}

bool QGCTileCacheFetcher::_wipeDirectory(const QString &dirPath)
{
    bool result = true;

    const QDir dir(dirPath);
    if (dir.exists(dirPath)) {
        _cacheWasReset = true;

        const QFileInfoList fileList = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::System | QDir::Hidden | QDir::AllDirs | QDir::Files, QDir::DirsFirst);
        for (const QFileInfo &info : fileList) {
            if (info.isDir()) {
                result = _wipeDirectory(info.absoluteFilePath());
            } else {
                result = QFile::remove(info.absoluteFilePath());
            }

            if (!result) {
                return result;
            }
        }
        result = dir.rmdir(dirPath);
    }

    return result;
}

void QGCTileCacheFetcher::_wipeOldCaches()
{
    const QStringList oldCaches = {"/QGCMapCache55", "/QGCMapCache100", "/QGCMapCache300"};
    for (const QString &cache : oldCaches) {
        QString oldCacheDir;
        #if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
            oldCacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        #else
            oldCacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
        #endif
        oldCacheDir += cache;
        _wipeDirectory(oldCacheDir);
    }
}

void QGCTileCacheFetcher::_initCache()
{
    _wipeOldCaches();

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    cacheDir += QStringLiteral("/QGCMapCache");
#else
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    cacheDir += QStringLiteral("/QGCMapCache");
#endif
    if (!QGCFileHelper::ensureDirectoryExists(cacheDir)) {
        qCWarning(QGCTileCacheFetcherLog) << "Could not create mapping disk cache directory:" << cacheDir;

        cacheDir = QDir::homePath() + QStringLiteral("/.qgcmapscache/");
        if (!QGCFileHelper::ensureDirectoryExists(cacheDir)) {
            qCWarning(QGCTileCacheFetcherLog) << "Could not create mapping disk cache directory:" << cacheDir;
            cacheDir.clear();
        }
    }

    _cachePath = cacheDir;
    if (!_cachePath.isEmpty()) {
        _databaseFilePath = QString(_cachePath + QStringLiteral("/qgcMapCache.db"));

        qCDebug(QGCTileCacheFetcherLog) << "Map Cache in:" << _databaseFilePath;
    } else {
        qCCritical(QGCTileCacheFetcherLog) << "Could not find suitable map cache directory.";
    }

    if (_cacheWasReset) {
        QGC::showAppMessage(QCoreApplication::translate("QGCTileCacheFetcher",
            "The Offline Map Cache database has been upgraded. "
            "Your old map cache sets have been reset."));
    }
}
