#pragma once

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>

class Vehicle;
class QWebSocket;

Q_DECLARE_LOGGING_CATEGORY(FactChannelLog)

/// Handles the `fact` channel for WebBridge (PROTOCOL.md §6).
/// Processes getParam and setParam requests and sends back responses to specific clients.
class FactChannel : public QObject
{
    Q_OBJECT

public:
    explicit FactChannel(QObject *parent = nullptr);
    ~FactChannel() override;

public slots:
    /// Connects to WebBridgeServer::factMessageReceived
    void handleMessage(QWebSocket *client, const QJsonObject &message);

signals:
    /// Emitted when a response is ready to be sent back to a specific client.
    /// Connects to WebBridgeServer::reply
    void responseReady(QWebSocket *client, const QJsonObject &message);

    /// Emitted when an error occurs while processing a request.
    /// Connects to WebBridgeServer::replyError
    void errorReady(QWebSocket *client, const QString &code, const QString &message, bool retryable, const QString &id);

private slots:
    void _onVehicleAdded(Vehicle *vehicle);
    void _onVehicleRemoved(Vehicle *vehicle);
    void _paramSetSuccess(int componentId, const QString &paramName);
    void _paramSetFailure(int componentId, const QString &paramName);

private:
    struct PendingRequest {
        QWebSocket *client = nullptr;
        QString id;
        int vehicleId = -1;
    };

    QJsonObject _buildParamValueMessage(const QString &id, int vehicleId, const QString &path, class Fact *fact) const;

    QHash<int, Vehicle *> _vehicles;          ///< vehicleId -> Vehicle (not owned)
    QHash<QString, QList<PendingRequest>> _pendingRequests; ///< path -> list of pending setParam requests
};
