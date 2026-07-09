#include "ImageChannel.h"

#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "Vehicle.h"
#include "WebBridge.h"

QGC_LOGGING_CATEGORY(ImageChannelLog, "WebBridge.ImageChannel")

namespace {
const QString kImageChannel = QStringLiteral("image");
} // namespace

ImageChannel::ImageChannel(WebBridge *bridge, QObject *parent)
    : QObject(parent)
    , _bridge(bridge)
{
    if (!_bridge) {
        qCWarning(ImageChannelLog) << "constructed with null WebBridge";
    }

    MultiVehicleManager *multiVehicleManager = MultiVehicleManager::instance();
    if (multiVehicleManager) {
        connect(multiVehicleManager, &MultiVehicleManager::vehicleAdded, this, &ImageChannel::_onVehicleAdded);
        connect(multiVehicleManager, &MultiVehicleManager::vehicleRemoved, this, &ImageChannel::_onVehicleRemoved);

        // Pick up vehicles that connected before this object was constructed; vehicleAdded()
        // only fires for vehicles added after the connect() above.
        QmlObjectListModel *vehicles = multiVehicleManager->vehicles();
        if (vehicles) {
            for (int i = 0; i < vehicles->count(); ++i) {
                _onVehicleAdded(vehicles->value<Vehicle *>(i));
            }
        }
    }

    qCDebug(ImageChannelLog) << this << "bridge" << static_cast<void*>(_bridge);
}

ImageChannel::~ImageChannel()
{
    qCDebug(ImageChannelLog) << this;
}

void ImageChannel::_onVehicleAdded(Vehicle *vehicle)
{
    if (!vehicle) {
        return;
    }

    const int vehicleId = vehicle->id();
    qCDebug(ImageChannelLog) << "_onVehicleAdded" << vehicleId;
    _vehicles[vehicleId] = vehicle;

    connect(vehicle, &Vehicle::imageBytesReady, this,
            [this, vehicleId](const QByteArray &bytes, quint32 width, quint32 height, const QString &format, quint32 imageIndex) {
        if (!_bridge) {
            return;
        }

        CachedImage image;
        image.bytes = bytes;
        image.width = width;
        image.height = height;
        image.format = format;
        image.imageIndex = imageIndex;
        _lastImage[vehicleId] = image;

        const QJsonObject payload = _buildPayload(image);
        const QJsonObject message = _bridge->makeStreamMessage(kImageChannel, kImageChannel, payload, vehicleId);
        emit imageReady(kImageChannel, vehicleId, message);
    });

    // Defensive: guarantee the vehicle is dropped even if it is destroyed without
    // MultiVehicleManager::vehicleRemoved() having fired first (mirrors TelemetryChannel).
    connect(vehicle, &QObject::destroyed, this, [this, vehicleId] {
        _vehicles.remove(vehicleId);
        _lastImage.remove(vehicleId);
    });
}

void ImageChannel::_onVehicleRemoved(Vehicle *vehicle)
{
    if (!vehicle) {
        return;
    }

    qCDebug(ImageChannelLog) << "_onVehicleRemoved" << vehicle->id();
    _vehicles.remove(vehicle->id());
    _lastImage.remove(vehicle->id());
}

void ImageChannel::sendSnapshot(const QString &channel, int vehicleId)
{
    if (channel != kImageChannel) {
        return;
    }

    if (!_bridge || !_vehicles.contains(vehicleId)) {
        qCDebug(ImageChannelLog) << "sendSnapshot: unknown vehicleId" << vehicleId;
        return;
    }

    const auto it = _lastImage.constFind(vehicleId);
    if (it == _lastImage.constEnd()) {
        // No image received yet for this vehicle -- nothing to snapshot. The client just gets
        // the next `image` message whenever ImageProtocolManager completes one (§15's "video's
        // cached videoConfig before the first keyframe" analogy).
        qCDebug(ImageChannelLog) << "sendSnapshot: no image yet for vehicleId" << vehicleId;
        return;
    }

    // Snapshot is always seq 1 (PROTOCOL.md §2.2/§2.4): reset before building the message so
    // WebBridge::makeStreamMessage()'s internal nextSeq() call returns 1.
    _bridge->resetSeq(WebBridge::streamKey(kImageChannel, vehicleId));

    const QJsonObject payload = _buildPayload(it.value());
    const QJsonObject message = _bridge->makeStreamMessage(kImageChannel, kImageChannel, payload, vehicleId);
    emit imageReady(kImageChannel, vehicleId, message);
}

QJsonObject ImageChannel::_buildPayload(const CachedImage &image) const
{
    QJsonObject payload;
    payload[QStringLiteral("imageIndex")] = static_cast<qint64>(image.imageIndex);
    payload[QStringLiteral("format")] = image.format;
    payload[QStringLiteral("width")] = static_cast<int>(image.width);
    payload[QStringLiteral("height")] = static_cast<int>(image.height);
    // PROTOCOL.md §15: base64-in-JSON, no separate binary framing (see the doc rationale).
    payload[QStringLiteral("data")] = QString::fromLatin1(image.bytes.toBase64());
    return payload;
}
