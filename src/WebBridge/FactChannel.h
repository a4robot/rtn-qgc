#pragma once

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>

class Vehicle;

Q_DECLARE_LOGGING_CATEGORY(FactChannelLog)

/// Handles the `fact` channel for WebBridge (PROTOCOL.md §6).
/// Processes getParam and setParam requests and sends back responses to specific clients.
///
/// Client identity: @p clientId below is WebBridgeServer's opaque per-connection quint64 (the
/// same one CommandChannel/MissionChannel already used) -- carried opaquely, never dereferenced.
/// Pre-Q7e this was a raw `QWebSocket *` handed straight through from WebBridgeServer; it became
/// quint64 as part of the Q7e transport swap (src/WebBridge/WsTransport.h) since QWebSocket no
/// longer exists in a QGC_ENABLE_QT_WEBSOCKETS=OFF build -- the only channel-class touch that
/// swap required, everything else here is unchanged.
class FactChannel : public QObject
{
    Q_OBJECT

public:
    explicit FactChannel(QObject *parent = nullptr);
    ~FactChannel() override;

public slots:
    /// Connects to WebBridgeServer::factMessageReceived
    void handleMessage(quint64 clientId, const QJsonObject &message);

signals:
    /// Emitted when a response is ready to be sent back to a specific client.
    /// Connects to WebBridgeServer::reply
    void responseReady(quint64 clientId, const QJsonObject &message);

    /// Emitted when an error occurs while processing a request.
    /// Connects to WebBridgeServer::replyError
    void errorReady(quint64 clientId, const QString &code, const QString &message, bool retryable, const QString &id);

private slots:
    void _onVehicleAdded(Vehicle *vehicle);
    void _onVehicleRemoved(Vehicle *vehicle);
    void _paramSetSuccess(int componentId, const QString &paramName);
    void _paramSetFailure(int componentId, const QString &paramName);

private:
    struct PendingRequest {
        quint64 clientId = 0;
        QString id;
        int vehicleId = -1;
    };

    QJsonObject _buildParamValueMessage(const QString &id, int vehicleId, const QString &path, class Fact *fact) const;

    QHash<int, Vehicle *> _vehicles;          ///< vehicleId -> Vehicle (not owned)
    QHash<QString, QList<PendingRequest>> _pendingRequests; ///< path -> list of pending setParam requests
};
