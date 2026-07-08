#pragma once

#include <QtLocation/private/qgeotilefetcher_p.h>
#include <QtNetwork/QNetworkRequest>

#include "QGCMapUrlEngine.h"

class QGeoTiledMappingManagerEngineQGC;
class QGeoTiledMapReplyQGC;
class QGeoTileSpec;
class QNetworkAccessManager;

class QGeoTileFetcherQGC : public QGeoTileFetcher
{
    Q_OBJECT

public:
    explicit QGeoTileFetcherQGC(QNetworkAccessManager *networkManager, const QVariantMap &parameters, QGeoTiledMappingManagerEngineQGC *parent = nullptr);
    ~QGeoTileFetcherQGC();

    static QNetworkRequest getNetworkRequest(int mapId, int x, int y, int zoom);
    static uint32_t concurrentDownloads(const QString &type) { return UrlFactory::concurrentDownloads(type); }

private:
    QGeoTiledMapReply* getTileImage(const QGeoTileSpec &spec) final;
    bool initialized() const final;
    bool fetchingEnabled() const final;
    void timerEvent(QTimerEvent *event) final;
    void handleReply(QGeoTiledMapReply *reply, const QGeoTileSpec &spec) final;

    QNetworkAccessManager *m_networkManager = nullptr;
};
