#include "JsonParsing.h"
#include "QGCApplication.h"
#include "qgc_version.h"

#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QMetaMethod>
#include <QtCore/QMetaObject>
#include <QtCore/QRegularExpression>
#ifdef QGC_ENABLE_QML
#include <QtGui/QFontDatabase>
#include <QtGui/QIcon>
#endif
#include <QtNetwork/QHostAddress>
#include "QGCNetworkHelper.h"
#ifdef QGC_ENABLE_QML
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickImageProvider>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtSvg/QSvgRenderer>
#endif

#include <QtCore/private/qthread_p.h>

#include "LogManager.h"
#include "AudioOutput.h"
#include "FollowMe.h"
#include "JoystickManager.h"
#include "LinkManager.h"
#include "MAVLinkProtocol.h"
#include "MultiVehicleManager.h"
#include "ParameterManager.h"
#include "PositionManager.h"
#include "QGCCommandLineParser.h"
#include "QGCCorePlugin.h"
#ifdef QGC_ENABLE_QT_NETWORK
#include "QGCFileDownload.h"
#endif
#ifdef QGC_ENABLE_QML
#include "ColoredSvgImageProvider.h"
#include "QGCImageProvider.h"
#endif
#include "QGCLoggingCategory.h"
#include "QGCLoggingCategoryManager.h"
#include "SettingsManager.h"
#include "MavlinkSettings.h"
#include "AppSettings.h"
#include "CommandChannel.h"
#include "FactChannel.h"
#include "ImageChannel.h"
#include "MissionChannel.h"
#include "NotificationChannel.h"
#include "SettingsChannel.h"
#include "TelemetryChannel.h"

#include "UDPLink.h"
#include "Vehicle.h"
#include "VehicleComponent.h"
#include "VideoManager.h"
#include "VideoManager2.h"
#include "WebBridge.h"
#include "WebBridgeServer.h"

#ifdef QGC_GST_STREAMING
#include "GhostVideoSource.h"
#include "VideoStreamServer.h"
#endif

#ifdef QGC_ENABLE_MOCKLINK
#include "MockLink.h"
#endif

#ifndef QGC_NO_SERIAL_LINK
#include "SerialLink.h"
#endif

QGC_LOGGING_CATEGORY(QGCApplicationLog, "API.QGCApplication")

QGCApplication::QGCApplication(int &argc, char *argv[], const QGCCommandLineParser::CommandLineParseResult &cli)
    : QGCApplicationBase(argc, argv)
    , _runningUnitTests(cli.runningUnitTests)
    , _simpleBootTest(cli.simpleBootTest)
    , _headless(cli.headless)
    , _bridgePort(static_cast<quint16>(cli.bridgePort))
    , _bridgeHost(cli.bridgeHost.value_or(QString()))
    , _bridgeToken(cli.bridgeToken.value_or(QString()))
    , _mockLink(cli.mockLink)
    , _fakeMobile(cli.fakeMobile)
    , _logOutput(cli.logOutput)
    , _systemId(cli.systemId.value_or(0))
{
    _msecsElapsedTime.start();

    // Setup for network proxy support
    QGCNetworkHelper::initializeProxySupport();

    bool fClearSettingsOptions = cli.clearSettingsOptions;  // Clear stored settings
    const bool fClearCache = cli.clearCache;                // Clear parameter/airframe caches
    const QString loggingOptions = cli.loggingOptions.value_or(QString(""));

    // Set up timer for delayed missing fact display
    _missingParamsDelayedDisplayTimer.setSingleShot(true);
    _missingParamsDelayedDisplayTimer.setInterval(_missingParamsDelayedDisplayTimerTimeout);
    (void) connect(&_missingParamsDelayedDisplayTimer, &QTimer::timeout, this, &QGCApplication::_missingParamsDisplay);

    // Set application information
    QString applicationName;
    if (_runningUnitTests || _simpleBootTest) {
        // We don't want unit tests to use the same QSettings space as the normal app. So we tweak the app
        // name. Also we want to run unit tests with clean settings every time.
        // Include test name or PID to prevent settings file conflicts when tests run in parallel
        if (!cli.unitTests.isEmpty()) {
            applicationName = QStringLiteral("%1_unittest_%2").arg(QGC_APP_NAME, cli.unitTests.first());
        } else {
            applicationName = QStringLiteral("%1_unittest_%2").arg(QGC_APP_NAME).arg(QCoreApplication::applicationPid());
        }
    } else {
#ifdef QGC_DAILY_BUILD
        // This gives daily builds their own separate settings space. Allowing you to use daily and stable builds
        // side by side without daily screwing up your stable settings.
        applicationName = QStringLiteral("%1 Daily").arg(QGC_APP_NAME);
#else
        applicationName = QGC_APP_NAME;
#endif
    }
    setApplicationName(applicationName);
#ifdef QGC_ENABLE_QML
    // QGuiApplication-only: associates the window with its .desktop file for window
    // manager/taskbar integration. Meaningless without a window.
    setDesktopFileName(QGC_PACKAGE_NAME);
#endif
    setOrganizationName(QGC_ORG_NAME);
    setOrganizationDomain(QGC_ORG_DOMAIN);
    setApplicationVersion(QString(QGC_APP_VERSION_STR));

    // Set settings format
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings settings;
    qCDebug(QGCApplicationLog) << "Settings location" << settings.fileName() << "Is writable?:" << settings.isWritable();

    if (!settings.isWritable()) {
        qCWarning(QGCApplicationLog) << "Setings location is not writable";
    }

    // The setting will delete all settings on this boot
    fClearSettingsOptions |= settings.value(AppSettings::clearSettingsNextBootKey, false).toBool();

    if (_runningUnitTests || _simpleBootTest) {
        // Unit tests run with clean settings
        fClearSettingsOptions = true;
    }

    if (fClearSettingsOptions) {
        // User requested settings to be cleared on command line
        settings.clear();

        // Clear parameter cache
        QDir paramDir(ParameterManager::parameterCacheDir());
        paramDir.removeRecursively();
        paramDir.mkpath(paramDir.absolutePath());
    } else {
        // Determine if upgrade message for settings version bump is required. Check and clear must happen before toolbox is started since
        // that will write some settings.
        if (settings.contains(_settingsVersionKey)) {
            if (settings.value(_settingsVersionKey).toInt() != QGC_SETTINGS_VERSION) {
                settings.clear();
                _settingsUpgraded = true;
            }
        }
    }
    settings.setValue(_settingsVersionKey, QGC_SETTINGS_VERSION);

    if (fClearCache) {
        QDir dir(ParameterManager::parameterCacheDir());
        dir.removeRecursively();
        QFile parameter(cachedParameterMetaDataFile());
        parameter.remove();
        QFile airframe(cachedAirframeMetaDataFile());
        airframe.remove();

        // Clear versioned parameter metadata cache
        const QString metaDataCachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                          + QStringLiteral("/ParameterMetaData");
        QDir(metaDataCachePath).removeRecursively();
    }

    // Set up our logging filters
    QGCLoggingCategoryManager::init();
    QGCLoggingCategoryManager::instance()->installFilter(loggingOptions);

    // We need to set language as early as possible prior to loading on JSON files.
    setLanguage();

#ifdef QGC_ENABLE_QML
    // Force old SVG Tiny 1.2 behavior for compatibility
    QSvgRenderer::setDefaultOptions(QtSvg::Tiny12FeaturesOnly);
#endif

#if !defined(QGC_DAILY_BUILD) && !defined(QGC_DISABLE_NEW_VERSION_CHECK)
    // _checkForNewVersion();
#endif
}

void QGCApplication::setLanguage()
{
    _locale = QLocale::system();
    qCDebug(QGCApplicationLog) << "System reported locale:" << _locale << "; Name" << _locale.name() << "; Preffered (used in maps): " << (QLocale::system().uiLanguages().length() > 0 ? QLocale::system().uiLanguages()[0] : "None");

    QLocale::Language possibleLocale = AppSettings::_qLocaleLanguageEarlyAccess();
    if (possibleLocale != QLocale::AnyLanguage) {
        _locale = QLocale(possibleLocale);
    }
    //-- We have specific fonts for Korean
#ifdef QGC_ENABLE_QML
    if (_locale == QLocale::Korean) {
        qCDebug(QGCApplicationLog) << "Loading Korean fonts" << _locale.name();
        if(QFontDatabase::addApplicationFont(":/fonts/NanumGothic-Regular") < 0) {
            qCWarning(QGCApplicationLog) << "Could not load /fonts/NanumGothic-Regular font";
        }
        if(QFontDatabase::addApplicationFont(":/fonts/NanumGothic-Bold") < 0) {
            qCWarning(QGCApplicationLog) << "Could not load /fonts/NanumGothic-Bold font";
        }
    }
#endif
    qCDebug(QGCApplicationLog) << "Loading localizations for" << _locale.name();
    removeTranslator(JsonParsing::translator());
    removeTranslator(&_qgcTranslatorSourceCode);
    removeTranslator(&_qgcTranslatorQtLibs);
    if (_locale.name() != "en_US") {
        QLocale::setDefault(_locale);
        if (_qgcTranslatorQtLibs.load("qt_" + _locale.name(), QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
            installTranslator(&_qgcTranslatorQtLibs);
        } else {
            qCWarning(QGCApplicationLog) << "Qt lib localization for" << _locale.name() << "is not present";
        }
        if (_qgcTranslatorSourceCode.load(_locale, QLatin1String("qgc_source_"), "", ":/i18n")) {
            installTranslator(&_qgcTranslatorSourceCode);
        } else {
            qCWarning(QGCApplicationLog) << "Error loading source localization for" << _locale.name();
        }
        if (JsonParsing::translator()->load(_locale, QLatin1String("qgc_json_"), "", ":/i18n")) {
            installTranslator(JsonParsing::translator());
        } else {
            qCWarning(QGCApplicationLog) << "Error loading json localization for" << _locale.name();
        }
    }

#ifdef QGC_ENABLE_QML
    if (_qmlAppEngine) {
        _qmlAppEngine->retranslate();
    }
#endif

    emit languageChanged(_locale);
}

QGCApplication::~QGCApplication()
{

}

void QGCApplication::init()
{
    SettingsManager::instance()->init();
    if (_systemId > 0) {
        qCDebug(QGCApplicationLog) << "Setting MAVLink System ID to:" << _systemId;
        SettingsManager::instance()->mavlinkSettings()->gcsMavlinkSystemID()->setRawValue(_systemId);
    }

    LogManager::instance()->init();

    // Although this should really be in _initForNormalAppBoot putting it here allowws us to create unit tests which pop up more easily
#ifdef QGC_ENABLE_QML
    if (QFontDatabase::addApplicationFont(":/fonts/opensans") < 0) {
        qCWarning(QGCApplicationLog) << "Could not load /fonts/opensans font";
    }

    if (QFontDatabase::addApplicationFont(":/fonts/opensans-demibold") < 0) {
        qCWarning(QGCApplicationLog) << "Could not load /fonts/opensans-demibold font";
    }
#endif

    if (_simpleBootTest) {
        // Since GStream builds are so problematic we initialize video during the simple boot test
        // to make sure it works and verfies plugin availability.
        _bootTestPassed = _initVideo();
    } else if (_runningUnitTests) {
        // Unit test framework handles the remaining initialization
    } else if (_headless) {
        _initForHeadlessBoot();
    } else {
#ifdef QGC_ENABLE_QML
        _initForNormalAppBoot();
#else
        qCWarning(QGCApplicationLog) << "This build has no QML UI (QGC_ENABLE_QML=OFF). Falling back to headless boot.";
        _headless = true;
        _initForHeadlessBoot();
#endif
    }
}

bool QGCApplication::_initVideo()
{
#ifdef QGC_GST_STREAMING
    qCDebug(QGCApplicationLog) << "Using default graphics API for appsink → VideoOutput video path";
#endif

    QGCCorePlugin::instance();  // CorePlugin must be initialized before VideoManager for Video Cleanup
    VideoManager *videoManager = VideoManager::instance();
    videoManager->startGStreamerInit();
    const bool initSucceeded = !_simpleBootTest || videoManager->waitForGStreamerInit();
    VideoManager2 *videoManager2 = VideoManager2::instance();
    videoManager2->startGStreamerInit();
    videoManager2->waitForGStreamerInit();
    _videoManagerInitialized = true;
    return initSucceeded;
}

#ifdef QGC_ENABLE_QML
void QGCApplication::_initForNormalAppBoot()
{
    (void) _initVideo();

    QQuickStyle::setStyle("Basic");
    QGCCorePlugin::instance()->init();
    MAVLinkProtocol::instance()->init();
    MultiVehicleManager::instance()->init();
    _qmlAppEngine = QGCCorePlugin::instance()->createQmlApplicationEngine(this);
    QObject::connect(_qmlAppEngine, &QQmlApplicationEngine::objectCreationFailed, this, QCoreApplication::quit, Qt::QueuedConnection);

    // Must register before createRootWindow — root QML references QGCColoredImage which resolves image://coloredsvg/... at load time.
    _qmlAppEngine->addImageProvider(_qgcImageProviderId, new QGCImageProvider());
    _qmlAppEngine->addImageProvider(QLatin1String(ColoredSvgImageProvider::ProviderId), new ColoredSvgImageProvider());

    QGCCorePlugin::instance()->createRootWindow(_qmlAppEngine);

    AudioOutput::instance()->init(SettingsManager::instance()->appSettings()->audioVolume(), SettingsManager::instance()->appSettings()->audioMuted());
    FollowMe::instance()->init();
    QGCPositionManager::instance()->init();
    LinkManager::instance()->init();
    VideoManager::instance()->init(mainRootWindow());
    VideoManager2::instance()->init(mainRootWindow());

    // Set the window icon now that custom plugin has a chance to override it
#ifdef Q_OS_LINUX
    QUrl windowIcon = QUrl("qrc:/res/qgroundcontrol.ico");
    windowIcon = _qmlAppEngine->interceptUrl(windowIcon, QQmlAbstractUrlInterceptor::UrlString);
    // The interceptor needs "qrc:/path" but QIcon expects ":/path"
    setWindowIcon(QIcon(":" + windowIcon.path()));
#endif

    // Safe to show popup error messages now that main window is created
    _showErrorsInToolbar = true;

    #ifdef Q_OS_LINUX
    #ifndef Q_OS_ANDROID
    #ifndef QGC_NO_SERIAL_LINK
        if (!_runningUnitTests) {
            // Determine if we have the correct permissions to access USB serial devices
            QFile permFile("/etc/group");
            if(permFile.open(QIODevice::ReadOnly)) {
                while(!permFile.atEnd()) {
                    const QString line = permFile.readLine();
                    if (line.contains("dialout") && !line.contains(getenv("USER"))) {
                        permFile.close();
                        showAppMessage(tr(
                            "The current user does not have the correct permissions to access serial devices. "
                            "You should also remove modemmanager since it also interferes.<br/><br/>"
                            "If you are using Ubuntu, execute the following commands to fix these issues:<br/>"
                            "<pre>sudo usermod -a -G dialout $USER<br/>"
                            "sudo apt-get remove modemmanager</pre>"));
                        break;
                    }
                }
                permFile.close();
            }
        }
    #endif
    #endif
    #endif

    // Now that main window is up check for lost log files
    MAVLinkProtocol::instance()->checkForLostLogFiles();

    // Load known link configurations
    LinkManager::instance()->loadLinkConfigurationList();

    // Probe for joysticks
    JoystickManager::instance()->init();

    if (_settingsUpgraded) {
        showAppMessage(tr("The format for %1 saved settings has been modified. "
                    "Your saved settings have been reset to defaults.").arg(applicationName()));
    }

    // Connect links with flag AutoconnectLink
    LinkManager::instance()->startAutoConnectedLinks();
}
#endif // QGC_ENABLE_QML

void QGCApplication::_initForHeadlessBoot()
{
    (void) _initVideo();

    QGCCorePlugin::instance()->init();
    MAVLinkProtocol::instance()->init();
    MultiVehicleManager::instance()->init();

    AudioOutput::instance()->init(SettingsManager::instance()->appSettings()->audioVolume(), SettingsManager::instance()->appSettings()->audioMuted());
    FollowMe::instance()->init();
    QGCPositionManager::instance()->init();
    LinkManager::instance()->init();

    // VideoManager/VideoManager2 full init() is skipped entirely: it refuses a null window with a
    // critical error and needs the root QQuickWindow to schedule render jobs. GStreamer itself is
    // still initialized by _initVideo() above so shutdown() cleanup stays balanced.

    // _showErrorsInToolbar stays false: with no root window showAppMessage/showCriticalVehicleMessage
    // route to the log instead of UI dialogs.

    // Check for lost log files
    MAVLinkProtocol::instance()->checkForLostLogFiles();

    // Load known link configurations
    LinkManager::instance()->loadLinkConfigurationList();

    // Probe for joysticks
    JoystickManager::instance()->init();

    if (_settingsUpgraded) {
        showAppMessage(tr("The format for %1 saved settings has been modified. "
                    "Your saved settings have been reset to defaults.").arg(applicationName()));
    }

    // Web bridge: only when explicitly requested via --bridge-port.
    if (_bridgePort != 0) {
        _webBridge = new WebBridge(_bridgePort, this);
        _webBridgeServer = new WebBridgeServer(_webBridge, this);
        if (!_bridgeHost.isEmpty()) {
            _webBridgeServer->setListenAddress(QHostAddress(_bridgeHost));
        }
        if (!_bridgeToken.isEmpty()) {
            _webBridgeServer->setAuthToken(_bridgeToken);
        }
        _telemetryChannel = new TelemetryChannel(_webBridge, this);
        _factChannel = new FactChannel(this);
        _commandChannel = new CommandChannel(_webBridge, this);
        _missionChannel = new MissionChannel(_webBridge, this);
        _notificationChannel = new NotificationChannel(_webBridge, this);
        _imageChannel = new ImageChannel(_webBridge, this);
        _settingsChannel = new SettingsChannel(_webBridge, this);
        connect(_webBridgeServer, &WebBridgeServer::snapshotRequested, _telemetryChannel, &TelemetryChannel::sendSnapshot);
        connect(_webBridgeServer, &WebBridgeServer::commandReceived, _commandChannel, &CommandChannel::handleCommand);
        connect(_commandChannel, &CommandChannel::responseReady, _webBridgeServer, &WebBridgeServer::sendToClient);
        connect(_webBridgeServer, &WebBridgeServer::snapshotRequested, _missionChannel, &MissionChannel::sendSnapshot);
        connect(_webBridgeServer, &WebBridgeServer::missionMessageReceived, _missionChannel, &MissionChannel::handleMissionMessage);
        connect(_missionChannel, &MissionChannel::missionStateReady, _webBridgeServer, &WebBridgeServer::broadcast);
        connect(_missionChannel, &MissionChannel::responseReady, _webBridgeServer, &WebBridgeServer::sendToClient);
        connect(_telemetryChannel, &TelemetryChannel::telemetryReady, _webBridgeServer, &WebBridgeServer::broadcast);
        // §15 image channel (Q8d): same snapshot/broadcast wiring as telemetry/mission above.
        connect(_webBridgeServer, &WebBridgeServer::snapshotRequested, _imageChannel, &ImageChannel::sendSnapshot);
        connect(_imageChannel, &ImageChannel::imageReady, _webBridgeServer, &WebBridgeServer::broadcast);
        connect(_webBridgeServer, &WebBridgeServer::factMessageReceived, _factChannel, &FactChannel::handleMessage);
        connect(_factChannel, &FactChannel::responseReady, _webBridgeServer, &WebBridgeServer::reply);
        connect(_factChannel, &FactChannel::errorReady, _webBridgeServer, &WebBridgeServer::replyError);

        // §16 settings + link management: request/response like fact (§6) above, plus one
        // unsubscribed broadcast (settingChanged, §16.5) fanned out the same way notification
        // (§14) is below.
        connect(_webBridgeServer, &WebBridgeServer::settingsMessageReceived, _settingsChannel, &SettingsChannel::handleMessage);
        connect(_settingsChannel, &SettingsChannel::responseReady, _webBridgeServer, &WebBridgeServer::reply);
        connect(_settingsChannel, &SettingsChannel::errorReady, _webBridgeServer, &WebBridgeServer::replyError);
        connect(_settingsChannel, &SettingsChannel::settingChangedReady, _webBridgeServer, &WebBridgeServer::broadcastAll);

        // Notification (§14): sourced from signals this class itself emits (below) plus
        // AudioOutput::textAnnounced() (connected inside NotificationChannel's own constructor,
        // since AudioOutput has no QGCApplication dependency to avoid). Wired here, not inside
        // NotificationChannel, so that class never needs to include QGCApplication.h (it would
        // otherwise pull in QtGui just for these two signal declarations).
        connect(this, &QGCApplication::criticalVehicleMessageAnnounced, _notificationChannel, &NotificationChannel::handleCriticalVehicleMessage);
        connect(this, &QGCApplication::appMessageAnnounced, _notificationChannel, &NotificationChannel::handleAppMessage);
        // Broadcast stream (§14): no subscription, every authenticated client -- same fan-out
        // shape as WebBridge::tickReady() (§11.1).
        connect(_notificationChannel, &NotificationChannel::notificationReady, _webBridgeServer, &WebBridgeServer::broadcastAll);
        // One-shot per-connection "ghost bridge ready" (§14.3): targeted, not broadcast, so it is
        // reliably observed by a client connecting well after bridge startup (broadcastAll() at
        // construction time would reach nobody yet).
        connect(_webBridgeServer, &WebBridgeServer::clientAuthenticated, _notificationChannel, &NotificationChannel::sendWelcome);
        connect(_notificationChannel, &NotificationChannel::welcomeReady, _webBridgeServer, &WebBridgeServer::sendToClient);

#ifdef QGC_GST_STREAMING
        // Video: taps a live GStreamer pipeline's shared tee and relays H.264 access units plus
        // codec config over the bridge's `video` channel (PROTOCOL.md §9). streamId 1 matches
        // the protocol's current single-stream v0.1 scope.
        _videoStreamServer = new VideoStreamServer(1, this);
        connect(_videoStreamServer, &VideoStreamServer::frameReady, _webBridgeServer,
                [this](quint8 streamId, const QByteArray &frame) {
                    _webBridgeServer->broadcastBinary(QStringLiteral("video"), streamId, frame);
                });
        connect(_videoStreamServer, &VideoStreamServer::configReady, _webBridgeServer, &WebBridgeServer::cacheVideoConfig);

        // Feeds _videoStreamServer's tap: owns its own GstVideoReceiver (independent of
        // VideoManager, which headless boot never initializes with a window -- see _initVideo()
        // above), started from whatever VideoSettings currently has configured. Inert (logs once,
        // no retry) when no source URI is configured or GStreamer support isn't compiled in.
        _ghostVideoSource = new GhostVideoSource(_videoStreamServer, this);
        _ghostVideoSource->start();

        // §16.6 live-apply: SettingsChannel stays GStreamer-free (its videoSettingChanged() signal
        // doc comment explains why), so QGCApplication -- which already has both objects and is
        // compiled only when QGC_GST_STREAMING is available -- makes this connection instead. Every
        // accepted Video-group setSetting re-invokes start(), which is idempotent and always
        // re-resolves the configured URI from scratch (see GhostVideoSource::start()'s doc
        // comment), so this is the whole live-apply mechanism -- no new state machine needed.
        connect(_settingsChannel, &SettingsChannel::videoSettingChanged, _ghostVideoSource, &GhostVideoSource::start);
#endif

        _webBridge->start();

        if (!_webBridgeServer->start()) {
            qCCritical(QGCApplicationLog) << "WebBridge server failed to bind" << (_bridgeHost.isEmpty() ? QStringLiteral("127.0.0.1") : _bridgeHost) << "port" << _bridgePort;
        }
    }

    // Simulated PX4 vehicle for ghost/bridge development: only when explicitly requested via --mock-link.
    if (_mockLink) {
#ifdef QGC_ENABLE_MOCKLINK
        // enableCamera=true (Q8d): MockLink's periodic optical-flow image emission
        // (DATA_TRANSMISSION_HANDSHAKE + ENCAPSULATED_DATA -- see
        // MockLink::_sendMockOpticalFlowImage()) rides along on this same flag as the Camera
        // Protocol v2 simulation it already gates, so the ghost's `--mock-link` e2e now
        // exercises the `image` channel (PROTOCOL.md §15) end to end too, same as it already
        // does for telemetry/mission/command.
        (void) MockLink::startPX4MockLink(false /* sendStatusText */, true /* enableCamera */, false /* enableGimbal */);
#else
        qCWarning(QGCApplicationLog) << "--mock-link requested but this build was not compiled with QGC_ENABLE_MOCKLINK";
#endif
    }

    // Connect links with flag AutoconnectLink
    LinkManager::instance()->startAutoConnectedLinks();
}

void QGCApplication::reportMissingParameter(int componentId, const QString &name)
{
    const QPair<int, QString> missingParam(componentId, name);

    if (!_missingParams.contains(missingParam)) {
        _missingParams.append(missingParam);
    }
    _missingParamsDelayedDisplayTimer.start();
}

void QGCApplication::_missingParamsDisplay()
{
    if (_missingParams.isEmpty()) {
        return;
    }

    QString params;
    for (QPair<int, QString>& missingParam: _missingParams) {
        const QString param = QStringLiteral("%1:%2").arg(missingParam.first).arg(missingParam.second);
        if (params.isEmpty()) {
            params += param;
        } else {
            params += QStringLiteral(", %1").arg(param);
        }

    }
    _missingParams.clear();

    showAppMessage(tr("Parameters are missing from firmware. You may be running a version of firmware which is not fully supported or your firmware has a bug in it. Missing params: %1").arg(params));
}

QObject *QGCApplication::_rootQmlObject()
{
#ifdef QGC_ENABLE_QML
    if (_qmlAppEngine && _qmlAppEngine->rootObjects().size()) {
        return _qmlAppEngine->rootObjects()[0];
    }
#endif

    return nullptr;
}

void QGCApplication::showCriticalVehicleMessage(const QString &message)
{
    // PreArm messages are handled by Vehicle and shown in Map
    if (message.startsWith(QStringLiteral("PreArm")) || message.startsWith(QStringLiteral("preflight"), Qt::CaseInsensitive)) {
        return;
    }

    // Unconditional -- in particular this also fires in headless boot, where the branches below
    // never reach a UI (_showErrorsInToolbar stays false, see _initForHeadlessBoot()) and this
    // would otherwise be silently dropped to the log. NotificationChannel (src/WebBridge/) is
    // exactly that non-UI consumer.
    emit criticalVehicleMessageAnnounced(message);

    QObject *const rootQmlObject = _rootQmlObject();
    if (rootQmlObject && _showErrorsInToolbar) {
        QVariant varReturn;
        QVariant varMessage = QVariant::fromValue(message);
        QMetaObject::invokeMethod(rootQmlObject, "showCriticalVehicleMessage", Q_RETURN_ARG(QVariant, varReturn), Q_ARG(QVariant, varMessage));
    } else if (runningUnitTests() || !_showErrorsInToolbar) {
        // Unit tests can run without UI
        qCDebug(QGCApplicationLog) << "QGCApplication::showCriticalVehicleMessage unittest" << message;
    } else {
        qCWarning(QGCApplicationLog) << "Internal error";
    }
}

void QGCApplication::showAppMessage(const QString &message, const QString &title)
{
    const QString dialogTitle = title.isEmpty() ? applicationName() : title;

    // Unconditional, same reasoning as showCriticalVehicleMessage() above -- NotificationChannel
    // is a non-UI consumer that wants every app message regardless of dialog/headless routing.
    emit appMessageAnnounced(message, dialogTitle);

    QObject *const rootQmlObject = _rootQmlObject();
    if (rootQmlObject) {
        QVariant varReturn;
        QVariant varMessage = QVariant::fromValue(message);
        QMetaObject::invokeMethod(rootQmlObject, "_showMessageDialog", Q_RETURN_ARG(QVariant, varReturn), Q_ARG(QVariant, dialogTitle), Q_ARG(QVariant, varMessage));
    } else if (runningUnitTests()) {
        // Unit tests can run without UI
        // We don't use a logging category to make it easier to debug unit tests
        qDebug() << "QGCApplication::showAppMessage unittest title:message" << dialogTitle << message;
    } else if (_headless) {
        // Headless has no dialogs to show messages in. Route to the log instead of queueing forever.
        qCWarning(QGCApplicationLog) << "QGCApplication::showAppMessage title:message" << dialogTitle << message;
    } else {
        // UI isn't ready yet
        _delayedAppMessages.append(QPair<QString, QString>(dialogTitle, message));
        QTimer::singleShot(200, this, &QGCApplication::_showDelayedAppMessages);
    }
}

void QGCApplication::showRebootAppMessage(const QString &message, const QString &title)
{
    static QTime lastRebootMessage;

    const QTime currentTime = QTime::currentTime();
    const QTime previousTime = lastRebootMessage;
    lastRebootMessage = currentTime;

    if (previousTime.isValid() && (previousTime.msecsTo(currentTime) < (60 * 1000 * 2))) {
        // Debounce reboot messages
        return;
    }

    showAppMessage(message, title);
}

void QGCApplication::_showDelayedAppMessages()
{
    if (_rootQmlObject()) {
        for (const QPair<QString, QString>& appMsg: _delayedAppMessages) {
            showAppMessage(appMsg.second, appMsg.first);
        }
        _delayedAppMessages.clear();
    } else {
        QTimer::singleShot(200, this, &QGCApplication::_showDelayedAppMessages);
    }
}

#ifdef QGC_ENABLE_QML
QQuickWindow *QGCApplication::mainRootWindow()
{
    if (!_mainRootWindow) {
        _mainRootWindow = qobject_cast<QQuickWindow*>(_rootQmlObject());
    }

    return _mainRootWindow;
}
#endif

void QGCApplication::showVehicleConfig()
{
    if (_rootQmlObject()) {
      QMetaObject::invokeMethod(_rootQmlObject(), "showVehicleConfig");
    }
}

void QGCApplication::qmlAttemptWindowClose()
{
    if (_rootQmlObject()) {
        QMetaObject::invokeMethod(_rootQmlObject(), "attemptWindowClose");
    }
}

void QGCApplication::_checkForNewVersion()
{
#ifdef QGC_ENABLE_QT_NETWORK
    if (_runningUnitTests) {
        return;
    }

    if (!_parseVersionText(applicationVersion(), _majorVersion, _minorVersion, _buildVersion)) {
        return;
    }

    const QString versionCheckFile = QGCCorePlugin::instance()->stableVersionCheckFileUrl();
    if (!versionCheckFile.isEmpty()) {
        QGCFileDownload *const download = new QGCFileDownload(this);
        (void) connect(download, &QGCFileDownload::finished, this, &QGCApplication::_qgcCurrentStableVersionDownloadComplete);
        if (!download->start(versionCheckFile)) {
            qCDebug(QGCApplicationLog) << "Download QGC stable version failed to start" << download->errorString();
            download->deleteLater();
        }
    }
#endif  // QGC_ENABLE_QT_NETWORK
}

#ifdef QGC_ENABLE_QT_NETWORK
void QGCApplication::_qgcCurrentStableVersionDownloadComplete(bool success, const QString &localFile, const QString &errorMsg)
{
    if (success) {
        QFile versionFile(localFile);
        if (versionFile.open(QIODevice::ReadOnly)) {
            QTextStream textStream(&versionFile);
            const QString version = textStream.readLine();

            qCDebug(QGCApplicationLog) << version;

            int majorVersion, minorVersion, buildVersion;
            if (_parseVersionText(version, majorVersion, minorVersion, buildVersion)) {
                if (_majorVersion < majorVersion ||
                        ((_majorVersion == majorVersion) && (_minorVersion < minorVersion)) ||
                        ((_majorVersion == majorVersion) && (_minorVersion == minorVersion) && (_buildVersion < buildVersion))) {
                    showAppMessage(tr("There is a newer version of %1 available. You can download it from %2.").arg(applicationName()).arg(QGCCorePlugin::instance()->stableDownloadLocation()), tr("New Version Available"));
                }
            }
        }
    } else if (!errorMsg.isEmpty()) {
        qCDebug(QGCApplicationLog) << "Download QGC stable version failed" << errorMsg;
    }

    sender()->deleteLater();
}
#endif  // QGC_ENABLE_QT_NETWORK

bool QGCApplication::_parseVersionText(const QString &versionString, int &majorVersion, int &minorVersion, int &buildVersion)
{
    static const QRegularExpression regExp("v(\\d+)\\.(\\d+)\\.(\\d+)");
    const QRegularExpressionMatch match = regExp.match(versionString);
    if (match.hasMatch() && match.lastCapturedIndex() == 3) {
        majorVersion = match.captured(1).toInt();
        minorVersion = match.captured(2).toInt();
        buildVersion = match.captured(3).toInt();
        return true;
    }

    return false;
}

QString QGCApplication::cachedParameterMetaDataFile()
{
    QSettings settings;
    const QDir parameterDir = QFileInfo(settings.fileName()).dir();
    return parameterDir.filePath(QStringLiteral("ParameterFactMetaData.json"));
}

QString QGCApplication::cachedAirframeMetaDataFile()
{
    QSettings settings;
    const QDir airframeDir = QFileInfo(settings.fileName()).dir();
    return airframeDir.filePath(QStringLiteral("PX4AirframeFactMetaData.xml"));
}

int QGCApplication::CompressedSignalList::_signalIndex(const QMetaMethod &method)
{
    if (method.methodType() != QMetaMethod::Signal) {
        qCWarning(QGCApplicationLog) << "Internal error:" << Q_FUNC_INFO <<  "not a signal" << method.methodType();
        return -1;
    }

    int index = -1;
    const QMetaObject *metaObject = method.enclosingMetaObject();
    for (int i=0; i<=method.methodIndex(); i++) {
        if (metaObject->method(i).methodType() != QMetaMethod::Signal) {
            continue;
        }
        index++;
    }

    return index;
}

void QGCApplication::CompressedSignalList::add(const QMetaMethod &method)
{
    const QMetaObject *metaObject = method.enclosingMetaObject();
    const int signalIndex = _signalIndex(method);

    if (signalIndex != -1 && !contains(metaObject, signalIndex)) {
        _signalMap[method.enclosingMetaObject()].insert(signalIndex);
    }
}

void QGCApplication::CompressedSignalList::remove(const QMetaMethod &method)
{
    const int signalIndex = _signalIndex(method);
    const QMetaObject *const metaObject = method.enclosingMetaObject();

    if (signalIndex != -1 && _signalMap.contains(metaObject) && _signalMap[metaObject].contains(signalIndex)) {
        _signalMap[metaObject].remove(signalIndex);
        if (_signalMap[metaObject].count() == 0) {
            _signalMap.remove(metaObject);
        }
    }
}

bool QGCApplication::CompressedSignalList::contains(const QMetaObject *metaObject, int signalIndex)
{
    return _signalMap.contains(metaObject) && _signalMap[metaObject].contains(signalIndex);
}

void QGCApplication::addCompressedSignal(const QMetaMethod &method)
{
    _compressedSignals.add(method);
}

void QGCApplication::removeCompressedSignal(const QMetaMethod &method)
{
    _compressedSignals.remove(method);
}

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
bool QGCApplication::compressEvent(QEvent *event, QObject *receiver, QPostEventList *postedEvents)
{
    if (event->type() != QEvent::MetaCall) {
        return QGCApplicationBase::compressEvent(event, receiver, postedEvents);
    }

    const QMetaCallEvent *mce = static_cast<QMetaCallEvent*>(event);
    if (!mce->sender() || !_compressedSignals.contains(mce->sender()->metaObject(), mce->signalId())) {
        return QGCApplicationBase::compressEvent(event, receiver, postedEvents);
    }

    for (QPostEventList::iterator it = postedEvents->begin(); it != postedEvents->end(); ++it) {
        QPostEvent &cur = *it;
        if (cur.receiver != receiver || cur.event == 0 || cur.event->type() != event->type()) {
            continue;
        }
        const QMetaCallEvent *cur_mce = static_cast<QMetaCallEvent*>(cur.event);
        if (cur_mce->sender() != mce->sender() || cur_mce->signalId() != mce->signalId() || cur_mce->id() != mce->id()) {
            continue;
        }
        /* Keep The Newest Call */
        // We can't merely qSwap the existing posted event with the new one, since QEvent
        // keeps track of whether it has been posted. Deletion of a formerly posted event
        // takes the posted event list mutex and does a useless search of the posted event
        // list upon deletion. We thus clear the QEvent::posted flag before deletion.
        struct EventHelper : private QEvent {
            static void clearPostedFlag(QEvent * ev) {
                (&static_cast<EventHelper*>(ev)->t)[1] &= ~0x8001; // Hack to clear QEvent::posted
            }
        };
        EventHelper::clearPostedFlag(cur.event);
        delete cur.event;
        cur.event = event;
        return true;
    }

    return false;
}
QT_WARNING_POP

bool QGCApplication::event(QEvent *e)
{
#ifdef QGC_ENABLE_QML
    if (e->type() == QEvent::Quit) {
        if (!_mainRootWindow) {
            return QGCApplicationBase::event(e);
        }
        // On OSX if the user selects Quit from the menu (or Command-Q) the ApplicationWindow does not signal closing. Instead you get a Quit event here only.
        // This in turn causes the standard QGC shutdown sequence to not run. So in this case we close the window ourselves such that the
        // signal is sent and the normal shutdown sequence runs.
        const bool forceClose = _mainRootWindow->property("_forceClose").toBool();
        qCDebug(QGCApplicationLog) << "Quit event" << forceClose;
        // forceClose
        //  true:   Standard QGC shutdown sequence is complete. Let the app quit normally by falling through to the base class processing.
        //  false:  QGC shutdown sequence has not been run yet. Don't let this event close the app yet. Close the main window to kick off the normal shutdown.
        if (!forceClose) {
            //
            _mainRootWindow->close();
            e->ignore();
            return true;
        }
    }
#endif

    return QGCApplicationBase::event(e);
}

#ifdef QGC_ENABLE_QML
QGCImageProvider *QGCApplication::qgcImageProvider()
{
    if (_qmlAppEngine) {
        return dynamic_cast<QGCImageProvider*>(_qmlAppEngine->imageProvider(_qgcImageProviderId));
    }

    return nullptr;
}
#endif // QGC_ENABLE_QML

void QGCApplication::shutdown()
{
    qCDebug(QGCApplicationLog) << "Exit";

    if (_headless) {
        // Stop serving bridge clients before tearing down what feeds them.
        if (_webBridgeServer) {
            _webBridgeServer->stop();
            _webBridge->stop();
#ifdef QGC_GST_STREAMING
            if (_ghostVideoSource) {
                // Detaches the tap and stops the GstVideoReceiver that feeds it, in that order,
                // before either object is destroyed below.
                _ghostVideoSource->stop();
            }
            if (_videoStreamServer) {
                // Idempotent: _ghostVideoSource->stop() above already detached the tap in the
                // normal case. Kept as a defensive no-op for a _videoStreamServer that somehow
                // got attached by something other than _ghostVideoSource.
                _videoStreamServer->stop();
            }
#endif
        }
        // The QML main window calls LinkManager.shutdown() as it closes
        // (MainWindow.qml); headless has no window, so disconnect links here
        // before static teardown destroys them mid-signal.
        LinkManager::instance()->shutdown();
    }

    if (_videoManagerInitialized) {
        VideoManager::instance()->cleanup();
        VideoManager2::instance()->cleanup();
    }

    QGCCorePlugin::instance()->cleanup();

    if (_runningUnitTests || _simpleBootTest) {
        const QSettings settings;
        const QString settingsFile = settings.fileName();
        if (QFile::exists(settingsFile)) {
            if (QFile::remove(settingsFile)) {
                qCDebug(QGCApplicationLog) << "Removed test run settings file:" << settingsFile;
            } else {
                qCWarning(QGCApplicationLog) << "Failed to remove test run settings file:" << settingsFile;
            }
        }

        // Remove the app-specific settings directory (parent of ParamCache)
        QDir settingsAppDir(ParameterManager::parameterCacheDir());
        settingsAppDir.cdUp();
        if (settingsAppDir.exists()) {
            if (settingsAppDir.removeRecursively()) {
                qCDebug(QGCApplicationLog) << "Removed test run settings directory:" << settingsAppDir.absolutePath();
            } else {
                qCWarning(QGCApplicationLog) << "Failed to remove test run settings directory:" << settingsAppDir.absolutePath();
            }
        }

        QDir appDir(SettingsManager::instance()->appSettings()->savePath()->rawValue().toString());
        if (appDir.exists()) {
            if (appDir.removeRecursively()) {
                qCDebug(QGCApplicationLog) << "Removed test run app data directory:" << appDir.absolutePath();
            } else {
                qCWarning(QGCApplicationLog) << "Failed to remove test run app data directory:" << appDir.absolutePath();
            }
        }
    }

#ifdef QGC_ENABLE_QML
    // This is bad, but currently qobject inheritances are incorrect and cause crashes on exit without
    delete _qmlAppEngine;
#endif
}
