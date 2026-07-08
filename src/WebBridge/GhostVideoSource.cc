#include "GhostVideoSource.h"

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(GhostVideoSourceLog, "WebBridge.GhostVideoSource")

#ifdef QGC_GST_STREAMING

#include "GstVideoReceiver.h"
#include "QGCCorePlugin.h"
#include "SettingsManager.h"
#include "VideoSettings.h"
#include "VideoStreamServer.h"

namespace
{
constexpr int kBaseRetryMs = 5000;
constexpr int kMaxRetryMs = 30000;
} // namespace

GhostVideoSource::GhostVideoSource(VideoStreamServer *tap, QObject *parent)
    : QObject(parent)
    , _tap(tap)
    , _backoffMs(kBaseRetryMs)
{
    _retryTimer.setSingleShot(true);
    connect(&_retryTimer, &QTimer::timeout, this, [this]() {
        if (!_desiredRunning) {
            return;
        }
        if (!_receiver && !_createReceiver()) {
            return; // _createReceiver() already rescheduled a retry
        }
        _startReceiverNow();
    });

    qCDebug(GhostVideoSourceLog) << this;
}

GhostVideoSource::~GhostVideoSource()
{
    stop();
}

// Mirrors VideoManager::_updateSettings()'s per-videoSource URI derivation (VideoManager.cc)
// for the sources that make sense for a headless ghost (no UVC camera enumeration, no
// autoStream/MAVLink-camera-negotiated URI -- both need a live Vehicle/UI this process doesn't
// have). videoDisabled/videoSourceNoVideo and anything unrecognized resolve to an empty URI.
QString GhostVideoSource::_resolveConfiguredUri() const
{
    VideoSettings *videoSettings = SettingsManager::instance()->videoSettings();
    if (!videoSettings) {
        return QString();
    }

    const QString source = videoSettings->videoSource()->rawValue().toString();
    if (source == VideoSettings::videoSourceUDPH264) {
        return QStringLiteral("udp://%1").arg(videoSettings->udpUrl()->rawValue().toString());
    }
    if (source == VideoSettings::videoSourceUDPH265) {
        return QStringLiteral("udp265://%1").arg(videoSettings->udpUrl()->rawValue().toString());
    }
    if (source == VideoSettings::videoSourceMPEGTS) {
        return QStringLiteral("mpegts://%1").arg(videoSettings->udpUrl()->rawValue().toString());
    }
    if (source == VideoSettings::videoSourceRTSP) {
        return videoSettings->rtspUrl()->rawValue().toString();
    }
    if (source == VideoSettings::videoSourceTCP) {
        return QStringLiteral("tcp://%1").arg(videoSettings->tcpUrl()->rawValue().toString());
    }
    if (source == VideoSettings::videoSource3DRSolo) {
        return QStringLiteral("udp://0.0.0.0:5600");
    }
    if (source == VideoSettings::videoSourceParrotDiscovery) {
        return QStringLiteral("udp://0.0.0.0:8888");
    }
    if (source == VideoSettings::videoSourceYuneecMantisG) {
        return QStringLiteral("rtsp://192.168.42.1:554/live");
    }
    if (source == VideoSettings::videoSourceHerelinkAirUnit) {
        return QStringLiteral("rtsp://192.168.0.10:8554/H264Video");
    }
    if (source == VideoSettings::videoSourceHerelinkHotspot) {
        return QStringLiteral("rtsp://192.168.43.1:8554/fpv_stream");
    }

    return QString();
}

bool GhostVideoSource::_createReceiver()
{
    VideoReceiver *receiver = QGCCorePlugin::instance()->createVideoReceiver(this);
    if (!receiver) {
        qCWarning(GhostVideoSourceLog) << "QGCCorePlugin::createVideoReceiver() returned null -- no video backend compiled in?";
        _scheduleRetry();
        return false;
    }

    auto *const gstReceiver = qobject_cast<GstVideoReceiver *>(receiver);
    if (!gstReceiver) {
        qCWarning(GhostVideoSourceLog) << "createVideoReceiver() did not return a GstVideoReceiver -- "
                                           "VideoStreamServer's tap only knows how to attach to a GStreamer tee";
        delete receiver;
        _scheduleRetry();
        return false;
    }

    _receiver = receiver;
    connect(_receiver, &VideoReceiver::streamingChanged, this, &GhostVideoSource::_onStreamingChanged);
    connect(_receiver, &VideoReceiver::onStartComplete, this, &GhostVideoSource::_onStartComplete);
    connect(_receiver, &VideoReceiver::onStopComplete, this, &GhostVideoSource::_onStopComplete);
    return true;
}

void GhostVideoSource::_startReceiverNow()
{
    _receiver->setUri(_configuredUri);
    _receiver->setLowLatency(SettingsManager::instance()->videoSettings()->lowLatencyMode()->rawValue().toBool());

    // Mirrors VideoManager::_startReceiver(): RTSP gets the user-configured negotiation timeout
    // (rtspsrc falls back from UDP to TCP after ~5s), everything else gets a short fixed timeout.
    const uint32_t timeout = _configuredUri.startsWith(QStringLiteral("rtsp://"))
                                  ? SettingsManager::instance()->videoSettings()->rtspTimeout()->rawValue().toUInt()
                                  : 3;

    _pipelineUp = false;
    qCDebug(GhostVideoSourceLog) << "starting receiver, uri" << _configuredUri << "timeout" << timeout;
    _receiver->start(timeout);
}

void GhostVideoSource::_scheduleRetry()
{
    if (!_desiredRunning) {
        return;
    }
    qCDebug(GhostVideoSourceLog) << "scheduling retry in" << _backoffMs << "ms";
    _retryTimer.start(_backoffMs);
    _backoffMs = qMin(_backoffMs * 2, kMaxRetryMs);
}

void GhostVideoSource::_onStreamingChanged(bool active)
{
    qCDebug(GhostVideoSourceLog) << "streamingChanged" << active;
    if (active) {
        _backoffMs = kBaseRetryMs; // successful stream -- reset backoff for the next disconnect
        auto *const gstReceiver = qobject_cast<GstVideoReceiver *>(_receiver);
        if (gstReceiver && !_tap->attach(gstReceiver->pipelineHandle(), gstReceiver->teeHandle())) {
            qCWarning(GhostVideoSourceLog) << "VideoStreamServer::attach() failed despite streamingChanged(true)";
        }
    } else {
        _tap->stop();
    }
}

void GhostVideoSource::_onStartComplete(VideoReceiver::STATUS status)
{
    switch (status) {
    case VideoReceiver::STATUS_OK:
        _pipelineUp = true;
        break;
    case VideoReceiver::STATUS_INVALID_URL:
        qCWarning(GhostVideoSourceLog) << "invalid video URI, staying inert until VideoSettings changes:" << _configuredUri;
        _pipelineUp = false;
        _desiredRunning = false;
        break;
    case VideoReceiver::STATUS_INVALID_STATE:
        // start() called while a pipeline already existed -- shouldn't happen given the
        // single-flight guarding in start()/_onStopComplete(), but harmless if it ever does.
        break;
    default:
        qCWarning(GhostVideoSourceLog) << "start failed, status" << status;
        _pipelineUp = false;
        _scheduleRetry();
        break;
    }
}

void GhostVideoSource::_onStopComplete(VideoReceiver::STATUS status)
{
    Q_UNUSED(status);
    _pipelineUp = false;
    _tap->stop();
    if (_desiredRunning) {
        qCDebug(GhostVideoSourceLog) << "receiver stopped (watchdog or requested restart) -- retrying";
        _scheduleRetry();
    }
}

void GhostVideoSource::start()
{
    const QString uri = _resolveConfiguredUri();
    if (uri.isEmpty()) {
        _desiredRunning = false;
        _retryTimer.stop();
        if (_receiver) {
            _tap->stop();
            _receiver->stop();
        }
        if (!_loggedNoUri) {
            qCInfo(GhostVideoSourceLog) << "no video source configured (VideoSettings videoSource/udpUrl/rtspUrl/tcpUrl) "
                                            "-- ghost video tap stays inert";
            _loggedNoUri = true;
        }
        return;
    }

    _loggedNoUri = false;
    const bool uriChanged = (uri != _configuredUri);
    _configuredUri = uri;
    _desiredRunning = true;
    _backoffMs = kBaseRetryMs;
    _retryTimer.stop();

    if (!_receiver) {
        if (!_createReceiver()) {
            return; // _createReceiver() already scheduled a retry
        }
        _startReceiverNow();
        return;
    }

    if (_pipelineUp && !uriChanged) {
        return; // already running with this uri -- idempotent
    }

    if (_pipelineUp) {
        // URI changed while running: stop first. _onStopComplete() will see _desiredRunning
        // still true (and _configuredUri already updated) and restart with the new URI itself.
        _receiver->stop();
        return;
    }

    _startReceiverNow();
}

void GhostVideoSource::stop()
{
    _desiredRunning = false;
    _retryTimer.stop();
    if (_tap) {
        _tap->stop();
    }
    if (_receiver) {
        _receiver->stop();
    }
    _pipelineUp = false;
}

#else // !QGC_GST_STREAMING

GhostVideoSource::GhostVideoSource(VideoStreamServer *tap, QObject *parent)
    : QObject(parent)
{
    Q_UNUSED(tap);
    qCDebug(GhostVideoSourceLog) << this << "GStreamer streaming support not compiled in (QGC_GST_STREAMING undefined) -- inert";
}

GhostVideoSource::~GhostVideoSource() = default;

void GhostVideoSource::start()
{
    static bool logged = false;
    if (!logged) {
        qCInfo(GhostVideoSourceLog) << "GStreamer streaming support not compiled in -- ghost video tap stays inert";
        logged = true;
    }
}

void GhostVideoSource::stop()
{
}

#endif // QGC_GST_STREAMING
