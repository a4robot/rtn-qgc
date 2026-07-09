#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QMap>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtCore/QTranslator>
#include <QtGui/QGuiApplication>

namespace QGCCommandLineParser {
    struct CommandLineParseResult;
}

#ifdef QGC_ENABLE_QML
class QQmlApplicationEngine;
class QQuickWindow;
class QGCImageProvider;
#endif
class QGCApplication;
class QEvent;
class QPostEventList;
class QMetaMethod;
struct QMetaObject;
class CommandChannel;
class FactChannel;
class MissionChannel;
class NotificationChannel;
class TelemetryChannel;
class WebBridge;
class WebBridgeServer;
#ifdef QGC_GST_STREAMING
class VideoStreamServer;
class GhostVideoSource;
#endif

#if defined(qApp)
#undef qApp
#endif
#define qApp (static_cast<QGCApplication*>(QGuiApplication::instance()))

#if defined(qGuiApp)
#undef qGuiApp
#endif
#define qGuiApp (static_cast<QGCApplication*>(QGuiApplication::instance()))

#define qgcApp() qApp

/// \brief The main application and management class.
///
class QGCApplication : public QGuiApplication
{
    Q_OBJECT

    /// Unit Test have access to creating and destroying singletons
    friend class UnitTest;
public:
    QGCApplication(int &argc, char *argv[], const QGCCommandLineParser::CommandLineParseResult &args);
    ~QGCApplication();

    bool runningUnitTests() const { return _runningUnitTests; }
    bool simpleBootTest() const { return _simpleBootTest; }
    bool headless() const { return _headless; }
    bool bootTestPassed() const { return _bootTestPassed; }

    /// Returns true if Qt debug output should be logged to a file
    bool logOutput() const { return _logOutput; }

    /// Used to report a missing Parameter. Warning will be displayed to user. Method may be called
    /// multiple times.
    void reportMissingParameter(int componentId, const QString &name);

    /// @return true: Fake ui into showing mobile interface
    bool fakeMobile() const { return _fakeMobile; }

    void setLanguage();
#ifdef QGC_ENABLE_QML
    QQuickWindow *mainRootWindow();
#endif
    uint64_t msecsSinceBoot() const { return _msecsElapsedTime.elapsed(); }

    /// Registers the signal such that only the last duplicate signal added is left in the queue.
    void addCompressedSignal(const QMetaMethod &method);

    void removeCompressedSignal(const QMetaMethod &method);

    bool event(QEvent *e) final;

    static QString cachedParameterMetaDataFile();
    static QString cachedAirframeMetaDataFile();

public:
    /// Perform initialize which is common to both normal application running and unit tests.
    void init();
    void shutdown();

#ifdef QGC_ENABLE_QML
    /// Although public, these methods are internal and should only be called by UnitTest code
    QQmlApplicationEngine *qmlAppEngine() const { return _qmlAppEngine; }
#endif

signals:
    void languageChanged(const QLocale &locale);

    /// Emitted from showCriticalVehicleMessage(), unconditionally and regardless of whether a
    /// root QML window exists to display it (i.e. also in headless boot, where that method is
    /// otherwise a pure log statement -- see its .cc comment). WebBridge's NotificationChannel
    /// (src/WebBridge/NotificationChannel.h) subscribes to this to source `notification`
    /// severity `critical` messages (PROTOCOL.md §14). PreArm/preflight messages are already
    /// filtered out by showCriticalVehicleMessage() before this fires, same as the UI path.
    void criticalVehicleMessageAnnounced(const QString &message);

    /// Emitted from showAppMessage(), unconditionally, regardless of UI/headless state. Does not
    /// overlap with AudioOutput::textAnnounced() or criticalVehicleMessageAnnounced() --
    /// showAppMessage() does not call AudioOutput::say() or showCriticalVehicleMessage().
    /// WebBridge's NotificationChannel subscribes to this for `notification` messages whose
    /// severity it classifies heuristically from @p message (PROTOCOL.md §14).
    void appMessageAnnounced(const QString &message, const QString &title);

public slots:
    void showVehicleConfig();

    void qmlAttemptWindowClose();

    /// Get current language
    QLocale getCurrentLanguage() const { return _locale; }

    /// Show non-modal vehicle message to the user
    void showCriticalVehicleMessage(const QString &message);

    /// Show modal application message to the user
    void showAppMessage(const QString &message, const QString &title = QString());

    /// Show modal application message to the user about the need for a reboot. Multiple messages will be supressed if they occur
    /// one after the other.
    void showRebootAppMessage(const QString &message, const QString &title = QString());

#ifdef QGC_ENABLE_QML
    /// Image provider registered as "QGCImages" on the QML engine (MAVLink image protocol frames).
    QGCImageProvider *qgcImageProvider();
#endif

private slots:
    /// Called when the delay timer fires to show the missing parameters warning
    void _missingParamsDisplay();
    void _qgcCurrentStableVersionDownloadComplete(bool success, const QString &localFile, const QString &errorMsg);
    static bool _parseVersionText(const QString &versionString, int &majorVersion, int &minorVersion, int &buildVersion);
    void _showDelayedAppMessages();

private:
    bool compressEvent(QEvent *event, QObject *receiver, QPostEventList *postedEvents) final;

    bool _initVideo();

#ifdef QGC_ENABLE_QML
    /// Initialize the application for normal application boot. Or in other words we are not going to run unit tests.
    void _initForNormalAppBoot();
#endif

    /// Initialize the application for headless boot: core services only, no QML engine or root window.
    void _initForHeadlessBoot();

    QObject *_rootQmlObject();
    void _checkForNewVersion();

    bool _runningUnitTests = false;
    bool _simpleBootTest = false;
    bool _headless = false;    ///< true: Running without the QML UI
    quint16 _bridgePort = 0;    ///< --bridge-port: websocket bridge listen port, 0 = bridge disabled
    QString _bridgeHost;        ///< --bridge-host: websocket bridge bind address, empty = localhost-only default
    QString _bridgeToken;       ///< --bridge-token: required web bridge hello token, empty = any non-empty token accepted
    bool _mockLink = false;    ///< --mock-link: start a simulated PX4 vehicle (requires QGC_ENABLE_MOCKLINK build)
    WebBridge *_webBridge = nullptr;                ///< Bridge core (headless + --bridge-port only)
    WebBridgeServer *_webBridgeServer = nullptr;    ///< Bridge websocket listener
    TelemetryChannel *_telemetryChannel = nullptr;  ///< Vehicle state → bridge telemetry stream
    FactChannel *_factChannel = nullptr;            ///< Parameter channel
    CommandChannel *_commandChannel = nullptr;      ///< §5 guided-action channel
    MissionChannel *_missionChannel = nullptr;      ///< §7 mission channel
    NotificationChannel *_notificationChannel = nullptr; ///< §14 announcement channel
#ifdef QGC_GST_STREAMING
    VideoStreamServer *_videoStreamServer = nullptr; ///< GStreamer tee tap → bridge `video` channel (§9)
    GhostVideoSource *_ghostVideoSource = nullptr;   ///< Feeds _videoStreamServer's tap from VideoSettings' configured source
#endif
    bool _fakeMobile = false;    ///< true: Fake ui into displaying mobile interface
    bool _logOutput = false;    ///< true: Log Qt debug output to file
    quint8 _systemId = 0; ///< MAVLink system ID, 0 means not set

    static constexpr int _missingParamsDelayedDisplayTimerTimeout = 1000;   ///< Timeout to wait for next missing fact to come in before display
    QTimer _missingParamsDelayedDisplayTimer;                               ///< Timer use to delay missing fact display
    QList<QPair<int,QString>> _missingParams;                               ///< List of missing parameter component id:name

#ifdef QGC_ENABLE_QML
    QQmlApplicationEngine *_qmlAppEngine = nullptr;
#endif
    bool _settingsUpgraded = false;    ///< true: Settings format has been upgrade to new version
    int _majorVersion = 0;
    int _minorVersion = 0;
    int _buildVersion = 0;
#ifdef QGC_ENABLE_QML
    QQuickWindow *_mainRootWindow = nullptr;
#endif
    QTranslator _qgcTranslatorSourceCode;           ///< translations for source code C++/Qml
    QTranslator _qgcTranslatorQtLibs;               ///< tranlsations for Qt libraries
    QLocale _locale;
    bool _error = false;
    bool _showErrorsInToolbar = false;
    QElapsedTimer _msecsElapsedTime;
    bool _videoManagerInitialized = false;
    bool _bootTestPassed = true;

    QList<QPair<QString /* title */, QString /* message */>> _delayedAppMessages;

    class CompressedSignalList
    {
    public:
        CompressedSignalList() {}
        void add(const QMetaMethod &method);
        void remove(const QMetaMethod &method);
        bool contains(const QMetaObject *metaObject, int signalIndex);

    private:
        /// Returns a signal index that is can be compared to QMetaCallEvent.signalId
        static int _signalIndex(const QMetaMethod &method);

        QMap<const QMetaObject*, QSet<int>> _signalMap;

        Q_DISABLE_COPY(CompressedSignalList)
    };

    CompressedSignalList _compressedSignals;

    const QString _settingsVersionKey = QStringLiteral("SettingsVersion"); ///< Settings key which hold settings version

#ifdef QGC_ENABLE_QML
    const QString _qgcImageProviderId = QStringLiteral("QGCImages");
#endif
};
