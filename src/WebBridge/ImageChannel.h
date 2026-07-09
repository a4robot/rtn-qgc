#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>

class Vehicle;
class WebBridge;

Q_DECLARE_LOGGING_CATEGORY(ImageChannelLog)

/// Implements the `image` channel (PROTOCOL.md §15, Q8d): forwards MAVLink image-transmission-
/// protocol bytes -- ImageProtocolManager::imageBytesReady() via Vehicle::imageBytesReady(), the
/// QImage-free twin of the QML-only imageReady()/QGCImageProvider path Q7d gated out of the OFF
/// ghost -- to the browser for decode. Same "server forwards encoded bytes, client decodes"
/// philosophy as the `video` channel (§9), but base64-in-JSON rather than a second binary-framing
/// path: see PROTOCOL.md §15 for the wire-format rationale (images are ~1 Hz max, so a second
/// binary-frame/keyframe-gate mechanism buys nothing here).
///
/// Vehicle-scoped and subscribed exactly like `telemetry`/`mission` (PROTOCOL.md §2/§3.1): each
/// tracked vehicle's *latest* image is state (principle §2.1 -- an optical-flow camera's most
/// recent frame is the current state of that data source, same as telemetry's most recent
/// attitude), so this class follows TelemetryChannel/MissionChannel's pattern (tracks
/// MultiVehicleManager's vehicle set, connects per-vehicle) rather than NotificationChannel's
/// unsubscribed-broadcast pattern. sendSnapshot() replays the last-received image for a
/// (re)subscribing client; unlike telemetry (always has *some* state), there may be nothing to
/// send yet if the vehicle has not completed an image transmission -- same "nothing to replay
/// yet" case as `video`'s cached videoConfig before the first keyframe.
///
/// Qt Core only (no QtWebSockets/QtQml/QtQuick, matching the rest of this module --
/// src/WebBridge/CMakeLists.txt).
class ImageChannel : public QObject
{
    Q_OBJECT

public:
    /// @param bridge Not owned. Used for the server clock / seq bookkeeping via
    ///                WebBridge::makeStreamMessage() and WebBridge::resetSeq()
    ///                (PROTOCOL.md §2.4). Must outlive this object.
    explicit ImageChannel(WebBridge *bridge, QObject *parent = nullptr);
    ~ImageChannel() override;

public slots:
    /// Connectable to WebBridgeServer::snapshotRequested(channel, vehicleId). Resets the image
    /// stream's sequence counter (PROTOCOL.md §2.4/§11.2) and, if this vehicle has received at
    /// least one image, immediately re-emits it as the snapshot (seq 1, PROTOCOL.md §2.2). If no
    /// image has arrived yet for @p vehicleId, this is a no-op -- the client simply receives the
    /// next `image` message whenever one completes (no snapshot to replay, same as `video`
    /// before the first keyframe).
    ///
    /// Requests for any channel other than "image", and requests for a vehicleId this class isn't
    /// tracking, are ignored: WebBridgeServer is expected to have already validated the (channel,
    /// vehicleId) pair (PROTOCOL.md §3.1) before routing here, so this is a defensive no-op
    /// rather than an error path.
    void sendSnapshot(const QString &channel, int vehicleId);

signals:
    /// One `image` channel message (PROTOCOL.md §3/§15), already wrapped by
    /// WebBridge::makeStreamMessage() with the envelope fields (`type`, `channel`, `seq`,
    /// `snapshot`, `timeUs`, `vehicleId`). `channel` is always "image". Emitted whenever the
    /// tracked vehicle completes a new image transmission, plus once immediately from
    /// sendSnapshot() (when a cached image exists).
    void imageReady(const QString &channel, int vehicleId, const QJsonObject &message);

private slots:
    void _onVehicleAdded(Vehicle *vehicle);
    void _onVehicleRemoved(Vehicle *vehicle);

private:
    /// Latest image received for one vehicle -- the payload fields of PROTOCOL.md §15's `image`
    /// message, pre-envelope.
    struct CachedImage {
        QByteArray bytes;
        quint32 width = 0;
        quint32 height = 0;
        QString format;
        quint32 imageIndex = 0;
    };

    /// Builds the `image` payload ({imageIndex, format, width, height, data}, PROTOCOL.md §15)
    /// from a cached image.
    QJsonObject _buildPayload(const CachedImage &image) const;

    WebBridge *_bridge = nullptr;                ///< Not owned
    QHash<int, Vehicle *> _vehicles;              ///< vehicleId -> Vehicle (not owned)
    QHash<int, CachedImage> _lastImage;           ///< vehicleId -> most recent completed image (may be absent)
};
