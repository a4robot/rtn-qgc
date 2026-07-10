#include "SettingsChannel.h"

#include <algorithm>

#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>
#include <QtCore/QSet>

#include "Fact.h"
#include "FactMetaData.h"
#include "LinkConfiguration.h"
#include "LinkInterface.h"
#include "LinkManager.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "AppSettings.h"
#include "AutoConnectSettings.h"
#include "VideoSettings.h"
#include "TCPLink.h"
#include "UDPLink.h"
#include "WebBridge.h"
#ifndef QGC_NO_SERIAL_LINK
#include "SerialLink.h"
#endif

QGC_LOGGING_CATEGORY(SettingsChannelLog, "WebBridge.SettingsChannel")

namespace {

const QString kGetSettings = QStringLiteral("getSettings");
const QString kSetSetting = QStringLiteral("setSetting");
const QString kGetLinks = QStringLiteral("getLinks");
const QString kAddLink = QStringLiteral("addLink");
const QString kRemoveLink = QStringLiteral("removeLink");
const QString kConnectLink = QStringLiteral("connectLink");
const QString kDisconnectLink = QStringLiteral("disconnectLink");
const QString kVideoGroup = QStringLiteral("Video");

/// PROTOCOL.md §16.3: "meta.type ∈ string|int|double|bool" -- coarser than FactChannel.cc's
/// getFactTypeString() (§6's meta.type keeps MAVLink param-storage width like uint8/float since
/// vehicle params round-trip over the wire at that width; app settings never do).
QString settingsTypeString(FactMetaData::ValueType_t type)
{
    switch (type) {
    case FactMetaData::valueTypeBool:
        return QStringLiteral("bool");
    case FactMetaData::valueTypeString:
        return QStringLiteral("string");
    case FactMetaData::valueTypeFloat:
    case FactMetaData::valueTypeDouble:
    case FactMetaData::valueTypeElapsedTimeInSeconds:
        return QStringLiteral("double");
    default:
        return QStringLiteral("int");
    }
}

/// PROTOCOL.md §16.7: "type ∈ serial|udp|tcp|bluetooth|mock|logReplay|other".
QString linkTypeString(LinkConfiguration::LinkType type)
{
    switch (type) {
#ifndef QGC_NO_SERIAL_LINK
    case LinkConfiguration::TypeSerial:
        return QStringLiteral("serial");
#endif
    case LinkConfiguration::TypeUdp:
        return QStringLiteral("udp");
    case LinkConfiguration::TypeTcp:
        return QStringLiteral("tcp");
#ifndef QGC_NO_BLUETOOTH_LINK
    case LinkConfiguration::TypeBluetooth:
        return QStringLiteral("bluetooth");
#endif
    case LinkConfiguration::TypeLogReplay:
        return QStringLiteral("logReplay");
#if defined(QT_DEBUG) || defined(QGC_ENABLE_MOCKLINK)
    case LinkConfiguration::TypeMock:
        return QStringLiteral("mock");
#endif
    default:
        return QStringLiteral("other");
    }
}

} // namespace

SettingsChannel::SettingsChannel(WebBridge *bridge, QObject *parent)
    : QObject(parent)
    , _bridge(bridge)
{
    if (!_bridge) {
        qCWarning(SettingsChannelLog) << "constructed with null WebBridge";
    }

    _buildWhitelist();

    qCDebug(SettingsChannelLog) << this << "bridge" << static_cast<void *>(_bridge) << "whitelist size" << _whitelist.size();
}

SettingsChannel::~SettingsChannel()
{
    qCDebug(SettingsChannelLog) << this;
}

void SettingsChannel::_buildWhitelist()
{
    SettingsManager *settingsManager = SettingsManager::instance();
    if (!settingsManager) {
        qCWarning(SettingsChannelLog) << "no SettingsManager instance -- whitelist stays empty";
        return;
    }

    // PROTOCOL.md §16.2: the normative whitelist. Every entry here MUST have a matching row in
    // that table -- this is the single source of truth server-side; the doc table is documentation
    // of this list, not the other way around.
    if (VideoSettings *videoSettings = settingsManager->videoSettings()) {
        _whitelist.append({kVideoGroup, QStringLiteral("videoSource"), [videoSettings] { return videoSettings->videoSource(); }});
        _whitelist.append({kVideoGroup, QStringLiteral("udpUrl"), [videoSettings] { return videoSettings->udpUrl(); }});
        _whitelist.append({kVideoGroup, QStringLiteral("rtspUrl"), [videoSettings] { return videoSettings->rtspUrl(); }});
        _whitelist.append({kVideoGroup, QStringLiteral("tcpUrl"), [videoSettings] { return videoSettings->tcpUrl(); }});
        _whitelist.append({kVideoGroup, QStringLiteral("streamEnabled"), [videoSettings] { return videoSettings->streamEnabled(); }});
        _whitelist.append({kVideoGroup, QStringLiteral("lowLatencyMode"), [videoSettings] { return videoSettings->lowLatencyMode(); }});
    }
    if (AppSettings *appSettings = settingsManager->appSettings()) {
        _whitelist.append({QStringLiteral("App"), QStringLiteral("defaultMissionItemAltitude"), [appSettings] { return appSettings->defaultMissionItemAltitude(); }});
        _whitelist.append({QStringLiteral("App"), QStringLiteral("offlineEditingFirmwareClass"), [appSettings] { return appSettings->offlineEditingFirmwareClass(); }});
        _whitelist.append({QStringLiteral("App"), QStringLiteral("offlineEditingVehicleClass"), [appSettings] { return appSettings->offlineEditingVehicleClass(); }});
        _whitelist.append({QStringLiteral("App"), QStringLiteral("offlineEditingCruiseSpeed"), [appSettings] { return appSettings->offlineEditingCruiseSpeed(); }});
        _whitelist.append({QStringLiteral("App"), QStringLiteral("offlineEditingHoverSpeed"), [appSettings] { return appSettings->offlineEditingHoverSpeed(); }});
        _whitelist.append({QStringLiteral("App"), QStringLiteral("offlineEditingAscentSpeed"), [appSettings] { return appSettings->offlineEditingAscentSpeed(); }});
        _whitelist.append({QStringLiteral("App"), QStringLiteral("offlineEditingDescentSpeed"), [appSettings] { return appSettings->offlineEditingDescentSpeed(); }});
    }
    if (AutoConnectSettings *autoConnectSettings = settingsManager->autoConnectSettings()) {
        _whitelist.append({QStringLiteral("AutoConnect"), QStringLiteral("autoConnectUDP"), [autoConnectSettings] { return autoConnectSettings->autoConnectUDP(); }});
        _whitelist.append({QStringLiteral("AutoConnect"), QStringLiteral("autoConnectPixhawk"), [autoConnectSettings] { return autoConnectSettings->autoConnectPixhawk(); }});
        _whitelist.append({QStringLiteral("AutoConnect"), QStringLiteral("autoConnectSiKRadio"), [autoConnectSettings] { return autoConnectSettings->autoConnectSiKRadio(); }});
        _whitelist.append({QStringLiteral("AutoConnect"), QStringLiteral("autoConnectRTKGPS"), [autoConnectSettings] { return autoConnectSettings->autoConnectRTKGPS(); }});
        _whitelist.append({QStringLiteral("AutoConnect"), QStringLiteral("udpListenPort"), [autoConnectSettings] { return autoConnectSettings->udpListenPort(); }});
    }

    for (const WhitelistEntry &entry : std::as_const(_whitelist)) {
        Fact *fact = entry.getter();
        if (!fact) {
            qCWarning(SettingsChannelLog) << "whitelist entry" << entry.group << entry.name << "resolved to a null Fact -- skipped";
            continue;
        }
        _factToEntry.insert(fact, qMakePair(entry.group, entry.name));
        // PROTOCOL.md §16.5: fires on ANY write to a whitelisted Fact, not only ones this channel
        // itself originated (e.g. a different client's setSetting) -- so all connected clients
        // observe the same current state, matching notification's "no subscription, everyone
        // sees it" broadcast model.
        connect(fact, &Fact::rawValueChanged, this, &SettingsChannel::_onWhitelistedFactChanged);
    }
}

Fact *SettingsChannel::_resolveWhitelisted(const QString &group, const QString &name) const
{
    for (const WhitelistEntry &entry : _whitelist) {
        if (entry.group == group && entry.name == name) {
            return entry.getter();
        }
    }
    return nullptr;
}

QJsonObject SettingsChannel::_describeSetting(const QString &group, const QString &name, const Fact *fact)
{
    QJsonObject entry;
    entry[QStringLiteral("group")] = group;
    entry[QStringLiteral("name")] = name;
    entry[QStringLiteral("value")] = QJsonValue::fromVariant(fact->rawValue());

    QJsonObject meta;
    meta[QStringLiteral("type")] = settingsTypeString(fact->type());
    const QVariantList enumValues = fact->enumValues();
    if (!enumValues.isEmpty()) {
        QJsonArray enumArray;
        for (const QVariant &value : enumValues) {
            enumArray.append(QJsonValue::fromVariant(value));
        }
        meta[QStringLiteral("enumValues")] = enumArray;
    }
    entry[QStringLiteral("meta")] = meta;

    return entry;
}

bool SettingsChannel::_valueAcceptable(const Fact *fact, const QJsonValue &value, QString &outReason)
{
    switch (fact->type()) {
    case FactMetaData::valueTypeBool:
        if (!value.isBool()) {
            outReason = QStringLiteral("value must be a boolean");
            return false;
        }
        break;
    case FactMetaData::valueTypeString: {
        if (!value.isString()) {
            outReason = QStringLiteral("value must be a string");
            return false;
        }
        const QVariantList enumValues = fact->enumValues();
        if (!enumValues.isEmpty() && !enumValues.contains(QVariant(value.toString()))) {
            outReason = QStringLiteral("value is not one of meta.enumValues");
            return false;
        }
        break;
    }
    default: {
        // Every remaining ValueType_t (int/uint of every width, float, double,
        // elapsedTimeInSeconds) is a plain JSON number on the wire (§16.3's "int|double" coarsens
        // them exactly the way getSettings's meta.type already does).
        if (!value.isDouble()) {
            outReason = QStringLiteral("value must be a number");
            return false;
        }
        // Numeric enums exist too (e.g. App.offlineEditingFirmwareClass/offlineEditingVehicleClass,
        // §16.2 -- their JSON metadata declares enumStrings/enumValues just like videoSource does),
        // so enum membership is enforced here the same way the string branch above enforces it.
        // Compared numerically (not via QVariant equality) since JSON numbers decode as double
        // while FactMetaData's enumValues may be stored as int/uint/double depending on the Fact's
        // own type.
        const QVariantList enumValues = fact->enumValues();
        if (!enumValues.isEmpty()) {
            const double target = value.toDouble();
            const bool matches = std::any_of(enumValues.cbegin(), enumValues.cend(), [target](const QVariant &candidate) {
                return candidate.toDouble() == target;
            });
            if (!matches) {
                outReason = QStringLiteral("value is not one of meta.enumValues");
                return false;
            }
        }
        break;
    }
    }
    return true;
}

void SettingsChannel::handleMessage(quint64 clientId, const QJsonObject &message)
{
    const QString type = message.value(QStringLiteral("type")).toString();

    if (type == kGetSettings) {
        _handleGetSettings(clientId, message);
    } else if (type == kSetSetting) {
        _handleSetSetting(clientId, message);
    } else if (type == kGetLinks) {
        _handleGetLinks(clientId, message);
    } else if (type == kAddLink) {
        _handleAddLink(clientId, message);
    } else if (type == kRemoveLink) {
        _handleRemoveLink(clientId, message);
    } else if (type == kConnectLink) {
        _handleConnectLink(clientId, message, true);
    } else if (type == kDisconnectLink) {
        _handleConnectLink(clientId, message, false);
    } else {
        // Defensive only -- WebBridgeServer routes exactly these seven types here (mirrors
        // FactChannel's getParam/setParam routing).
        const QString id = message.value(QStringLiteral("id")).toString();
        emit errorReady(clientId, QStringLiteral("UNKNOWN_TYPE"), QStringLiteral("Unrecognized settings message type: %1").arg(type), false, id);
    }
}

void SettingsChannel::_handleGetSettings(quint64 clientId, const QJsonObject &request)
{
    const QString id = request.value(QStringLiteral("id")).toString();
    const bool hasGroup = request.contains(QStringLiteral("group"));
    const QString group = request.value(QStringLiteral("group")).toString();

    static const QSet<QString> kKnownGroups = { kVideoGroup, QStringLiteral("App"), QStringLiteral("AutoConnect") };
    if (hasGroup && !kKnownGroups.contains(group)) {
        emit errorReady(clientId, QStringLiteral("UNKNOWN_SETTINGS_GROUP"), QStringLiteral("Unknown settings group: %1").arg(group), false, id);
        return;
    }

    QJsonArray settings;
    for (const WhitelistEntry &entry : _whitelist) {
        if (hasGroup && entry.group != group) {
            continue;
        }
        Fact *fact = entry.getter();
        if (!fact) {
            continue;
        }
        settings.append(_describeSetting(entry.group, entry.name, fact));
    }

    QJsonObject response;
    response[QStringLiteral("type")] = QStringLiteral("settingsValue");
    response[QStringLiteral("id")] = id;
    response[QStringLiteral("settings")] = settings;
    emit responseReady(clientId, response);
}

void SettingsChannel::_handleSetSetting(quint64 clientId, const QJsonObject &request)
{
    const QString id = request.value(QStringLiteral("id")).toString();
    const QString group = request.value(QStringLiteral("group")).toString();
    const QString name = request.value(QStringLiteral("name")).toString();

    if (group.isEmpty() || name.isEmpty() || !request.contains(QStringLiteral("value"))) {
        emit errorReady(clientId, QStringLiteral("BAD_MESSAGE"), QStringLiteral("setSetting requires group, name, and value"), false, id);
        return;
    }

    Fact *fact = _resolveWhitelisted(group, name);
    if (!fact) {
        // PROTOCOL.md §16.4: "unknown group, or unknown name within a known group" -> the same
        // SETTING_NOT_ALLOWED code -- the whitelist table is the sole source of truth, so both
        // failure shapes look identical to a client.
        emit errorReady(clientId, QStringLiteral("SETTING_NOT_ALLOWED"), QStringLiteral("Setting not in whitelist: %1.%2").arg(group, name), false, id);
        return;
    }

    const QJsonValue value = request.value(QStringLiteral("value"));
    QString reason;
    if (!_valueAcceptable(fact, value, reason)) {
        emit errorReady(clientId, QStringLiteral("VALUE_OUT_OF_RANGE"), reason, false, id);
        return;
    }

    // SettingsFact::setRawValue() writes through to QSettings synchronously (PROTOCOL.md §16.1) --
    // by the time this call returns, rawValue() already reflects the new value, and
    // _onWhitelistedFactChanged() has already fired the §16.5 broadcast (direct connection, same
    // thread) and, for the Video group, videoSettingChanged() (§16.6).
    fact->setRawValue(value.toVariant());

    QJsonArray settings;
    settings.append(_describeSetting(group, name, fact));

    QJsonObject response;
    response[QStringLiteral("type")] = QStringLiteral("settingsValue");
    response[QStringLiteral("id")] = id;
    response[QStringLiteral("settings")] = settings;
    emit responseReady(clientId, response);
}

void SettingsChannel::_onWhitelistedFactChanged(const QVariant &value)
{
    Q_UNUSED(value);

    auto *fact = qobject_cast<Fact *>(sender());
    if (!fact) {
        return;
    }
    const auto entryIt = _factToEntry.constFind(fact);
    if (entryIt == _factToEntry.constEnd()) {
        return;
    }
    const QString &group = entryIt.value().first;
    const QString &name = entryIt.value().second;

    QJsonObject message = _describeSetting(group, name, fact);
    message[QStringLiteral("type")] = QStringLiteral("settingChanged");
    message[QStringLiteral("timeUs")] = static_cast<qint64>(_bridge ? _bridge->serverTimeUs() : 0);
    emit settingChangedReady(message);

    if (group == kVideoGroup) {
        // PROTOCOL.md §16.6: the live-apply nudge. This class stays GStreamer-free -- see the
        // signal's doc comment in the header for why QGCApplication.cc, not this class, connects
        // it to GhostVideoSource::start().
        emit videoSettingChanged();
    }
}

void SettingsChannel::_handleGetLinks(quint64 clientId, const QJsonObject &request)
{
    const QString id = request.value(QStringLiteral("id")).toString();

    QJsonArray links;
    LinkManager *linkManager = LinkManager::instance();
    if (linkManager) {
        for (const SharedLinkConfigurationPtr &config : linkManager->linkConfigurationsList()) {
            if (config) {
                links.append(_describeLink(config.get()));
            }
        }
    }

    QJsonObject response;
    response[QStringLiteral("type")] = QStringLiteral("linksValue");
    response[QStringLiteral("id")] = id;
    response[QStringLiteral("links")] = links;
    emit responseReady(clientId, response);
}

QJsonObject SettingsChannel::_describeLink(const LinkConfiguration *config)
{
    QJsonObject entry;
    entry[QStringLiteral("name")] = config->name();
    entry[QStringLiteral("type")] = linkTypeString(config->type());
    entry[QStringLiteral("connected")] = config->link() != nullptr && config->link()->isConnected();
    entry[QStringLiteral("autoConnect")] = config->isAutoConnect();

    QJsonObject linkConfig;
    if (const auto *udp = qobject_cast<const UDPConfiguration *>(config)) {
        linkConfig[QStringLiteral("listenPort")] = udp->localPort();
        QJsonArray targetHosts;
        for (const QString &host : udp->hostList()) {
            targetHosts.append(host);
        }
        linkConfig[QStringLiteral("targetHosts")] = targetHosts;
    } else if (const auto *tcp = qobject_cast<const TCPConfiguration *>(config)) {
        linkConfig[QStringLiteral("host")] = tcp->host();
        linkConfig[QStringLiteral("port")] = tcp->port();
#ifndef QGC_NO_SERIAL_LINK
    } else if (const auto *serial = qobject_cast<const SerialConfiguration *>(config)) {
        linkConfig[QStringLiteral("port")] = serial->portName();
        linkConfig[QStringLiteral("baud")] = serial->baud();
#endif
    }
    entry[QStringLiteral("config")] = linkConfig;

    return entry;
}

LinkConfiguration *SettingsChannel::_findLinkByName(const QString &name)
{
    LinkManager *linkManager = LinkManager::instance();
    if (!linkManager) {
        return nullptr;
    }
    for (const SharedLinkConfigurationPtr &config : linkManager->linkConfigurationsList()) {
        if (config && config->name() == name) {
            return config.get();
        }
    }
    return nullptr;
}

QJsonObject SettingsChannel::_makeLinkAck(const QString &id, bool accepted, const QString &name, const QString &reason)
{
    QJsonObject ack;
    ack[QStringLiteral("type")] = QStringLiteral("linkAck");
    ack[QStringLiteral("id")] = id;
    ack[QStringLiteral("status")] = accepted ? QStringLiteral("accepted") : QStringLiteral("rejected");
    ack[QStringLiteral("name")] = name;
    if (!accepted) {
        ack[QStringLiteral("reason")] = reason;
    }
    return ack;
}

void SettingsChannel::_handleAddLink(quint64 clientId, const QJsonObject &request)
{
    const QString id = request.value(QStringLiteral("id")).toString();
    const QJsonObject config = request.value(QStringLiteral("config")).toObject();
    const QString typeStr = config.value(QStringLiteral("type")).toString();
    const QString name = config.value(QStringLiteral("name")).toString();

    if (name.isEmpty()) {
        emit responseReady(clientId, _makeLinkAck(id, false, name, QStringLiteral("config.name is required")));
        return;
    }
    if (_findLinkByName(name)) {
        emit responseReady(clientId, _makeLinkAck(id, false, name, QStringLiteral("A link named \"%1\" already exists").arg(name)));
        return;
    }

    LinkManager *linkManager = LinkManager::instance();
    if (!linkManager) {
        emit responseReady(clientId, _makeLinkAck(id, false, name, QStringLiteral("LinkManager not available")));
        return;
    }

    // PROTOCOL.md §16.7 addLink: only serial|udp|tcp are constructible over the wire. Initialized
    // to a harmless default -- every path below either overwrites it before use or returns first.
    LinkConfiguration::LinkType linkType = LinkConfiguration::TypeUdp;
#ifndef QGC_NO_SERIAL_LINK
    if (typeStr == QStringLiteral("serial")) {
        linkType = LinkConfiguration::TypeSerial;
    } else
#endif
    if (typeStr == QStringLiteral("udp")) {
        linkType = LinkConfiguration::TypeUdp;
    } else if (typeStr == QStringLiteral("tcp")) {
        linkType = LinkConfiguration::TypeTcp;
    } else {
        emit responseReady(clientId, _makeLinkAck(id, false, name, QStringLiteral("config.type must be one of serial|udp|tcp (got \"%1\")").arg(typeStr)));
        return;
    }

    LinkConfiguration *newConfig = linkManager->createConfiguration(static_cast<int>(linkType), name);
    if (!newConfig) {
        emit responseReady(clientId, _makeLinkAck(id, false, name, QStringLiteral("Failed to create link configuration")));
        return;
    }

    if (auto *udp = qobject_cast<UDPConfiguration *>(newConfig)) {
        if (config.contains(QStringLiteral("listenPort"))) {
            udp->setLocalPort(static_cast<quint16>(config.value(QStringLiteral("listenPort")).toInt()));
        }
        // Optional: same "host:port" string shape getLinks reports for config.targetHosts --
        // UDPConfiguration::addHost(QString) already parses that shape itself (UDPLink.cc), so no
        // manual splitting is needed here.
        const QJsonArray targetHosts = config.value(QStringLiteral("targetHosts")).toArray();
        for (const QJsonValue &hostEntry : targetHosts) {
            const QString hostPort = hostEntry.toString();
            if (!hostPort.isEmpty()) {
                udp->addHost(hostPort);
            }
        }
    } else if (auto *tcp = qobject_cast<TCPConfiguration *>(newConfig)) {
        tcp->setHost(config.value(QStringLiteral("host")).toString());
        if (config.contains(QStringLiteral("port"))) {
            tcp->setPort(static_cast<quint16>(config.value(QStringLiteral("port")).toInt()));
        }
#ifndef QGC_NO_SERIAL_LINK
    } else if (auto *serial = qobject_cast<SerialConfiguration *>(newConfig)) {
        serial->setPortName(config.value(QStringLiteral("port")).toString());
        if (config.contains(QStringLiteral("baud"))) {
            serial->setBaud(config.value(QStringLiteral("baud")).toInt());
        }
#endif
    }

    // PROTOCOL.md §16.7: "persisted immediately ... but not connected".
    linkManager->endCreateConfiguration(newConfig);

    emit responseReady(clientId, _makeLinkAck(id, true, name));
}

void SettingsChannel::_handleRemoveLink(quint64 clientId, const QJsonObject &request)
{
    const QString id = request.value(QStringLiteral("id")).toString();
    const QString name = request.value(QStringLiteral("name")).toString();

    LinkConfiguration *config = _findLinkByName(name);
    if (!config) {
        emit responseReady(clientId, _makeLinkAck(id, false, name, QStringLiteral("No link configuration named \"%1\"").arg(name)));
        return;
    }

    LinkManager::instance()->removeConfiguration(config); // also disconnects if currently connected
    emit responseReady(clientId, _makeLinkAck(id, true, name));
}

void SettingsChannel::_handleConnectLink(quint64 clientId, const QJsonObject &request, bool doConnect)
{
    const QString id = request.value(QStringLiteral("id")).toString();
    const QString name = request.value(QStringLiteral("name")).toString();

    LinkConfiguration *config = _findLinkByName(name);
    if (!config) {
        emit responseReady(clientId, _makeLinkAck(id, false, name, QStringLiteral("No link configuration named \"%1\"").arg(name)));
        return;
    }

    // PROTOCOL.md §16.7: fire-and-forget -- both paths dispatch onto the link's own worker thread;
    // "accepted" means "requested", not "now connected/disconnected".
    if (doConnect) {
        LinkManager::instance()->createConnectedLink(config);
    } else if (LinkInterface *link = config->link()) {
        link->disconnect();
    }

    emit responseReady(clientId, _makeLinkAck(id, true, name));
}
