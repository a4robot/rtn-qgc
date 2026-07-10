#pragma once

#include <QtCore/QByteArrayView>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QStringView>
#include <QtCore/QUrl>
#ifdef QGC_ENABLE_QT_NETWORK
#include <QtNetwork/QNetworkRequest>
#endif

#include <memory>

#include "QGCTileSet.h"

class MapProvider;
class ElevationProvider;

class UrlFactory
{
public:
    static QString getImageFormat(QStringView type, QByteArrayView image);
    static QString getImageFormat(int qtMapId, QByteArrayView image);

    static QUrl getTileURL(QStringView type, int x, int y, int zoom);
    static QUrl getTileURL(int qtMapId, int x, int y, int zoom);

#ifdef QGC_ENABLE_QT_NETWORK
    /// Builds a fully configured tile-fetch QNetworkRequest (headers, referrer, token,
    /// cache/redirect attributes) for the given map provider/tile coordinate.
    /// Location-free: does not depend on QtLocation/QGeoTileSpec. Shared by the
    /// QtLocation tile fetcher (QGeoTileFetcherQGC) and the Terrain tile fetcher.
    /// Q8f: QGC_ENABLE_QT_NETWORK=OFF removes every tile-fetch path (TerrainTileFetcher,
    /// QGCCachedTileSet, the QML map plugin), so this request builder goes with them.
    static QNetworkRequest getTileNetworkRequest(int qtMapId, int x, int y, int zoom);
#endif

    /* Note: QNetworkAccessManager queues the requests it receives. The number of requests executed in parallel is dependent on the protocol.
     * Currently, for the HTTP protocol on desktop platforms, 6 requests are executed in parallel for one host/port combination. */
    static uint32_t concurrentDownloads(QStringView type) { Q_UNUSED(type); return 6; }

    static quint32 averageSizeForType(QStringView type);

    static bool isElevation(int qtMapId);

    static int long2tileX(QStringView mapType, double lon, int z);
    static int lat2tileY(QStringView mapType, double lat, int z);

    static QGCTileSet getTileCount(int zoom, double topleftLon, double topleftLat,
                            double bottomRightLon, double bottomRightLat,
                            QStringView mapType);

    static const QList<std::shared_ptr<const MapProvider>>& getProviders() { return _providers; }
    static QStringList getElevationProviderTypes();
    static QStringList getProviderTypes();
    static int getQtMapIdFromProviderType(QStringView type);
    static QString getProviderTypeFromQtMapId(int qtMapId);
    static std::shared_ptr<const MapProvider> getMapProviderFromQtMapId(int qtMapId);
    static std::shared_ptr<const MapProvider> getMapProviderFromProviderType(QStringView type);
    static QString providerTypeFromHash(int hash);

    static int hashFromProviderType(QStringView type);
    static QString tileHashToType(QStringView tileHash);
    static QString getTileHash(QStringView type, int x, int y, int z);

private:
    static const QList<std::shared_ptr<const MapProvider>> _providers;
};

typedef std::shared_ptr<const MapProvider> SharedMapProvider;
typedef std::shared_ptr<const ElevationProvider> SharedElevationProvider;
