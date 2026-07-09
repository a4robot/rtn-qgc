#pragma once

#include <QtCore/QDateTime>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QTextStream>

#include <functional>

class QGCAbstractState;
class QGCStateMachine;

/// Portable counterpart of ../Helpers/StateMachineLogger.h. Ported as-is
/// per AUDIT.md -- this is exactly the hook tools/ghost/statemachine-port/
/// capture.sh uses (QGC_STATEMACHINE_LOG=1), so its output format/text must
/// stay byte-identical to the Qt-based original for the replay-compare
/// against baseline-mocklink-connect.log to mean anything. Only the state
/// type it connects to (QGCAbstractState instead of QAbstractState) and the
/// machine signals it connects to (QGCStateMachine::started/stopped instead
/// of QStateMachine::started/stopped) changed.
class StateMachineLogger : public QObject
{
    Q_OBJECT

public:
    enum LogLevel {
        Silent = 0,
        Error = 1,
        Normal = 2,
        Verbose = 3,
        Trace = 4
    };
    Q_ENUM(LogLevel)

    enum LogEvent {
        EventNone           = 0,
        EventStateEntry     = 1 << 0,
        EventStateExit      = 1 << 1,
        EventTransition     = 1 << 2,
        EventSignal         = 1 << 3,
        EventTimeout        = 1 << 4,
        EventError          = 1 << 5,
        EventMachineStart   = 1 << 6,
        EventMachineStop    = 1 << 7,
        EventRetry          = 1 << 8,
        EventProgress       = 1 << 9,
        EventCustom         = 1 << 10,

        EventAll            = 0xFFFF,
        EventStateChanges   = EventStateEntry | EventStateExit,
        EventMachineEvents  = EventMachineStart | EventMachineStop,
        EventErrors         = EventError | EventTimeout
    };
    Q_DECLARE_FLAGS(LogEvents, LogEvent)

    struct LogEntry {
        QDateTime timestamp;
        qint64 elapsedMs = 0;
        LogLevel level = Normal;
        LogEvent event = EventNone;
        QString machine;
        QString state;
        QString previousState;
        QString message;
        QString transitionReason;
        qint64 stateDurationMs = 0;
        QJsonObject context;
        int depth = 0;

        QString toString(bool colored = false, bool showTiming = true, bool indent = true) const;
        QJsonObject toJson() const;
    };

    using LogHandler = std::function<void(const LogEntry &)>;

    explicit StateMachineLogger(QGCStateMachine *machine, QObject *parent = nullptr);
    ~StateMachineLogger() override;

    void setEnabled(bool enabled);
    bool isEnabled() const { return _enabled; }

    void setLogLevel(LogLevel level) { _logLevel = level; }
    LogLevel logLevel() const { return _logLevel; }

    void setStateLogLevel(const QString &stateName, LogLevel level);
    void clearStateLogLevel(const QString &stateName);

    void setLogFilter(LogEvents events) { _logFilter = events; }
    LogEvents logFilter() const { return _logFilter; }

    void excludeState(const QString &stateName);
    void includeState(const QString &stateName);
    void clearExclusions() { _excludedStates.clear(); }

    void setColoredOutput(bool enabled) { _coloredOutput = enabled; }
    bool coloredOutput() const { return _coloredOutput; }

    void setLogTimings(bool enabled) { _logTimings = enabled; }
    bool logTimings() const { return _logTimings; }

    void setLogIndent(bool enabled) { _logIndent = enabled; }
    bool logIndent() const { return _logIndent; }

    void setLogTransitionReasons(bool enabled) { _logTransitionReasons = enabled; }
    bool logTransitionReasons() const { return _logTransitionReasons; }

    bool setLogFile(const QString &filePath);
    void closeLogFile();

    void setLogHandler(LogHandler handler) { _customHandler = std::move(handler); }

    void enableCrashLog(int maxEntries);
    void disableCrashLog();
    QString dumpCrashLog() const;
    QList<LogEntry> crashLogEntries() const { return _crashLog; }

    void log(LogLevel level, const QString &message, const QJsonObject &context = QJsonObject());
    void logEvent(LogEvent event, const QString &message, const QJsonObject &context = QJsonObject());

    QHash<LogEvent, int> eventCounts() const { return _eventCounts; }
    void resetStats() { _eventCounts.clear(); }

private slots:
    void _onMachineStarted();
    void _onMachineStopped();
    void _onStateEntered();
    void _onStateExited();

private:
    void _log(const LogEntry &entry);
    void _connectToState(QGCAbstractState *state);
    QString _colorize(const QString &text, const QString &colorCode) const;
    QString _eventColor(LogEvent event) const;
    LogLevel _effectiveLogLevel(const QString &stateName) const;
    QString _determineTransitionReason() const;

    QGCStateMachine *_machine = nullptr;
    bool _enabled = false;
    LogLevel _logLevel = Normal;
    LogEvents _logFilter = EventAll;

    bool _coloredOutput = false;
    bool _logTimings = true;
    bool _logIndent = true;
    bool _logTransitionReasons = true;

    QSet<QString> _excludedStates;
    QHash<QString, LogLevel> _stateLogLevels;

    QFile *_logFile = nullptr;
    QTextStream *_logStream = nullptr;
    LogHandler _customHandler;

    QList<LogEntry> _crashLog;
    int _crashLogMaxEntries = 0;

    QElapsedTimer _machineTimer;
    QHash<QString, qint64> _stateEntryTimes;
    QString _previousState;
    int _currentDepth = 0;

    QHash<LogEvent, int> _eventCounts;
    QList<QMetaObject::Connection> _connections;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(StateMachineLogger::LogEvents)
