#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>

#include "QGCMapTaskBase.h"

class QNetworkAccessManager;
class QNetworkReply;
class QGCFetchTileTask;
struct QGCCacheTile;

/// Fetches a single terrain elevation tile (disk cache first, network fallback),
/// reusing the same disk-cache pipeline (QGCMapEngine/QGCTileCacheFetcher) and
/// tile-request construction (UrlFactory) as the QtLocation map plugin, but
/// without depending on QtLocation itself: carries the tile address as plain
/// {providerType, mapId, x, y, zoom} rather than QGeoTileSpec, and is a bare
/// QObject rather than a QGeoTiledMapReply.
class TerrainTileFetcher : public QObject
{
    Q_OBJECT

public:
    explicit TerrainTileFetcher(QNetworkAccessManager *networkManager, const QString &providerType, int mapId, int x, int y, int zoom, QObject *parent = nullptr);
    ~TerrainTileFetcher();

    /// Kicks off the fetch (cache lookup, then network on cache miss).
    ///     @return true: fetch started, finished() will be emitted later.
    ///             false: could not even start (caller should discard this object).
    bool init();

    bool hasError() const { return _hasError; }
    const QString &errorString() const { return _errorString; }
    /// Raw tile bytes on success (empty on error).
    const QByteArray &tileData() const { return _tileData; }

    const QString &providerType() const { return _providerType; }
    int mapId() const { return _mapId; }
    int x() const { return _x; }
    int y() const { return _y; }
    int zoom() const { return _zoom; }

signals:
    void finished();

private slots:
    void _cacheTaskFetched(QGCCacheTile *tile);
    void _cacheTaskError(QGCMapTask::TaskType type, const QString &errorString);
    void _networkReplyFinished();
    void _networkReplyError();

private:
    void _fetchFromNetwork();
    void _finishWithError(const QString &errorString);
    void _finishWithData(const QByteArray &data);

    QNetworkAccessManager *_networkManager = nullptr;
    const QString _providerType;
    const int _mapId;
    const int _x;
    const int _y;
    const int _zoom;

    bool _initialized = false;
    bool _hasError = false;
    QString _errorString;
    QByteArray _tileData;
};
