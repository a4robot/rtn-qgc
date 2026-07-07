#include "WebBridge.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(WebBridgeLog, "WebBridge.WebBridge")

WebBridge::WebBridge(quint16 listenPort, QObject *parent)
    : QObject(parent)
    , _listenPort(listenPort)
{
    qCDebug(WebBridgeLog) << this << "listenPort" << _listenPort;

    // Anchor the monotonic clock to the wall clock exactly once; see serverTimeUs() docs.
    _epochAnchorUs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000;
    _monotonic.start();

    _tickTimer.setInterval(kTickIntervalMs);
    connect(&_tickTimer, &QTimer::timeout, this, &WebBridge::_sendTick);
}

WebBridge::~WebBridge()
{
    qCDebug(WebBridgeLog) << this;
}

quint64 WebBridge::serverTimeUs() const
{
    return _epochAnchorUs + static_cast<quint64>(_monotonic.nsecsElapsed() / 1000);
}

quint64 WebBridge::uptimeS() const
{
    if (!_running) {
        return 0;
    }
    return (serverTimeUs() - _startedTimeUs) / 1000000;
}

quint64 WebBridge::nextSeq(const QString &channel)
{
    return ++_seqCounters[channel];
}

void WebBridge::resetSeq(const QString &channel)
{
    _seqCounters.remove(channel);
}

QString WebBridge::streamKey(const QString &channel, int vehicleId)
{
    if (vehicleId == kNoVehicleId) {
        return channel;
    }
    return channel + QLatin1Char('/') + QString::number(vehicleId);
}

QJsonObject WebBridge::makeStreamMessage(const QString &channel, const QString &type, const QJsonObject &payload, int vehicleId)
{
    const quint64 seq = nextSeq(streamKey(channel, vehicleId));

    // Payload fields sit at the top level of the message (PROTOCOL.md §4 telemetry example);
    // envelope fields are written last so they win any name collision.
    QJsonObject message = payload;
    message[QStringLiteral("type")] = type;
    message[QStringLiteral("channel")] = channel;
    if (vehicleId != kNoVehicleId) {
        message[QStringLiteral("vehicleId")] = vehicleId;
    }
    message[QStringLiteral("seq")] = static_cast<qint64>(seq);
    message[QStringLiteral("snapshot")] = (seq == 1);
    message[QStringLiteral("timeUs")] = static_cast<qint64>(serverTimeUs());
    return message;
}

QJsonObject WebBridge::makeTick() const
{
    QJsonArray vehicleIds;
    for (const int vehicleId : _vehicleIds) {
        vehicleIds.append(vehicleId);
    }

    QJsonObject tick;
    tick[QStringLiteral("type")] = QStringLiteral("tick");
    tick[QStringLiteral("serverTimeUs")] = static_cast<qint64>(serverTimeUs());
    tick[QStringLiteral("uptimeS")] = static_cast<qint64>(uptimeS());
    tick[QStringLiteral("vehicleIds")] = vehicleIds;
    return tick;
}

void WebBridge::_sendTick()
{
    emit tickReady(makeTick());
}

void WebBridge::start()
{
    if (_running) {
        qCDebug(WebBridgeLog) << "start: already running";
        return;
    }

    _running = true;
    _startedTimeUs = serverTimeUs();
    _tickTimer.start();

    qCDebug(WebBridgeLog) << "started: listenPort" << _listenPort << "tick every" << kTickIntervalMs << "ms";
    emit started();
}

void WebBridge::stop()
{
    if (!_running) {
        qCDebug(WebBridgeLog) << "stop: not running";
        return;
    }

    _tickTimer.stop();
    _running = false;
    _seqCounters.clear();   // No state kept across runs; every stream restarts at seq 1 (§11.2)

    qCDebug(WebBridgeLog) << "stopped";
    emit stopped();
}
