#pragma once

#include "UnitTest.h"

/// Wave 16 (Q8f prep) characterization tests for TerrainTileFetcher's network
/// fallback path (TerrainTileFetcher::_fetchFromNetwork/_networkReplyFinished/
/// _networkReplyError), exercised end-to-end against a small local HTTP
/// responder rather than a real map/terrain provider.
///
/// To reach the network path without depending on real internet, these route
/// TerrainTileFetcher through the "CustomURL Custom" map provider (its URL is
/// read from AppSettings::customURL() at request time) pointed at
/// 127.0.0.1 - see src/QtLocationPlugin/Providers/GenericMapProvider.cpp.
///
/// TerrainTileFetcher::init() first checks the disk tile cache and, on a
/// miss, gates the network attempt behind QGCNetworkHelper::isInternetAvailable()
/// (a QNetworkInformation reachability probe). In a sandboxed/offline test
/// container that probe can legitimately report "unavailable" even though
/// loopback HTTP works fine - when that happens these tests QSKIP with an
/// explanation rather than failing, matching the "expensive SKIP paths"
/// pattern documented in test/TESTING.md. The request/response *shape*
/// (headers, URL, timeout) is pinned unconditionally and without any network
/// I/O in UrlFactoryTest::_testGetTileNetworkRequest*.
class TerrainTileFetcherNetworkTest : public UnitTest
{
    Q_OBJECT

private slots:
    /// 200 OK with a canned tile body: TerrainTileFetcher::finished() fires,
    /// hasError() is false, and tileData() is exactly the served bytes.
    void _testNetworkFetchSuccessPath();

    /// 404 Not Found: Qt's QNetworkAccessManager maps this to
    /// QNetworkReply::ContentNotFoundError, which TerrainTileFetcher surfaces
    /// via _networkReplyError() (NOT the isHttpSuccess() statusCode check in
    /// _networkReplyFinished(), which is unreachable for real HTTP errors -
    /// reply->error() != NoError short-circuits first). Pins hasError()==true.
    void _testNetworkFetch404SurfacesAsError();

    /// Server accepts the connection and never responds: the 10s
    /// QNetworkRequest::transferTimeout() baked into
    /// UrlFactory::getTileNetworkRequest() fires QNetworkReply::OperationCanceledError
    /// (the wave-10 fix this pins), and finished() still arrives with
    /// hasError()==true rather than hanging forever.
    void _testNetworkFetchTimeoutSurfacesAsError();
};
