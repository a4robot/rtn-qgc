#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QString>

#include <limits>

class QGCAbstractState;
class QGCStateMachine;

/// Portable counterpart of ../Helpers/StateMachineProfiler.h. Ported as-is
/// per AUDIT.md.
class StateMachineProfiler : public QObject
{
    Q_OBJECT

public:
    struct StateProfile {
        QString name;
        int entryCount = 0;
        qint64 totalTimeMs = 0;
        qint64 minTimeMs = std::numeric_limits<qint64>::max();
        qint64 maxTimeMs = 0;
        qint64 lastEntryTime = 0;

        double averageTimeMs() const {
            return entryCount > 0 ? static_cast<double>(totalTimeMs) / entryCount : 0.0;
        }
    };

    explicit StateMachineProfiler(QGCStateMachine *machine);
    ~StateMachineProfiler() override = default;

    void setEnabled(bool enabled);
    bool isEnabled() const { return _enabled; }

    void reset();

    StateProfile profile(const QString &stateName) const;
    QHash<QString, StateProfile> allProfiles() const { return _profiles; }

    qint64 totalRuntimeMs() const { return _totalRuntimeMs; }
    int transitionCount() const { return _transitionCount; }

    QString summary() const;
    QJsonObject toJson() const;
    void logProfile() const;

private slots:
    void _onMachineStarted();
    void _onMachineStopped();
    void _onStateEntered();
    void _onStateExited();

private:
    QGCStateMachine *_machine = nullptr;
    bool _enabled = false;

    QHash<QString, StateProfile> _profiles;
    QElapsedTimer _machineTimer;
    QElapsedTimer _stateTimer;
    qint64 _totalRuntimeMs = 0;
    int _transitionCount = 0;

    QString _currentStateName;
    QList<QMetaObject::Connection> _stateConnections;
};
