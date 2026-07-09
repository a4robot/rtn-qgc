#include "NotificationChannel.h"

#include "AudioOutput.h"
#include "QGCLoggingCategory.h"
#include "WebBridge.h"

QGC_LOGGING_CATEGORY(NotificationChannelLog, "WebBridge.NotificationChannel")

namespace {

const QString kNotificationType = QStringLiteral("notification");

/// PROTOCOL.md §14.1: keyword scan used to classify AudioOutput::say()/showAppMessage() text into
/// info vs warning. Case-insensitive substring match; first hit wins (order does not matter,
/// these are independent triggers). Kept intentionally small -- this is a heuristic display hint,
/// not a parser of QGC's actual message severity (which showCriticalVehicleMessage()'s hardcoded
/// "critical" already covers for the one source that carries real severity information).
const QStringList kWarningKeywords = {
    QStringLiteral("warning"),
    QStringLiteral("lost"),
    QStringLiteral("breach"),
    QStringLiteral("fail"),
    QStringLiteral("reject"),
    QStringLiteral("error"),
    QStringLiteral("critical"),
    QStringLiteral("emergency"),
};

} // namespace

NotificationChannel::NotificationChannel(WebBridge *bridge, QObject *parent)
    : QObject(parent)
    , _bridge(bridge)
{
    if (!_bridge) {
        qCWarning(NotificationChannelLog) << "constructed with null WebBridge";
    }

    // AudioOutput is a singleton with no QGCApplication dependency, so this connection is safe to
    // make directly here (unlike the two QGCApplication-sourced slots, which QGCApplication.cc
    // connects itself -- see the class doc comment for why).
    connect(AudioOutput::instance(), &AudioOutput::textAnnounced, this, &NotificationChannel::_handleSpoken);

    qCDebug(NotificationChannelLog) << this << "bridge" << static_cast<void*>(_bridge);
}

NotificationChannel::~NotificationChannel()
{
    qCDebug(NotificationChannelLog) << this;
}

QString NotificationChannel::_severityString(Severity severity)
{
    switch (severity) {
    case Severity::Warning:
        return QStringLiteral("warning");
    case Severity::Critical:
        return QStringLiteral("critical");
    case Severity::Info:
    default:
        return QStringLiteral("info");
    }
}

NotificationChannel::Severity NotificationChannel::_classify(const QString &text)
{
    for (const QString &keyword : kWarningKeywords) {
        if (text.contains(keyword, Qt::CaseInsensitive)) {
            return Severity::Warning;
        }
    }
    return Severity::Info;
}

void NotificationChannel::_emit(Severity severity, const QString &text)
{
    if (text.isEmpty()) {
        return;
    }

    const quint64 nowUs = _bridge ? _bridge->serverTimeUs() : 0;

    // PROTOCOL.md §14.2: identical text within 1 s of its own previous emission is dropped
    // entirely (bookkeeping is only updated on a successful emission, not on suppression, so a
    // steady drip of the same text every ~1.0-1.1s still gets through every time rather than
    // sliding later and later).
    const auto lastIt = _lastEmittedAtUs.constFind(text);
    if (lastIt != _lastEmittedAtUs.constEnd() && (nowUs - lastIt.value()) < kDedupWindowUs) {
        qCDebug(NotificationChannelLog) << "dedup suppressed:" << text;
        return;
    }
    _lastEmittedAtUs.insert(text, nowUs);

    QJsonObject message;
    message[QStringLiteral("type")] = kNotificationType;
    message[QStringLiteral("severity")] = _severityString(severity);
    message[QStringLiteral("text")] = text;
    message[QStringLiteral("timeUs")] = static_cast<qint64>(nowUs);
    // vehicleId intentionally omitted -- see PROTOCOL.md §14 field table: none of the current
    // sources carry a vehicle id at the point they are hooked.

    emit notificationReady(message);
}

void NotificationChannel::_handleSpoken(const QString &text)
{
    _emit(_classify(text), text);
}

void NotificationChannel::handleCriticalVehicleMessage(const QString &message)
{
    _emit(Severity::Critical, message);
}

void NotificationChannel::handleAppMessage(const QString &message, const QString &title)
{
    Q_UNUSED(title);
    _emit(_classify(message), message);
}

void NotificationChannel::sendWelcome(quint64 clientToken)
{
    // PROTOCOL.md §14.3: targeted, not broadcast, and exempt from the §14.2 dedup window (does
    // not touch _lastEmittedAtUs) -- every newly-authenticated connection gets its own delivery.
    QJsonObject message;
    message[QStringLiteral("type")] = kNotificationType;
    message[QStringLiteral("severity")] = _severityString(Severity::Info);
    message[QStringLiteral("text")] = QStringLiteral("Ghost bridge ready");
    message[QStringLiteral("timeUs")] = static_cast<qint64>(_bridge ? _bridge->serverTimeUs() : 0);

    emit welcomeReady(clientToken, message);
}
