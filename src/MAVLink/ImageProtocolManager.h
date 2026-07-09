#pragma once

#include "MAVLinkLib.h"

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>
#ifdef QGC_ENABLE_QML
#include <QtGui/QImage>
#endif

/// \brief Supports the Mavlink image transmission protocol (https://mavlink.io/en/services/image_transmission.html).
///
/// Mainly used by optical flow cameras.
///
/// Q8d (STRANGLER_MILESTONES.md M8): Q7d gated the QML-ON-only QImage decode path out of the OFF
/// (ghost) build to let it drop libQt6Gui, at the cost of temporarily losing the image protocol in
/// headless builds. This class restores that capability without QImage: imageBytesReady() emits
/// the exact bytes reassembled from ENCAPSULATED_DATA packets -- the same bytes _getImage() would
/// otherwise have handed to QImage::loadFromData() -- so a non-QML consumer (WebBridge's
/// ImageChannel, src/WebBridge/ImageChannel.h) can forward them to the browser for decode, same
/// philosophy as the `video` channel (PROTOCOL.md §9: server forwards encoded bytes, client
/// decodes). imageReady(QImage)/_getImage() remain for QML builds and are compiled only under
/// QGC_ENABLE_QML, so this class touches no QtGui type at all in an OFF ghost build.
class ImageProtocolManager : public QObject
{
    Q_OBJECT

public:
    ImageProtocolManager(QObject *parent = nullptr);
    ~ImageProtocolManager();

    uint32_t flowImageIndex() const { return _flowImageIndex; }

    bool requestImage(uint8_t system_id, uint8_t component_id, uint8_t chan, mavlink_message_t &message);
    void cancelRequest(uint8_t system_id, uint8_t component_id, uint8_t chan, mavlink_message_t &message);

signals:
    /// Raw, still-encoded image bytes exactly as reassembled from ENCAPSULATED_DATA packets --
    /// emitted BEFORE any QImage decode and unconditionally (no QGC_ENABLE_QML gate, no QtGui
    /// dependency), so this is the only image signal available in an OFF (ghost) build.
    /// @p format is the MAVLINK_DATA_STREAM_IMG_* type carried by the preceding
    /// DATA_TRANSMISSION_HANDSHAKE, lower-cased to a wire-friendly string ("jpeg"/"png"/"bmp"/
    /// "pgm"/"raw8u"/"raw32u"/"unknown") -- see PROTOCOL.md §15. @p width/@p height are the
    /// handshake's declared dimensions (may be 0). @p imageIndex is the post-increment
    /// flowImageIndex() value for this image, matching the value flowImageIndexChanged() carries
    /// for the same completion.
    void imageBytesReady(const QByteArray &bytes, quint32 width, quint32 height, const QString &format, quint32 imageIndex);
#ifdef QGC_ENABLE_QML
    void imageReady(const QImage &image);
#endif
    void flowImageIndexChanged(uint32_t index);

public slots:
    void mavlinkMessageReceived(const mavlink_message_t &message);

private:
#ifdef QGC_ENABLE_QML
    QImage _getImage();
#endif

    /// Maps a MAVLINK_DATA_STREAM_IMG_* handshake type to the PROTOCOL.md §15 `format` wire
    /// string. Always available (no QGC_ENABLE_QML gate) -- used by imageBytesReady() in every
    /// build.
    static QString _formatString(uint8_t type);

    mavlink_data_transmission_handshake_t _imageHandshake = {};
    QByteArray _imageBytes;
    uint32_t _flowImageIndex = 0;
};
