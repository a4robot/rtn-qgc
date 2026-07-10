# Q8f (STRANGLER_MILESTONES.md M8): QGCFileDownload/QGCCachedFileDownload are
# QNetworkAccessManager-based generic HTTP(S) downloaders with no in-tree portable
# replacement (see cmake/CustomOptions.cmake's QGC_ENABLE_QT_NETWORK comment) -- only built
# when QGC_ENABLE_QT_NETWORK=ON. QGCNetworkHelper.{cc,h} stays unconditional: most of it is
# internally `#ifdef QGC_ENABLE_QT_NETWORK`-gated already, keeping the small always-available
# subset (isNetworkEthernet(), initializeProxySupport(), URL/HTTP-status utility functions)
# usable by both configs (e.g. Comms/UDPLink.cc, TCPLink.cc's isSecureConnection()).
qt_add_library(QGCNetwork STATIC
    QGCNetworkHelper.cc
    QGCNetworkHelper.h
)

if(QGC_ENABLE_QT_NETWORK)
    target_sources(QGCNetwork
        PRIVATE
            QGCCachedFileDownload.cc
            QGCCachedFileDownload.h
            QGCFileDownload.cc
            QGCFileDownload.h
    )
endif()

target_include_directories(QGCNetwork
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QGCNetwork
    PUBLIC
        Qt6::Core
        $<$<BOOL:${QGC_ENABLE_QT_NETWORK}>:Qt6::Network>
    PRIVATE
        QGCCompression
        QGCLogging
)

if(QGC_ENABLE_QT_NETWORK)
    target_precompile_headers(QGCNetwork PRIVATE
        <QtCore/QObject>
        <QtCore/QString>
        <QtCore/QByteArray>
        <QtCore/QUrl>
        <QtNetwork/QNetworkReply>
    )
else()
    target_precompile_headers(QGCNetwork PRIVATE
        <QtCore/QObject>
        <QtCore/QString>
        <QtCore/QByteArray>
        <QtCore/QUrl>
    )
endif()

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE QGCNetwork)
