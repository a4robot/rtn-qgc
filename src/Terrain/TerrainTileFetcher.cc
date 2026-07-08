#include "TerrainTileFetcher.h"
#include "ElevationMapProvider.h"
#include "QGCCacheTile.h"
#include "QGCLoggingCategory.h"
#include "QGCMapEngine.h"
#include "QGCMapTasks.h"
#include "QGCMapUrlEngine.h"
#include "QGCNetworkHelper.h"
#include "QGCTileCacheFetcher.h"

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

QGC_LOGGING_CATEGORY(TerrainTileFetcherLog, "Terrain.TerrainTileFetcher")

TerrainTileFetcher::TerrainTileFetcher(QNetworkAccessManager *networkManager, const QString &providerType, int mapId, int x, int y, int zoom, QObject *parent)
    : QObject(parent)
    , _networkManager(networkManager)
    , _providerType(providerType)
    , _mapId(mapId)
    , _x(x)
    , _y(y)
    , _zoom(zoom)
{
    qCDebug(TerrainTileFetcherLog) << this << providerType << x << y << zoom;
}

TerrainTileFetcher::~TerrainTileFetcher()
{
    qCDebug(TerrainTileFetcherLog) << this;
}

bool TerrainTileFetcher::init()
{
    if (_initialized) {
        return true;
    }
    _initialized = true;

    // In headless (QML-OFF) builds the QtLocation map engine never runs, so
    // make sure the cache database/worker is up before enqueueing tasks.
    // Idempotent; in QML builds the map engine performs the same init.
    QGCTileCacheFetcher::ensureCacheDatabaseInitialized();

    QGCFetchTileTask * const task = QGCTileCacheFetcher::createFetchTileTask(_providerType, _x, _y, _zoom);
    if (!task) {
        qCWarning(TerrainTileFetcherLog) << "Failed to create fetch tile task";
        _initialized = false;
        return false;
    }
    (void) connect(task, &QGCFetchTileTask::tileFetched, this, &TerrainTileFetcher::_cacheTaskFetched);
    (void) connect(task, &QGCMapTask::error, this, &TerrainTileFetcher::_cacheTaskError);
    if (!getQGCMapEngine()->addTask(task)) {
        task->deleteLater();
        _initialized = false;
        return false;
    }

    return true;
}

void TerrainTileFetcher::_cacheTaskFetched(QGCCacheTile *tile)
{
    if (!tile) {
        _finishWithError(tr("Invalid Cache Tile"));
        return;
    }

    _finishWithData(tile->img);
    delete tile;
}

void TerrainTileFetcher::_cacheTaskError(QGCMapTask::TaskType type, const QString &errorString)
{
    Q_UNUSED(errorString);
    Q_ASSERT(type == QGCMapTask::TaskType::taskFetchTile);

    if (!QGCNetworkHelper::isInternetAvailable()) {
        _finishWithError(tr("Network Not Available"));
        return;
    }

    _fetchFromNetwork();
}

void TerrainTileFetcher::_fetchFromNetwork()
{
    const QNetworkRequest request = UrlFactory::getTileNetworkRequest(_mapId, _x, _y, _zoom);
    if (request.url().isEmpty()) {
        qCWarning(TerrainTileFetcherLog) << "Invalid map provider for mapId" << _mapId;
        _finishWithError(tr("Invalid Map Provider"));
        return;
    }

    QNetworkReply * const reply = _networkManager->get(request);
    reply->setParent(this);
    QGCNetworkHelper::ignoreSslErrorsIfNeeded(reply);

    (void) connect(reply, &QNetworkReply::finished, this, &TerrainTileFetcher::_networkReplyFinished);
    (void) connect(reply, &QNetworkReply::errorOccurred, this, &TerrainTileFetcher::_networkReplyError);
}

void TerrainTileFetcher::_networkReplyFinished()
{
    QNetworkReply * const reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        _finishWithError(tr("Unexpected Error"));
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        // _networkReplyError already (or will) handle finishing with an error.
        return;
    }

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!QGCNetworkHelper::isHttpSuccess(statusCode)) {
        _finishWithError(reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString());
        return;
    }

    QByteArray image = reply->readAll();
    if (image.isEmpty()) {
        _finishWithError(tr("Image is Empty"));
        return;
    }

    const SharedMapProvider mapProvider = UrlFactory::getMapProviderFromQtMapId(_mapId);
    if (!mapProvider) {
        _finishWithError(tr("Invalid Map Provider"));
        return;
    }

    if (mapProvider->isElevationProvider()) {
        const SharedElevationProvider elevationProvider = std::dynamic_pointer_cast<const ElevationProvider>(mapProvider);
        image = elevationProvider->serialize(image);
        if (image.isEmpty()) {
            _finishWithError(tr("Failed to Serialize Terrain Tile"));
            return;
        }
    }

    const QString format = mapProvider->getImageFormat(image);
    if (format.isEmpty()) {
        _finishWithError(tr("Unknown Format"));
        return;
    }

    QGCTileCacheFetcher::cacheTile(_providerType, _x, _y, _zoom, image, format);

    _finishWithData(image);
}

void TerrainTileFetcher::_networkReplyError()
{
    // Note: also reached for QNetworkReply::OperationCanceledError (transfer timeout).
    // Always finish with an error, otherwise TerrainTileManager would stay stuck in
    // its Downloading state waiting for a finished() that never comes.
    const QNetworkReply * const reply = qobject_cast<const QNetworkReply*>(sender());
    const QString errorString = reply ? reply->errorString() : tr("Invalid Reply");
    _finishWithError(errorString);
}

void TerrainTileFetcher::_finishWithError(const QString &errorString)
{
    _hasError = true;
    _errorString = errorString;
    emit finished();
}

void TerrainTileFetcher::_finishWithData(const QByteArray &data)
{
    _hasError = false;
    _tileData = data;
    emit finished();
}
