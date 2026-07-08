#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>

#include "VideoReceiver.h"

Q_DECLARE_LOGGING_CATEGORY(GhostVideoSourceLog)

class VideoStreamServer;

/// Gives the headless ghost a video source to feed WebBridge's VideoStreamServer tap.
///
/// Owns one VideoReceiver (a GstVideoReceiver when QGC_GST_STREAMING is compiled in, created via
/// QGCCorePlugin::createVideoReceiver() -- the same extension point VideoManager itself uses),
/// pointed at whatever URI VideoSettings currently has configured (mirrors the per-source URI
/// derivation VideoManager::_updateSettings() does for the UDP h.264/h.265, TCP, RTSP and
/// MPEG-TS cases; see VideoManager.cc). GstVideoReceiver::start() builds the shared tee and both
/// the decoder and recorder branches unconditionally, each behind a `valve` defaulting to
/// `drop=TRUE` (see the pipeline diagram atop GstVideoReceiver.cc) -- streamingChanged(true)
/// fires as soon as the source pad links into the tee (GstVideoReceiver::_onNewSourcePad()),
/// *before* any decoder is attached, so this class never needs to call startDecoding()/
/// startRecording() to get a live tee to attach the WebBridge tap to.
///
/// This header stays GStreamer-free: VideoReceiver.h (the abstract base both GstVideoReceiver and
/// QtMultimediaReceiver implement) has no GStreamer dependency, and the concrete
/// GstVideoReceiver/pipelineHandle()/teeHandle() types are only named in GhostVideoSource.cc
/// (matching the void*-handle idiom VideoStreamServer.h documents at its own class docs).
///
/// Inert by construction: start() is a no-op (logs once) when no VideoSettings URI is configured,
/// or when GStreamer support was not compiled in. Reconnection: if the receiver's own watchdog
/// stops it (no frames for the configured timeout, see GstVideoReceiver::_watchdog()) or a start
/// attempt fails, this class detaches the tap and retries with exponential backoff (base 5s,
/// capped at 30s, reset to base on a successful streamingChanged(true)).
class GhostVideoSource : public QObject
{
    Q_OBJECT

public:
    /// @param tap The WebBridge VideoStreamServer to attach()/stop() as the underlying receiver's
    ///            streaming state changes. Borrowed -- not owned; must outlive this object.
    explicit GhostVideoSource(VideoStreamServer *tap, QObject *parent = nullptr);
    ~GhostVideoSource() override;

    /// Reads VideoSettings' currently configured source (videoSource + udpUrl/rtspUrl/tcpUrl) and
    /// (re)starts the underlying receiver if the derived URI is non-empty and different from what
    /// is already running. Safe to call repeatedly (e.g. after a VideoSettings change) --
    /// idempotent no-op if the URI is unchanged and already (re)connecting/connected. Lazily
    /// creates the receiver on first call with a non-empty URI.
    void start();

    /// Detaches the tap (if attached), stops the receiver, and cancels any pending retry.
    /// Idempotent; safe to call when never started and safe to call again after a prior stop().
    void stop();

private:
#ifdef QGC_GST_STREAMING
    QString _resolveConfiguredUri() const;
    bool _createReceiver();
    void _startReceiverNow();
    void _scheduleRetry();
    void _onStreamingChanged(bool active);
    void _onStartComplete(VideoReceiver::STATUS status);
    void _onStopComplete(VideoReceiver::STATUS status);

    VideoStreamServer *const _tap;
    VideoReceiver *_receiver = nullptr;
    QTimer _retryTimer;
    QString _configuredUri;
    int _backoffMs = 0;
    bool _desiredRunning = false; // true iff a watchdog/error-driven stop should trigger a retry
    bool _pipelineUp = false;     // true between a successful start and the next stop
    bool _loggedNoUri = false;
#endif
};
