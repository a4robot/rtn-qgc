#pragma once

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>

class QGCAbstractState;
class QGCStateMachine;

/// Portable counterpart of ../Helpers/StateHistoryRecorder.h. Ported as-is
/// per AUDIT.md -- this never depended on QStateMachine internals, only on
/// findChildren<QAbstractState*>() and the entered/exited signals, which
/// this port's QGCAbstractState still emits.
class StateHistoryRecorder : public QObject
{
    Q_OBJECT

public:
    enum TransitionReason {
        Entered,
        Exited,
        Timeout,
        Error,
        Signal,
        Event,
        Unknown
    };
    Q_ENUM(TransitionReason)

    struct HistoryEntry {
        QDateTime timestamp;
        QString stateName;
        TransitionReason reason;
        QString details;

        QJsonObject toJson() const;
        QString toString() const;
    };

    explicit StateHistoryRecorder(QGCStateMachine *machine, int maxEntries = 1000);
    ~StateHistoryRecorder() override = default;

    void setEnabled(bool enabled);
    bool isEnabled() const { return _enabled; }

    void setMaxEntries(int max);
    int maxEntries() const { return _maxEntries; }

    void clear();
    int count() const { return _history.size(); }
    QList<HistoryEntry> history() const { return _history; }
    QList<HistoryEntry> lastEntries(int n) const;
    QList<HistoryEntry> entriesForState(const QString &stateName) const;

    QString dumpHistory() const;
    QJsonArray toJson() const;
    void logHistory() const;

    void addEntry(const QString &stateName, TransitionReason reason, const QString &details = QString());

private slots:
    void _onStateEntered();
    void _onStateExited();

private:
    void _addEntry(const HistoryEntry &entry);
    void _connectToState(QGCAbstractState *state);

    QGCStateMachine *_machine = nullptr;
    bool _enabled = false;
    int _maxEntries = 1000;
    QList<HistoryEntry> _history;
    QList<QMetaObject::Connection> _connections;
};
