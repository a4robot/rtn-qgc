#include "VideoManager2.h"
#include "AppSettings.h"
#include "MavlinkCameraControlInterface.h"
#include "MultiVehicleManager.h"
#include "AppMessages.h"
#include "QGCApplication.h"
#include "QGCCameraManager.h"
#include "QGCCorePlugin.h"
#include "QGCLoggingCategory.h"
#include "QGCVideoStreamInfo.h"
#include "SettingsManager.h"
#include "SubtitleWriter.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"
#include "VideoReceiver.h"
#include "VideoSettings2.h"
#include "QtMultimediaReceiver.h"
#include "UVCReceiver.h"
#include <QTcpSocket>
#include <QRegularExpression>
#ifdef QGC_GST_STREAMING
#include "GStreamerHelpers.h"
#include "GStreamer.h"
#if defined(QGC_HAS_ANY_GPU_PATH)
#include "VideoReceiver/GStreamer/HwBuffers/QGCRhiCapture.h"
#endif
#include <QtMultimedia/QVideoSink>
#include <QtMultimediaQuick/private/qquickvideooutput_p.h>
#endif

#include <QtConcurrent/QtConcurrent>
#include <QtCore/QApplicationStatic>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFutureWatcher>
#include <QtCore/QPointer>
#include <QtCore/QRunnable>
#include <QtCore/QTimer>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

QGC_LOGGING_CATEGORY(VideoManager2Log, "Video.VideoManager2")

static constexpr const char *kFileExtension[VideoReceiver::FILE_FORMAT_MAX + 1] = {
    "mkv",
    "mov",
    "mp4"
};

Q_APPLICATION_STATIC(VideoManager2, _videoManager2Instance);

bool VideoManager2::_shouldSkipGStreamerForUnitTests()
{
    return qgcApp() && QGC::runningUnitTests() && !qEnvironmentVariableIsSet("QGC_TEST_ENABLE_GSTREAMER");
}

VideoManager2::VideoManager2(QObject *parent)
    : QObject(parent)
    , _subtitleWriter(new SubtitleWriter(this))
    , _videoSettings2(SettingsManager::instance()->videoSettings2())
{
    qCDebug(VideoManager2Log) << this;

    (void) qRegisterMetaType<VideoReceiver::STATUS>("STATUS");

#ifdef QGC_GST_STREAMING
    _gstreamerDisabledForUnitTests = _shouldSkipGStreamerForUnitTests();
    if (_gstreamerDisabledForUnitTests) {
        qCInfo(VideoManager2Log) << "Skipping GStreamer initialization for unit tests";
    }
#endif
}

VideoManager2::~VideoManager2()
{
    qCDebug(VideoManager2Log) << this;
}

VideoManager2 *VideoManager2::instance()
{
    return _videoManager2Instance();
}

void VideoManager2::startGStreamerInit()
{
#ifdef QGC_GST_STREAMING
    if (_gstreamerDisabledForUnitTests) {
        _initState = InitState::GstReady;
        qCInfo(VideoManager2Log) << "GStreamer initialization disabled for unit tests";
        return;
    }

    if (_initState != InitState::NotStarted) {
        qCWarning(VideoManager2Log) << "GStreamer init already started";
        return;
    }

    _initState = InitState::Pending;

    // GStreamer initialization is done by VideoManager
    _gstInitFuture = QtConcurrent::run([]() { return true; });

    _gstInitFuture.then(this, [this](bool success) {
        _onGstInitComplete(success);
    }).onCanceled(this, [this] {
        _onGstInitComplete(false);
    });
#endif
}

bool VideoManager2::waitForGStreamerInit(int timeoutMs)
{
#ifdef QGC_GST_STREAMING
    if (_gstreamerDisabledForUnitTests) {
        return true;
    }

    if (_initState == InitState::NotStarted) {
        startGStreamerInit();
    }

    switch (_initState) {
    case InitState::Failed:
        return false;
    case InitState::GstReady:
    case InitState::Running:
        return true;
    default:
        break;
    }

    if (!_gstInitFuture.isValid()) {
        qCCritical(VideoManager2Log) << "waitForGStreamerInit: no valid future";
        return false;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QFutureWatcher<bool> watcher;
    (void) connect(&watcher, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::quit);
    (void) connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    watcher.setFuture(_gstInitFuture);
    if (!watcher.isFinished()) {
        timer.start(timeoutMs);
        loop.exec();
    }

    if (!watcher.isFinished()) {
        qCCritical(VideoManager2Log) << "Timed out waiting for GStreamer init";
        return false;
    }

    const bool success = watcher.result();
    if (_initState == InitState::Pending || _initState == InitState::QmlReady) {
        _onGstInitComplete(success);
    }
    return _initState != InitState::Failed;
#else
    Q_UNUSED(timeoutMs);
    return true;
#endif
}

void VideoManager2::init(QQuickWindow *mainWindow)
{
    if (_initialized) {
        qCDebug(VideoManager2Log) << "Video Manager already initialized";
        return;
    }

    if (!mainWindow) {
        qCCritical(VideoManager2Log) << "Failed To Init Video Manager - mainWindow is NULL";
        return;
    }
    _mainWindow = mainWindow;

#if defined(QGC_HAS_ANY_GPU_PATH)
    QGCRhiCapture::connectWindow(mainWindow);  // populate cached QRhi for GPU bridge handlers
#endif

    (void) connect(_videoSettings2->videoSource(), &Fact::rawValueChanged, this, &VideoManager2::_videoSourceChanged);
    (void) connect(_videoSettings2->udpUrl(), &Fact::rawValueChanged, this, &VideoManager2::_videoSourceChanged);
    (void) connect(_videoSettings2->rtspUrl(), &Fact::rawValueChanged, this, &VideoManager2::_videoSourceChanged);
    (void) connect(_videoSettings2->tcpUrl(), &Fact::rawValueChanged, this, &VideoManager2::_videoSourceChanged);
    (void) connect(_videoSettings2->aspectRatio(), &Fact::rawValueChanged, this, &VideoManager2::aspectRatioChanged);
    (void) connect(_videoSettings2->lowLatencyMode(), &Fact::rawValueChanged, this, [this](const QVariant &value) { Q_UNUSED(value); _restartAllVideos(); });
    (void) connect(SettingsManager::instance()->appSettings()->gstDebugLevel(), &Fact::rawValueChanged, this, [](const QVariant &value) {
#ifdef QGC_GST_STREAMING
        GStreamer::setDebugLevel(value.toInt());
#else
        Q_UNUSED(value);
#endif
    });
    (void) connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged, this, &VideoManager2::_setActiveVehicle);

    (void) connect(this, &VideoManager2::autoStreamConfiguredChanged, this, &VideoManager2::_videoSourceChanged);

#ifdef QGC_GST_STREAMING
    if (_initState == InitState::NotStarted) {
        startGStreamerInit();
    }
#endif

    _mainWindow->scheduleRenderJob(
        QRunnable::create([this] {
            QMetaObject::invokeMethod(this, &VideoManager2::_initAfterQmlIsReady, Qt::QueuedConnection);
        }),
        QQuickWindow::AfterSynchronizingStage);

    _initialized = true;
}

void VideoManager2::_initAfterQmlIsReady()
{
    if (!_mainWindow) {
        qCCritical(VideoManager2Log) << "_initAfterQmlIsReady called with NULL mainWindow";
        return;
    }

    qCDebug(VideoManager2Log) << "_initAfterQmlIsReady";

#ifdef QGC_GST_STREAMING
    switch (_initState) {
    case InitState::Pending:
        _initState = InitState::QmlReady;
        qCDebug(VideoManager2Log) << "QML ready, waiting for GStreamer";
        return;
    case InitState::GstReady:
        _initState = InitState::Running;
        qCDebug(VideoManager2Log) << "QML ready, GStreamer already done — creating receivers";
        break;
    case InitState::Failed:
        qCWarning(VideoManager2Log) << "QML ready but GStreamer init failed";
        return;
    default:
        qCWarning(VideoManager2Log) << "_initAfterQmlIsReady: unexpected state" << static_cast<int>(_initState);
        return;
    }
#endif
    _createVideoReceivers();
}

void VideoManager2::_onGstInitComplete(bool success)
{
    if (!success) {
        _initState = InitState::Failed;
        qCCritical(VideoManager2Log) << "GStreamer initialization failed";
        return;
    }

#ifdef QGC_GST_STREAMING
    // VideoManager handles codec priorities globally.
#endif

    switch (_initState) {
    case InitState::Pending:
        _initState = InitState::GstReady;
        qCDebug(VideoManager2Log) << "GStreamer ready, waiting for QML";
        return;
    case InitState::QmlReady:
        _initState = InitState::Running;
        qCDebug(VideoManager2Log) << "GStreamer ready, QML already done — creating receivers";
        _createVideoReceivers();
        return;
    default:
        qCWarning(VideoManager2Log) << "_onGstInitComplete: unexpected state" << static_cast<int>(_initState);
        return;
    }
}

void VideoManager2::_createVideoReceivers()
{
#ifdef QGC_UNITTEST_BUILD
    if (_createVideoReceiversForTest) {
        _createVideoReceiversForTest();
        return;
    }
#endif
    static const QStringList videoStreamList = {
        "videoContent2",
        "thermalVideo2"
    };
    for (const QString &streamName : videoStreamList) {
        VideoReceiver *receiver = QGCCorePlugin::instance()->createVideoReceiver(this);
        if (!receiver) {
            continue;
        }
        receiver->setName(streamName);

        _initVideoReceiver(receiver, _mainWindow);
    }
}

void VideoManager2::cleanup()
{
    for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
        QGCCorePlugin::instance()->releaseVideoSink(receiver->sink());
    }
}

void VideoManager2::_cleanupOldVideos()
{
    if (!SettingsManager::instance()->videoSettings2()->enableStorageLimit()->rawValue().toBool()) {
        return;
    }

    const QString savePath = SettingsManager::instance()->appSettings()->videoSavePath();
    QDir videoDir = QDir(savePath);
    videoDir.setFilter(QDir::Files | QDir::Readable | QDir::NoSymLinks | QDir::Writable);
    videoDir.setSorting(QDir::Time);

    QStringList nameFilters;
    for (size_t i = 0; i < std::size(kFileExtension); i++) {
        nameFilters << QStringLiteral("*.") + kFileExtension[i];
    }

    videoDir.setNameFilters(nameFilters);
    QFileInfoList vidList = videoDir.entryInfoList();
    if (vidList.isEmpty()) {
        return;
    }

    uint64_t total = 0;
    for (const QFileInfo &video : std::as_const(vidList)) {
        total += video.size();
    }

    const uint64_t maxSize = SettingsManager::instance()->videoSettings2()->maxVideoSize()->rawValue().toUInt() * qPow(1024, 2);
    while ((total >= maxSize) && !vidList.isEmpty()) {
        const QFileInfo info = vidList.takeLast();
        total -= info.size();
        const QString path = info.filePath();
        qCDebug(VideoManager2Log) << "Removing old video file:" << path;
        (void) QFile::remove(path);
    }
}

void VideoManager2::startRecording(const QString &videoFile)
{
    const VideoReceiver::FILE_FORMAT fileFormat = static_cast<VideoReceiver::FILE_FORMAT>(_videoSettings2->recordingFormat()->rawValue().toInt());
    if (!VideoReceiver::isValidFileFormat(fileFormat)) {
        QGC::showAppMessage(tr("Invalid video format defined."));
        return;
    }

    _cleanupOldVideos();

    const QString savePath = SettingsManager::instance()->appSettings()->videoSavePath();
    if (savePath.isEmpty()) {
        QGC::showAppMessage(tr("Unabled to record video. Video save path must be specified in Settings."));
        return;
    }

    const QString videoFileUrl = videoFile.isEmpty() ? QDateTime::currentDateTime().toString("yyyy-MM-dd_hh.mm.ss") : videoFile;
    const QString ext = kFileExtension[fileFormat];

    for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
        if (!receiver->started()) {
            qCDebug(VideoManager2Log) << "Video receiver is not ready.";
            continue;
        }
        const QString streamName = (receiver->name() == QStringLiteral("videoContent2")) ? "CAM2" : receiver->name();
        const QString videoFileName = savePath + "/" + videoFileUrl + (streamName.isEmpty() ? "" : "_" + streamName) + "." + ext;
        receiver->startRecording(videoFileName, fileFormat);
    }
}

void VideoManager2::stopRecording()
{
    for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
        receiver->stopRecording();
    }
}

void VideoManager2::sendViewproCommand(int commandId)
{
    if (!_videoSettings2) {
        return;
    }

    QString url;
    QString source = _videoSettings2->videoSource()->rawValue().toString();
    if (source == VideoSettings2::videoSourceRTSP) {
        url = _videoSettings2->rtspUrl()->rawValue().toString();
    } else if (source == VideoSettings2::videoSourceUDPH264 || source == VideoSettings2::videoSourceUDPH265) {
        url = _videoSettings2->udpUrl()->rawValue().toString();
    } else if (source == VideoSettings2::videoSourceTCP) {
        url = _videoSettings2->tcpUrl()->rawValue().toString();
    }

    if (url.isEmpty()) {
        qCDebug(VideoManager2Log) << "sendViewproCommand: No video URL configured.";
        return;
    }

    QRegularExpression re("(?:rtsp|tcp|udp)://([^:/]+)");
    QRegularExpressionMatch match = re.match(url);
    if (!match.hasMatch()) {
        qCDebug(VideoManager2Log) << "sendViewproCommand: Could not parse IP from URL" << url;
        return;
    }

    QString ip = match.captured(1);

    QByteArray payload;
    if (commandId == 0) {
        // Rec_stop
        payload = QByteArray::fromHex("EB901455AADC11300F000000000000000005500000007BFB");
    } else if (commandId == 1) {
        // Rec_start
        payload = QByteArray::fromHex("EB901455AADC11300F000000000000000005100000003B7B");
    } else if (commandId == 2) {
        // Photograph
        payload = QByteArray::fromHex("EB901455AADC11300F000000000000000004D0000000FAF9");
    } else {
        return;
    }

    qCDebug(VideoManager2Log) << "sendViewproCommand: Sending TCP command" << commandId << "to" << ip << ":2000";

    QTcpSocket* socket = new QTcpSocket(this);
    QTimer* disconnectTimer = new QTimer(socket);
    disconnectTimer->setSingleShot(true);

    connect(socket, &QTcpSocket::connected, socket, [socket, payload, disconnectTimer]() {
        socket->write(payload);
        socket->flush();
        // Allow up to 2500 ms for camera to process command and respond before disconnecting
        disconnectTimer->start(2500);
    });
    connect(disconnectTimer, &QTimer::timeout, socket, [socket]() {
        qCDebug(VideoManager2Log) << "sendViewproCommand: Disconnecting TCP socket after timeout";
        socket->disconnectFromHost();
    });
    connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
        QByteArray response = socket->readAll();
        qCDebug(VideoManager2Log) << "sendViewproCommand: Received response from ViewPro camera:" << response.toHex();
        socket->disconnectFromHost();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, socket, [socket](QAbstractSocket::SocketError error) {
        qCDebug(VideoManager2Log) << "sendViewproCommand TCP error:" << error;
        socket->deleteLater();
    });

    socket->connectToHost(ip, 2000);
}

void VideoManager2::grabImage(const QString &imageFile)
{
    if (imageFile.isEmpty()) {
        _imageFile = SettingsManager::instance()->appSettings()->photoSavePath();
        _imageFile += QStringLiteral("/") + QDateTime::currentDateTime().toString("yyyy-MM-dd_hh.mm.ss.zzz") + QStringLiteral("_CAM2.jpg");
    } else {
        _imageFile = imageFile;
    }

    emit imageFileChanged(_imageFile);

    for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
        receiver->takeScreenshot(_imageFile);
        // QSharedPointer<QQuickItemGrabResult> result = receiver->widget()->grabToImage(const QSize &targetSize = QSize())
    }
}

double VideoManager2::aspectRatio() const
{
    for (VideoReceiver *receiver : _videoReceivers) {
        QGCVideoStreamInfo *pInfo = receiver->videoStreamInfo();
        if (!receiver->isThermal() && pInfo && !pInfo->isThermal()) {
            return pInfo->aspectRatio();
        }
    }

    // FIXME: use _videoReceiver->videoSize() to calculate AR (if AR is not specified in the settings?)
    return _videoSettings2->aspectRatio()->rawValue().toDouble();
}

double VideoManager2::thermalAspectRatio() const
{
    for (VideoReceiver *receiver : _videoReceivers) {
        QGCVideoStreamInfo *pInfo = receiver->videoStreamInfo();
        if (receiver->isThermal() && pInfo && pInfo->isThermal()) {
            return pInfo->aspectRatio();
        }
    }

    return 1.0;
}

double VideoManager2::hfov() const
{
    for (VideoReceiver *receiver : _videoReceivers) {
        QGCVideoStreamInfo *pInfo = receiver->videoStreamInfo();
        if (!receiver->isThermal() && pInfo && !pInfo->isThermal()) {
            return pInfo->hfov();
        }
    }

    return 1.0;
}

double VideoManager2::thermalHfov() const
{
    for (VideoReceiver *receiver : _videoReceivers) {
        QGCVideoStreamInfo *pInfo = receiver->videoStreamInfo();
        if (receiver->isThermal() && pInfo && pInfo->isThermal()) {
            return pInfo->hfov();
        }
    }

    return _videoSettings2->aspectRatio()->rawValue().toDouble();
}

bool VideoManager2::hasThermal() const
{
    for (VideoReceiver *receiver : _videoReceivers) {
        QGCVideoStreamInfo *pInfo = receiver->videoStreamInfo();
        if (receiver->isThermal() && pInfo && pInfo->isThermal()) {
            return true;
        }
    }

    return false;
}

bool VideoManager2::hasVideo() const
{
    return (_videoSettings2->streamEnabled()->rawValue().toBool() && _videoSettings2->streamConfigured());
}

bool VideoManager2::isUvc() const
{
    return (!_uvcVideoSourceID.isEmpty() && uvcEnabled() && hasVideo());
}

bool VideoManager2::gstreamerEnabled()
{
#ifdef QGC_GST_STREAMING
    return true;
#else
    return false;
#endif
}

bool VideoManager2::uvcEnabled()
{
    return UVCReceiver::enabled();
}

bool VideoManager2::qtmultimediaEnabled()
{
    return QtMultimediaReceiver::enabled();
}

void VideoManager2::setfullScreen(bool on)
{
    if (on) {
        if (!_activeVehicle || _activeVehicle->vehicleLinkManager()->communicationLost()) {
            on = false;
        }
    }

    if (on != _fullScreen) {
        _fullScreen = on;
        emit fullScreenChanged();
    }
}

bool VideoManager2::isStreamSource() const
{
    static const QStringList videoSourceList = {
        VideoSettings2::videoSourceUDPH264,
        VideoSettings2::videoSourceUDPH265,
        VideoSettings2::videoSourceRTSP,
        VideoSettings2::videoSourceTCP,
        VideoSettings2::videoSourceMPEGTS,
        VideoSettings2::videoSource3DRSolo,
        VideoSettings2::videoSourceParrotDiscovery,
        VideoSettings2::videoSourceYuneecMantisG,
        VideoSettings2::videoSourceHerelinkAirUnit,
        VideoSettings2::videoSourceHerelinkHotspot,
    };
    const QString videoSource = _videoSettings2->videoSource()->rawValue().toString();
    return (videoSourceList.contains(videoSource) || autoStreamConfigured());
}

void VideoManager2::_videoSourceChanged()
{
    bool changed = false;
    if (_activeVehicle) {
        QGCCameraManager* camMgr = _activeVehicle->cameraManager();
        for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
            QGCVideoStreamInfo* info = nullptr;
            if (receiver->isThermal()) {
                info = camMgr ? camMgr->thermalStreamInstance() : nullptr;
            } else {
                info = camMgr ? camMgr->currentStreamInstance() : nullptr;
            }
            receiver->setVideoStreamInfo(info);
            changed |= _updateSettings(receiver);
        }
    } else {
        for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
            receiver->setVideoStreamInfo(nullptr);
            changed |= _updateSettings(receiver);
        }
    }

    if (changed) {
        emit hasVideoChanged();
        emit isStreamSourceChanged();
        emit isAutoStreamChanged();

        if (hasVideo()) {
            _restartAllVideos();
        } else {
            stopVideo();
        }

        qCDebug(VideoManager2Log) << "New Video Source:" << _videoSettings2->videoSource()->rawValue().toString();
    }
}

bool VideoManager2::_updateUVC(VideoReceiver * /*receiver*/)
{
    bool result = false;

    const QString oldUvcVideoSrcID = _uvcVideoSourceID;

    if (!uvcEnabled() || !hasVideo() || isStreamSource()) {
        _uvcVideoSourceID = QString();
    } else {
        _uvcVideoSourceID = UVCReceiver::getSourceId();
    }

    if (oldUvcVideoSrcID != _uvcVideoSourceID) {
        qCDebug(VideoManager2Log) << "UVC changed from [" << oldUvcVideoSrcID << "] to [" << _uvcVideoSourceID << "]";
        if (!_uvcVideoSourceID.isEmpty()) {
            UVCReceiver::checkPermission();
        }
        result = true;
        emit uvcVideoSourceIDChanged();
        emit isUvcChanged();
    }

    return result;
}

bool VideoManager2::autoStreamConfigured() const
{
    for (VideoReceiver *receiver : _videoReceivers) {
        QGCVideoStreamInfo *pInfo = receiver->videoStreamInfo();
        if (!receiver->isThermal() && pInfo && !pInfo->isThermal()) {
            return !pInfo->uri().isEmpty();
        }
    }

    return false;
}

bool VideoManager2::_updateAutoStream(VideoReceiver *receiver)
{
    const QGCVideoStreamInfo *pInfo = receiver->videoStreamInfo();
    if (!pInfo) {
        return false;
    }

    qCDebug(VideoManager2Log) << QString("Configure stream (%1):").arg(receiver->name()) << pInfo->uri();

    QString source, url;
    switch (pInfo->type()) {
    case VIDEO_STREAM_TYPE_RTSP:
        source = VideoSettings2::videoSourceRTSP;
        url = pInfo->uri();
        if (source == VideoSettings2::videoSourceRTSP) {
            _videoSettings2->rtspUrl()->setRawValue(url);
        }
        break;
    case VIDEO_STREAM_TYPE_TCP_MPEG:
        source = VideoSettings2::videoSourceTCP;
        url = pInfo->uri();
        break;
    case VIDEO_STREAM_TYPE_RTPUDP:
        if (pInfo->encoding() == VIDEO_STREAM_ENCODING_H265) {
            source = VideoSettings2::videoSourceUDPH265;
            url = pInfo->uri().contains("udp265://") ? pInfo->uri() : QStringLiteral("udp265://0.0.0.0:%1").arg(pInfo->uri());
        } else {
            source = VideoSettings2::videoSourceUDPH264;
            url = pInfo->uri().contains("udp://") ? pInfo->uri() : QStringLiteral("udp://0.0.0.0:%1").arg(pInfo->uri());
        }
        break;
    case VIDEO_STREAM_TYPE_MPEG_TS:
        source = VideoSettings2::videoSourceMPEGTS;
        url = pInfo->uri().contains("mpegts://") ? pInfo->uri() : QStringLiteral("mpegts://0.0.0.0:%1").arg(pInfo->uri());
        break;
    default:
        qCWarning(VideoManager2Log) << "Unknown VIDEO_STREAM_TYPE";
        source = VideoSettings2::videoSourceNoVideo;
        url = pInfo->uri();
        break;
    }

    const bool settingsChanged = _updateVideoUri(receiver, url);
    if (settingsChanged) {
        if (!receiver->isThermal()) {
            _videoSettings2->videoSource()->setRawValue(source);
        }

        emit autoStreamConfiguredChanged();
    }

    return settingsChanged;
}

bool VideoManager2::_updateVideoUri(VideoReceiver *receiver, const QString &uri)
{
    if (!receiver) {
        qCDebug(VideoManager2Log) << "VideoReceiver is NULL";
        return false;
    }

    if ((uri == receiver->uri()) && !receiver->uri().isNull()) {
        return false;
    }

    qCDebug(VideoManager2Log) << "New Video URI" << uri;

    receiver->setUri(uri);

    return true;
}

bool VideoManager2::_updateSettings(VideoReceiver *receiver)
{
    if (!receiver) {
        qCDebug(VideoManager2Log) << "VideoReceiver is NULL";
        return false;
    }

    bool settingsChanged = false;

    const bool lowLatency = _videoSettings2->lowLatencyMode()->rawValue().toBool();
    if (lowLatency != receiver->lowLatency()) {
        receiver->setLowLatency(lowLatency);
        settingsChanged = true;
    }

    if (receiver->isThermal()) {
        return settingsChanged;
    }

    settingsChanged |= _updateUVC(receiver);
    settingsChanged |= _updateAutoStream(receiver);

    const QString source = _videoSettings2->videoSource()->rawValue().toString();
    if (source == VideoSettings2::videoSourceUDPH264) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("udp://%1").arg(_videoSettings2->udpUrl()->rawValue().toString()));
    } else if (source == VideoSettings2::videoSourceUDPH265) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("udp265://%1").arg(_videoSettings2->udpUrl()->rawValue().toString()));
    } else if (source == VideoSettings2::videoSourceMPEGTS) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("mpegts://%1").arg(_videoSettings2->udpUrl()->rawValue().toString()));
    } else if (source == VideoSettings2::videoSourceRTSP) {
        settingsChanged |= _updateVideoUri(receiver, _videoSettings2->rtspUrl()->rawValue().toString());
    } else if (source == VideoSettings2::videoSourceTCP) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("tcp://%1").arg(_videoSettings2->tcpUrl()->rawValue().toString()));
    } else if (source == VideoSettings2::videoSource3DRSolo) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("udp://0.0.0.0:5600"));
    } else if (source == VideoSettings2::videoSourceParrotDiscovery) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("udp://0.0.0.0:8888"));
    } else if (source == VideoSettings2::videoSourceYuneecMantisG) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("rtsp://192.168.42.1:554/live"));
    } else if (source == VideoSettings2::videoSourceHerelinkAirUnit) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("rtsp://192.168.0.10:8554/H264Video"));
    } else if (source == VideoSettings2::videoSourceHerelinkHotspot) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("rtsp://192.168.43.1:8554/fpv_stream"));
    } else if ((source == VideoSettings2::videoDisabled) || (source == VideoSettings2::videoSourceNoVideo)) {
        settingsChanged |= _updateVideoUri(receiver, QString());
    } else {
        settingsChanged |= _updateVideoUri(receiver, QString());
        if (!isUvc()) {
            qCCritical(VideoManager2Log) << "Video source URI \"" << source << "\" is not supported. Please add support!";
        }
    }

    return settingsChanged;
}

void VideoManager2::_setActiveVehicle(Vehicle *vehicle)
{
    qCDebug(VideoManager2Log) << Q_FUNC_INFO << "new vehicle" << vehicle << "old active vehicle" << _activeVehicle;

    if (_activeVehicle) {
        (void) disconnect(_activeVehicle->vehicleLinkManager(), &VehicleLinkManager::communicationLostChanged, this, &VideoManager2::_communicationLostChanged);
        auto cameraManager = _activeVehicle->cameraManager();
        if (cameraManager) {
            MavlinkCameraControlInterface *pCamera = cameraManager->currentCameraInstance();
            if (pCamera) {
                pCamera->stopStream();
            }
            (void) disconnect(cameraManager, &QGCCameraManager::streamChanged, this, &VideoManager2::_videoSourceChanged);
        }

        for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
            // disconnect(receiver->videoStreamInfo(), &QGCVideoStreamInfo::infoChanged, ))
            receiver->setVideoStreamInfo(nullptr);
        }
    }

    _activeVehicle = vehicle;
    if (_activeVehicle) {
        (void) connect(_activeVehicle->vehicleLinkManager(), &VehicleLinkManager::communicationLostChanged, this, &VideoManager2::_communicationLostChanged);
        if (_activeVehicle->cameraManager()) {
            (void) connect(_activeVehicle->cameraManager(), &QGCCameraManager::streamChanged, this, &VideoManager2::_videoSourceChanged);
            MavlinkCameraControlInterface *pCamera = _activeVehicle->cameraManager()->currentCameraInstance();
            if (pCamera) {
                pCamera->resumeStream();
            }
        }

        for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
            // VideoManager2 is purely manual or uses a secondary camera.
            // Do not bind it to currentStreamInstance() to avoid conflicting with VideoManager.
            receiver->setVideoStreamInfo(nullptr);
            // connect(receiver->videoStreamInfo(), &QGCVideoStreamInfo::infoChanged, ))
        }
    } else {
        setfullScreen(false);
    }
}

void VideoManager2::_communicationLostChanged(bool connectionLost)
{
    if (connectionLost) {
        setfullScreen(false);
    }
}

void VideoManager2::_restartAllVideos()
{
    for (VideoReceiver *videoReceiver : std::as_const(_videoReceivers)) {
        _restartVideo(videoReceiver);
    }
}

void VideoManager2::_restartVideo(VideoReceiver *receiver)
{
    if (!receiver) {
        qCDebug(VideoManager2Log) << "VideoReceiver is NULL";
        return;
    }

    qCDebug(VideoManager2Log) << "Restart video receiver" << receiver->name();

    if (receiver->started()) {
        _stopReceiver(receiver);
        // onStopComplete Signal Will Restart It
    } else {
        _startReceiver(receiver);
    }
}

void VideoManager2::_stopReceiver(VideoReceiver *receiver)
{
    if (!receiver) {
        qCDebug(VideoManager2Log) << "VideoReceiver is NULL";
        return;
    }

    if (receiver->started()) {
        receiver->stop();
    }
}

void VideoManager2::stopVideo()
{
    for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
        _stopReceiver(receiver);
    }
}

void VideoManager2::_startReceiver(VideoReceiver *receiver)
{
    if (!receiver) {
        qCDebug(VideoManager2Log) << "VideoReceiver is NULL";
        return;
    }

    if (receiver->started()) {
        qCDebug(VideoManager2Log) << "VideoReceiver is already started" << receiver->name();
        return;
    }

    if (receiver->uri().isEmpty()) {
        qCDebug(VideoManager2Log) << "VideoUri is NULL" << receiver->name();
        return;
    }

    const QString source = _videoSettings2->videoSource()->rawValue().toString();
    /* The gstreamer rtsp source will switch to tcp if udp is not available after 5 seconds.
       So we should allow for some negotiation time for rtsp */

    const uint32_t timeout = ((source == VideoSettings2::videoSourceRTSP) ? _videoSettings2->rtspTimeout()->rawValue().toUInt() : 3);

    receiver->start(timeout);
}

void VideoManager2::_initVideoReceiver(VideoReceiver *receiver, QQuickWindow *window)
{
    if (_videoReceivers.contains(receiver)) {
        qCWarning(VideoManager2Log) << "Receiver already initialized";
    }

    QQuickItem *widget = window->findChild<QQuickItem*>(receiver->name());
    if (!widget) {
        qCCritical(VideoManager2Log) << "stream widget not found" << receiver->name();
    }
    receiver->setWidget(widget);

    void *sink = QGCCorePlugin::instance()->createVideoSink(receiver->widget(), receiver);
    if (!sink) {
        qCCritical(VideoManager2Log) << "createVideoSink() failed" << receiver->name();
    }
    receiver->setSink(sink);

#ifdef QGC_GST_STREAMING
    if (sink && widget) {
        auto *videoOutput = qobject_cast<QQuickVideoOutput *>(widget);
        if (videoOutput) {
            QVideoSink *videoSink = videoOutput->videoSink();
            if (!GStreamer::setupAppSinkAdapter(sink, videoSink, receiver)) {
                qCWarning(VideoManager2Log) << "setupAppSinkAdapter failed" << receiver->name();
            }
            // Visibility gate: drop frames at the appsink while the host window is hidden
            // or minimized. The decoder still runs (cheap with HW accel) but render-thread
            // and copy work disappears. Connector handles late window attachment via
            // QQuickItem::windowChanged.
            auto applyVisibility = [receiver](QWindow *win) {
                if (!win) return;
                const QWindow::Visibility v = win->visibility();
                const bool active = (v != QWindow::Hidden && v != QWindow::Minimized);
                GStreamer::setAppSinkAdaptersActive(receiver, active);
            };
            // Track the previous connection so windowChanged can drop it before wiring the
            // new window. Without this, an old hidden/minimized window keeps gating the
            // live receiver after the video output reparents to a new window.
            auto prevConn = std::make_shared<QMetaObject::Connection>();
            auto wireWindow = [receiver, applyVisibility, prevConn](QQuickWindow *qw) {
                if (*prevConn) {
                    QObject::disconnect(*prevConn);
                    *prevConn = QMetaObject::Connection{};
                }
                if (!qw) return;
                applyVisibility(qw);
                *prevConn = QObject::connect(qw, &QWindow::visibilityChanged, receiver,
                    [applyVisibility, qw](QWindow::Visibility) { applyVisibility(qw); });
            };
            if (QQuickWindow *qw = videoOutput->window()) wireWindow(qw);
            QObject::connect(videoOutput, &QQuickVideoOutput::windowChanged, receiver, wireWindow);
        } else {
            qCWarning(VideoManager2Log) << "Widget is not a VideoOutput, cannot connect appsink" << receiver->name();
        }
    }
#endif

    (void) connect(receiver, &VideoReceiver::onStartComplete, this, [this, receiver](VideoReceiver::STATUS status) {
        qCDebug(VideoManager2Log) << "Video" << receiver->name() << "Start complete, status:" << status;
        switch (status) {
        case VideoReceiver::STATUS_OK:
            receiver->setStarted(true);
            if (receiver->sink()) {
                receiver->startDecoding(receiver->sink());
            }
            break;
        case VideoReceiver::STATUS_INVALID_URL:
        case VideoReceiver::STATUS_INVALID_STATE:
            break;
        default:
            _restartVideo(receiver);
            break;
        }
    });

    (void) connect(receiver, &VideoReceiver::onStopComplete, this, [this, receiver](VideoReceiver::STATUS status) {
        qCDebug(VideoManager2Log) << "Stop complete" << receiver->name() << receiver->uri()  << ", status:" << status;
        receiver->setStarted(false);
        if (status == VideoReceiver::STATUS_INVALID_URL) {
            qCDebug(VideoManager2Log) << "Invalid video URL. Not restarting";
        } else {
            QTimer::singleShot(1000, receiver, [this, receiver]() {
                qCDebug(VideoManager2Log) << "Restarting video receiver" << receiver->name() << receiver->uri();
                _startReceiver(receiver);
            });
        }
    });

    (void) connect(receiver, &VideoReceiver::streamingChanged, this, [this, receiver](bool active) {
        qCDebug(VideoManager2Log) << "Video" << receiver->name() << "streaming changed, active:" << (active ? "yes" : "no");
        if (!receiver->isThermal()) {
            _streaming = active;
            emit streamingChanged();
        }
    });

    (void) connect(receiver, &VideoReceiver::decodingChanged, this, [this, receiver](bool active) {
        qCDebug(VideoManager2Log) << "Video" << receiver->name() << "decoding changed, active:" << (active ? "yes" : "no");
        if (!receiver->isThermal()) {
            _decoding = active;
            emit decodingChanged();
        }
    });

    (void) connect(receiver, &VideoReceiver::recordingChanged, this, [this, receiver](bool active) {
        qCDebug(VideoManager2Log) << "Video" << receiver->name() << "recording changed, active:" << (active ? "yes" : "no");
        if (!receiver->isThermal()) {
            _recording = active;
            if (!active) {
                _subtitleWriter->stopCapturingTelemetry();
            }
            emit recordingChanged(_recording);
        }
    });

    (void) connect(receiver, &VideoReceiver::recordingStarted, this, [this, receiver](const QString &filename) {
        qCDebug(VideoManager2Log) << "Video" << receiver->name() << "recording started";
        if (!receiver->isThermal()) {
            _subtitleWriter->startCapturingTelemetry(filename, videoSize());
        }
    });

    (void) connect(receiver, &VideoReceiver::videoSizeChanged, this, [this, receiver](QSize size) {
        qCDebug(VideoManager2Log) << "Video" << receiver->name() << "resized. New resolution:" << size.width() << "x" << size.height();
        if (!receiver->isThermal()) {
            _videoSize = size;
            emit videoSizeChanged();
        }
    });

    (void) connect(receiver, &VideoReceiver::onTakeScreenshotComplete, this, [receiver](VideoReceiver::STATUS status) {
        if (status == VideoReceiver::STATUS_OK) {
            qCDebug(VideoManager2Log) << "Video" << receiver->name() << "screenshot taken";
        } else {
            qCWarning(VideoManager2Log) << "Video" << receiver->name() << "screenshot failed";
        }
    });

    (void) connect(receiver, &VideoReceiver::videoStreamInfoChanged, this, [this, receiver]() {
        const QGCVideoStreamInfo *videoStreamInfo = receiver->videoStreamInfo();
        qCDebug(VideoManager2Log) << "Video" << receiver->name() << "stream info:" << (videoStreamInfo ? "received" : "lost");

        (void) _updateAutoStream(receiver);
    });

    (void) _updateSettings(receiver);

    _videoReceivers.append(receiver);

    if (hasVideo()) {
        _startReceiver(receiver);
    }
}

void VideoManager2::startVideo()
{
    qCDebug(VideoManager2Log) << "startVideo";

    if (!hasVideo()) {
        qCDebug(VideoManager2Log) << "Stream not enabled/configured";
        return;
    }

    _restartAllVideos();
}
