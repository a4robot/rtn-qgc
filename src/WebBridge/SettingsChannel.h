#pragma once

#include <functional>

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QPair>
#include <QtCore/QString>
#include <QtCore/QVariant>

class Fact;
class LinkConfiguration;
class WebBridge;

Q_DECLARE_LOGGING_CATEGORY(SettingsChannelLog)

/// Handles the `settings` surface for WebBridge (PROTOCOL.md §16): app settings get/set against a
/// server-enforced whitelist (§16.2), plus link CRUD (§16.7) against LinkManager. Mirrors
/// FactChannel's architecture (request/response by id, one handleMessage() entry point) but reads/
/// writes SettingsManager Facts instead of vehicle Facts -- no vehicle, no MAVLink round trip, and
/// (per SettingsFact::setRawValue()) always synchronous, so unlike FactChannel this class never
/// needs a pending-request table: every response is produced inline inside handleMessage().
///
/// Client identity: @p clientId is WebBridgeServer's opaque per-connection quint64, exactly as
/// FactChannel/CommandChannel/MissionChannel already use it.
class SettingsChannel : public QObject
{
    Q_OBJECT

public:
    /// @param bridge Not owned; must outlive this object. Used only for serverTimeUs() (§16.5
    ///                settingChanged's `timeUs`) -- settings messages carry no seq/snapshot
    ///                envelope (§3), so makeStreamMessage()/resetSeq() are not used here.
    explicit SettingsChannel(WebBridge *bridge, QObject *parent = nullptr);
    ~SettingsChannel() override;

public slots:
    /// Connects to WebBridgeServer::settingsMessageReceived. Handles getSettings/setSetting/
    /// getLinks/addLink/removeLink/connectLink/disconnectLink (PROTOCOL.md §16.3/§16.4/§16.7).
    void handleMessage(quint64 clientId, const QJsonObject &message);

signals:
    /// Emitted when a response is ready to be sent back to a specific client (settingsValue,
    /// linksValue, linkAck). Connects to WebBridgeServer::reply.
    void responseReady(quint64 clientId, const QJsonObject &message);

    /// Emitted when a protocol-level error occurs (§10). Connects to WebBridgeServer::replyError.
    void errorReady(quint64 clientId, const QString &code, const QString &message, bool retryable, const QString &id);

    /// One `settingChanged` message (§16.5), already built. Connects to
    /// WebBridgeServer::broadcastAll -- no subscription, every authenticated client, same fan-out
    /// shape as `notification` (§14) and `tick` (§11.1).
    void settingChangedReady(const QJsonObject &message);

    /// Emitted whenever an accepted setSetting (or any other in-process write picked up by the
    /// same Fact::rawValueChanged listener that drives settingChangedReady()) lands on a `Video`
    /// group whitelist entry (PROTOCOL.md §16.6). QGCApplication.cc connects this directly to
    /// GhostVideoSource::start() -- the live-apply nudge the feature depends on; this class stays
    /// GStreamer-free and has no idea GhostVideoSource exists, matching NotificationChannel's
    /// established pattern of wiring QGCApplication-adjacent concerns externally rather than
    /// pulling the dependency into this module.
    void videoSettingChanged();

private slots:
    /// Connected once (in the constructor) to every whitelisted Fact's rawValueChanged(). Looks the
    /// sender up in _factToEntry to build and emit the §16.5 settingChanged broadcast, and (for the
    /// Video group) emits videoSettingChanged().
    void _onWhitelistedFactChanged(const QVariant &value);

private:
    /// One §16.2 whitelist row: (group, name) -> a Fact obtained lazily from SettingsManager. A
    /// std::function (not a raw member-function pointer) so the whitelist table can be built with
    /// simple lambdas over SettingsManager::instance()'s per-group accessors (videoSettings(),
    /// appSettings(), autoConnectSettings()) without needing a common base type across the three
    /// SettingsGroup subclasses' differently-named Fact accessor methods.
    struct WhitelistEntry {
        QString group;
        QString name;
        std::function<Fact *()> getter;
    };

    /// Builds the §16.2 whitelist table (called once, from the constructor) and connects every
    /// entry's Fact to _onWhitelistedFactChanged(). SettingsManager is already fully initialized
    /// by the time WebBridge wiring runs in QGCApplication.cc (SettingsManager::init() happens
    /// during core init, well before the `--bridge-port` block), so there is no lazy-init need
    /// here the way FactChannel/MissionChannel need for per-vehicle Facts that only exist once a
    /// Vehicle connects.
    void _buildWhitelist();

    /// Looks up a whitelist entry's live Fact*, or nullptr if (group, name) is not whitelisted.
    Fact *_resolveWhitelisted(const QString &group, const QString &name) const;

    /// Builds one §16.3 `settings[]` array entry ({group, name, value, meta}) for @p fact.
    static QJsonObject _describeSetting(const QString &group, const QString &name, const Fact *fact);

    /// Validates @p value against @p fact's cooked type / enumValues (§16.4's VALUE_OUT_OF_RANGE
    /// condition). Returns true (and leaves @p outReason untouched) iff acceptable.
    static bool _valueAcceptable(const Fact *fact, const QJsonValue &value, QString &outReason);

    void _handleGetSettings(quint64 clientId, const QJsonObject &request);
    void _handleSetSetting(quint64 clientId, const QJsonObject &request);
    void _handleGetLinks(quint64 clientId, const QJsonObject &request);
    void _handleAddLink(quint64 clientId, const QJsonObject &request);
    void _handleRemoveLink(quint64 clientId, const QJsonObject &request);
    // NOTE: parameter named doConnect, not connect -- QObject::connect() is a member of this class
    // and an unqualified `connect` parameter would shadow it for the duration of the function body.
    void _handleConnectLink(quint64 clientId, const QJsonObject &request, bool doConnect);

    /// §16.7 getLinks: describes one configured link ({name, type, connected, autoConnect,
    /// config}).
    static QJsonObject _describeLink(const LinkConfiguration *config);

    /// Finds a configured link by name (case-sensitive, §16.7's addLink uniqueness rule), or
    /// nullptr.
    static LinkConfiguration *_findLinkByName(const QString &name);

    /// Builds a `linkAck` (§16.7).
    static QJsonObject _makeLinkAck(const QString &id, bool accepted, const QString &name, const QString &reason = QString());

    WebBridge *_bridge = nullptr;    ///< Not owned
    QList<WhitelistEntry> _whitelist;
    QHash<Fact *, QPair<QString, QString>> _factToEntry;    ///< Fact* -> (group, name), for _onWhitelistedFactChanged()
};
