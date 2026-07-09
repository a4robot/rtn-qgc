#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>

Q_DECLARE_LOGGING_CATEGORY(VideoStreamServerLog)

/// Taps the encoded H.264 elementary stream out of a running GStreamer video pipeline and
/// re-emits it as WebBridge protocol frames (PROTOCOL.md §9 "video" channel).
///
/// Attachment model: mirrors the *recording* branch documented at the top of
/// GstVideoReceiver.cc (`_source-->_tee` fanning out to a decode branch and a recorder branch,
/// each added/removed from the shared `tee` at runtime — see GstVideoReceiver::startRecording()/
/// _makeFileSink()/_unlinkBranch()). This class adds a third tap the same way: it requests a new
/// `src_%u` pad on the pipeline's tee, wraps a queue->h264parse->appsink chain in its own bin
/// (ghost-padded, exactly like GstVideoReceiver::_makeFileSink()'s "sinkbin"), links the tee pad
/// to it, and syncs its state with the (already-PLAYING) pipeline. stop() reverses this: unlink
/// + release the tee pad, drive the bin to NULL, remove it from the pipeline.
///
/// GstVideoReceiver itself does not currently expose its `_tee`/`_pipeline` members (they are
/// private) — attach() takes them as `void *` (the exact idiom VideoReceiver.h already uses for
/// its GstElement/QQuickItem sink pointer in `startDecoding(void *sink)`), so this header has no
/// GStreamer dependency and compiles whether or not QGC_GST_STREAMING is defined. Wiring an
/// actual GstVideoReceiver instance to this class requires the orchestrator to add an accessor
/// exposing its tee + pipeline (out of scope here — see class docs in VideoStreamServer.cc).
///
/// All GStreamer-touching logic is compiled only under QGC_GST_STREAMING (matching how the
/// GStreamer/ subdirectory itself is conditionally built, see VideoManager/VideoReceiver/
/// GStreamer/CMakeLists.txt and QGC_GST_STREAMING guards in VideoManager.cc); when that macro is
/// not defined, attach() is an inert no-op returning false and stop()/isAttached() are no-ops.
class VideoStreamServer : public QObject
{
    Q_OBJECT

public:
    /// @param streamId Numeric stream id used both as PROTOCOL.md §9.2 binary-header `streamId`
    ///                 and as the `streamIndex` field of the §9.1 `videoConfig` JSON message.
    explicit VideoStreamServer(quint8 streamId, QObject *parent = nullptr);
    ~VideoStreamServer() override;

    quint8 streamId() const { return _streamId; }

    /// True once attach() has successfully wired the tap into a pipeline.
    bool isAttached() const { return _attached; }

    /// Attaches to a live GStreamer pipeline's tee element and begins producing frameReady()/
    /// configReady() signals. @p pipeline and @p tee are actually `GstElement *` (the pipeline
    /// owning the tee, and the shared branch-point tee itself); declared as `void *` so this
    /// header stays GStreamer-free (see class docs above). Both must be non-null and the
    /// pipeline must already be at least PAUSED (normally PLAYING, per GstVideoReceiver's own
    /// branch-attachment calls in startRecording()). Idempotent: returns true immediately if
    /// already attached. Returns false (and leaves the object detached) if GStreamer support was
    /// not compiled in, either pointer is null, or element construction/linking fails.
    bool attach(void *pipeline, void *tee);

    /// Detaches from the pipeline: unlinks and releases the tee request pad, drives the tap
    /// branch to GST_STATE_NULL, and removes it from the pipeline. Idempotent; safe to call when
    /// not attached (no-op) and safe to call again after a prior stop().
    void stop();

signals:
    /// One complete PROTOCOL.md §9.2 wire frame (16-byte header + one Annex-B H.264 access
    /// unit), ready to hand to the websocket transport as a single binary frame.
    ///
    /// Emitted directly from the GStreamer appsink callback, which runs on a GStreamer streaming
    /// thread — not this object's thread. That is intentional and safe: `emit` on a signal with
    /// the default Qt::AutoConnection re-checks the receiving QObject's thread affinity on every
    /// delivery (not just at connect() time), so Qt itself queues the invocation onto the
    /// receiver's thread when the emitting thread differs. QByteArray is implicitly-shared and
    /// safe to copy/pass across threads. No manual QMetaObject::invokeMethod marshaling is
    /// needed here (contrast GstVideoReceiver's `_dispatchSignal`, which exists only because that
    /// class's *worker thread* also runs pipeline-control code needing explicit dispatch).
    void frameReady(quint8 streamId, const QByteArray &framedPacket);

    /// PROTOCOL.md §9.1 videoConfig payload fields (codec, streamIndex, width, height, sps,
    /// pps) — NOT the full envelope (channel/type/seq/snapshot/timeUs), which the caller
    /// (WebBridgeServer) adds via WebBridge::makeStreamMessage(), matching how videoConfig sits
    /// in the same envelope shape as every other stream message (§3).
    ///
    /// Emitted whenever SPS/PPS or resolution is (re)observed: once for the first keyframe seen
    /// after attach(), and again whenever the extracted SPS/PPS bytes or width/height change.
    /// Because h264parse is configured with config-interval=-1 (§9.2's "keyframes must be
    /// preceded by in-band SPS/PPS" requirement), SPS/PPS is actually re-observed before *every*
    /// keyframe; this signal only re-fires when the bytes actually differ from the last one
    /// sent, so it does not fire once per GOP in the steady-state case. WebBridgeServer replays
    /// the last cached config to a newly-subscribing client (cacheVideoConfig()) and separately
    /// withholds this class's frameReady() binary frames from that client until its first
    /// keyframe (WebBridgeServer::broadcastBinary()'s per-client keyframe gate, PROTOCOL.md §9.2)
    /// -- this class itself stays a single global broadcaster of whatever the live pipeline emits
    /// next; it does not know about individual clients.
    ///
    /// Same cross-thread-emission note as frameReady() applies (QJsonObject is also
    /// implicitly-shared / safe to copy across threads).
    void configReady(quint8 streamId, const QJsonObject &videoConfig);

private:
#ifdef QGC_GST_STREAMING
    struct Impl;
    Impl *_impl = nullptr;

    /// Builds one §9.2 wire frame from already-extracted, plain-Qt-typed data and emits
    /// frameReady(); on keyframes, also compares @p width/@p height/@p sps/@p pps against the
    /// last-sent videoConfig and emits configReady() if anything changed. Deliberately free of
    /// GStreamer types (QByteArray/int only) so this header stays GStreamer-free even under this
    /// ifdef — the actual GstAppSink callback and Annex-B/caps parsing that produce these
    /// arguments live entirely in VideoStreamServer.cc (as a free function, not a member, so it
    /// never needs to appear in this header).
    void _handleAccessUnit(bool keyframe, quint64 timestampUs, const QByteArray &accessUnit,
                            int width, int height, const QByteArray &sps, const QByteArray &pps);
#endif

    const quint8 _streamId;
    bool _attached = false;
};
