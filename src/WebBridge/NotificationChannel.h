#pragma once

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QString>

class WebBridge;

Q_DECLARE_LOGGING_CATEGORY(NotificationChannelLog)

/// Turns QGC's local vehicle-announcement machinery into `notification` channel messages
/// (PROTOCOL.md §14). Historically these announcements only ever reached the user via
/// AudioOutput's local text-to-speech engine (or, headless, nowhere at all -- see
/// QGCApplication::showCriticalVehicleMessage()'s doc comment); in this architecture the browser
/// owns audio, so this class re-broadcasts them over the bridge for the web cockpit to display
/// and/or speak itself.
///
/// Sources (connected in this class's constructor for AudioOutput, and by QGCApplication for its
/// own two signals -- see QGCApplication.cc's headless-boot wiring block for why the latter two
/// are wired externally rather than here):
///   - AudioOutput::textAnnounced() (src/Utilities/Audio/AudioOutput.h) -- fires on every
///     AudioOutput::say() call in every build, regardless of TTS engine availability/mute/volume.
///   - QGCApplication::criticalVehicleMessageAnnounced() -- handleCriticalVehicleMessage() slot.
///   - QGCApplication::appMessageAnnounced() -- handleAppMessage() slot.
///
/// Deliberately has no QGCApplication.h / QtGui dependency (see the .cc for why): the two
/// QGCApplication-sourced slots below are plain public slots that QGCApplication.cc connects to
/// its own signals directly, keeping this module's include set at Qt Core only (plus WebBridge),
/// matching src/WebBridge/CMakeLists.txt's module boundary.
class NotificationChannel : public QObject
{
    Q_OBJECT

public:
    /// @param bridge Not owned; must outlive this object. Used for serverTimeUs() (PROTOCOL.md
    ///                §14 `timeUs`) -- notification messages carry no seq/snapshot envelope, so
    ///                WebBridge::makeStreamMessage()/resetSeq() are not used here.
    explicit NotificationChannel(WebBridge *bridge, QObject *parent = nullptr);
    ~NotificationChannel() override;

public slots:
    /// Connectable to QGCApplication::criticalVehicleMessageAnnounced(). Always severity
    /// `"critical"` (PROTOCOL.md §14.1).
    void handleCriticalVehicleMessage(const QString &message);

    /// Connectable to QGCApplication::appMessageAnnounced(). Severity is classified heuristically
    /// from @p message (PROTOCOL.md §14.1); @p title is not currently surfaced on the wire.
    void handleAppMessage(const QString &message, const QString &title);

    /// Connectable to WebBridgeServer::clientAuthenticated(quint64). Sends the one-shot,
    /// per-connection "Ghost bridge ready" welcome notification (PROTOCOL.md §14.3) addressed
    /// only to @p clientToken via welcomeReady() -- never broadcastAll(), so it reliably reaches a
    /// client that authenticates well after this channel (and the bridge) started.
    void sendWelcome(quint64 clientToken);

signals:
    /// One `notification` message (PROTOCOL.md §14), already built (type/severity/text/timeUs).
    /// Connect to WebBridgeServer::broadcastAll() -- notification requires no subscription, same
    /// fan-out shape as WebBridge::tickReady() (§11.1).
    void notificationReady(const QJsonObject &message);

    /// The one-shot welcome notification (§14.3) addressed to a single connection. Connect to
    /// WebBridgeServer::sendToClient().
    void welcomeReady(quint64 clientToken, const QJsonObject &message);

private slots:
    /// Connected (in the constructor) to AudioOutput::textAnnounced().
    void _handleSpoken(const QString &text);

private:
    enum class Severity {
        Info,
        Warning,
        Critical,
    };

    static QString _severityString(Severity severity);

    /// Classifies free-form announcement/app-message text into info/warning by a small
    /// case-insensitive keyword scan (PROTOCOL.md §14.1). Never returns Critical -- that severity
    /// is reserved for the unambiguous showCriticalVehicleMessage() source.
    static Severity _classify(const QString &text);

    /// Builds a `notification` message (PROTOCOL.md §14) and, unless suppressed by the 1 s
    /// per-text dedup window (§14.2), emits notificationReady() for it. Updates the dedup
    /// bookkeeping either way is skipped on suppression (only a successful emission resets the
    /// window), matching "repeated within 1 s of its own previous emission" in §14.2.
    void _emit(Severity severity, const QString &text);

    /// PROTOCOL.md §14.2: identical text within this window of its own last emission is dropped.
    static constexpr quint64 kDedupWindowUs = 1'000'000;

    WebBridge *_bridge = nullptr;                 ///< Not owned
    QHash<QString, quint64> _lastEmittedAtUs;     ///< text -> serverTimeUs() of its last emission (§14.2)
};
