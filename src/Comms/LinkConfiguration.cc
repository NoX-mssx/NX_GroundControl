#include "LinkConfiguration.h"
#include "QGCLoggingCategory.h"
#ifndef QGC_NO_SERIAL_LINK
#include "SerialLink.h"
#endif
#include "UDPLink.h"
#include "TCPLink.h"
#include "LogReplayLink.h"
#include "BluetoothLink.h"
#ifdef QT_DEBUG
#include "MockLink.h"
#endif

QGC_LOGGING_CATEGORY(LinkConfigurationLog, "Comms.LinkConfiguration")

LinkConfiguration::LinkConfiguration(const QString &name, QObject *parent)
    : QObject(parent)
    , _name(name)
{
    qCDebug(LinkConfigurationLog) << this;
}

LinkConfiguration::LinkConfiguration(const LinkConfiguration *copy, QObject *parent)
    : QObject(parent)
    , _link(copy->_link)
    , _name(copy->name())
    , _dynamic(copy->isDynamic())
    , _autoConnect(copy->isAutoConnect())
    , _highLatency(copy->isHighLatency())
    , _vehicleModel(copy->vehicleModel())
    , _wireGuardProfile(copy->_wireGuardProfile)
    , _pingAddress(copy->_pingAddress)
    , _boardId(copy->_boardId)
    , _throttleLimitEnabled(copy->_throttleLimitEnabled)
    , _pingThreshold1Ms(copy->_pingThreshold1Ms)
    , _throttlePercent1(copy->_throttlePercent1)
    , _pingThreshold2Ms(copy->_pingThreshold2Ms)
    , _throttlePercent2(copy->_throttlePercent2)
{
    qCDebug(LinkConfigurationLog) << this;

    Q_ASSERT(!_name.isEmpty());
}

LinkConfiguration::~LinkConfiguration()
{
    qCDebug(LinkConfigurationLog) << this;
}

void LinkConfiguration::copyFrom(const LinkConfiguration *source)
{
    Q_ASSERT(source);

    setLink(source->_link.lock());
    setName(source->name());
    setDynamic(source->isDynamic());
    setAutoConnect(source->isAutoConnect());
    setHighLatency(source->isHighLatency());
    setVehicleModel(source->vehicleModel());

    _wireGuardProfile = source->_wireGuardProfile;
    _pingAddress = source->_pingAddress;
    _boardId = source->_boardId;
    _throttleLimitEnabled = source->_throttleLimitEnabled;
    _pingThreshold1Ms = source->_pingThreshold1Ms;
    _throttlePercent1 = source->_throttlePercent1;
    _pingThreshold2Ms = source->_pingThreshold2Ms;
    _throttlePercent2 = source->_throttlePercent2;
    emit tunnelSettingsChanged();
}

void LinkConfiguration::loadTunnelSettings(const QSettings &settings, const QString &root)
{
    _wireGuardProfile = settings.value(root + QStringLiteral("/wireguard_profile")).toString();
    _pingAddress = settings.value(root + QStringLiteral("/ping_address")).toString();
    _boardId = settings.value(root + QStringLiteral("/board_id")).toString();
    _throttleLimitEnabled = settings.value(root + QStringLiteral("/throttle_limit")).toBool();
    _pingThreshold1Ms = settings.value(root + QStringLiteral("/ping_threshold_1"), _pingThreshold1Ms).toInt();
    _throttlePercent1 = settings.value(root + QStringLiteral("/throttle_percent_1"), _throttlePercent1).toInt();
    _pingThreshold2Ms = settings.value(root + QStringLiteral("/ping_threshold_2"), _pingThreshold2Ms).toInt();
    _throttlePercent2 = settings.value(root + QStringLiteral("/throttle_percent_2"), _throttlePercent2).toInt();
    emit tunnelSettingsChanged();
}

void LinkConfiguration::saveTunnelSettings(QSettings &settings, const QString &root) const
{
    settings.setValue(root + QStringLiteral("/wireguard_profile"), _wireGuardProfile);
    settings.setValue(root + QStringLiteral("/ping_address"), _pingAddress);
    settings.setValue(root + QStringLiteral("/board_id"), _boardId);
    settings.setValue(root + QStringLiteral("/throttle_limit"), _throttleLimitEnabled);
    settings.setValue(root + QStringLiteral("/ping_threshold_1"), _pingThreshold1Ms);
    settings.setValue(root + QStringLiteral("/throttle_percent_1"), _throttlePercent1);
    settings.setValue(root + QStringLiteral("/ping_threshold_2"), _pingThreshold2Ms);
    settings.setValue(root + QStringLiteral("/throttle_percent_2"), _throttlePercent2);
}

LinkConfiguration *LinkConfiguration::createSettings(int type, const QString &name)
{
    LinkConfiguration *config = nullptr;

    switch (static_cast<LinkType>(type)) {
#ifndef QGC_NO_SERIAL_LINK
    case TypeSerial:
        config = new SerialConfiguration(name);
        break;
#endif
    case TypeUdp:
        config = new UDPConfiguration(name);
        break;
    case TypeTcp:
        config = new TCPConfiguration(name);
        break;
    case TypeBluetooth:
        config = new BluetoothConfiguration(name);
        break;
    case TypeLogReplay:
        config = new LogReplayConfiguration(name);
        break;
#ifdef QT_DEBUG
    case TypeMock:
        config = new MockConfiguration(name);
        break;
#endif
    case TypeLast:
    default:
        break;
    }

    return config;
}

LinkConfiguration *LinkConfiguration::duplicateSettings(const LinkConfiguration *source)
{
    LinkConfiguration *dupe = nullptr;

    switch(source->type()) {
#ifndef QGC_NO_SERIAL_LINK
    case TypeSerial:
        dupe = new SerialConfiguration(qobject_cast<const SerialConfiguration*>(source));
        break;
#endif
    case TypeUdp:
        dupe = new UDPConfiguration(qobject_cast<const UDPConfiguration*>(source));
        break;
    case TypeTcp:
        dupe = new TCPConfiguration(qobject_cast<const TCPConfiguration*>(source));
        break;
    case TypeBluetooth:
        dupe = new BluetoothConfiguration(qobject_cast<const BluetoothConfiguration*>(source));
        break;
    case TypeLogReplay:
        dupe = new LogReplayConfiguration(qobject_cast<const LogReplayConfiguration*>(source));
        break;
#ifdef QT_DEBUG
    case TypeMock:
        dupe = new MockConfiguration(qobject_cast<const MockConfiguration*>(source));
        break;
#endif
    case TypeLast:
    default:
        break;
    }

    return dupe;
}

void LinkConfiguration::setName(const QString &name)
{
    if (name != _name) {
        _name = name;
        emit nameChanged(name);
    }
}

void LinkConfiguration::setLink(const SharedLinkInterfacePtr link)
{
    if (link.get() != this->link()) {
        _link = link;
        emit linkChanged();
        emit linkActiveChanged();

        if (link.get()) {
            (void) connect(link.get(), &LinkInterface::disconnected, this, &LinkConfiguration::linkChanged, Qt::QueuedConnection);
        }
    }
}

void LinkConfiguration::setDynamic(bool dynamic)
{
    if (dynamic != _dynamic) {
        _dynamic = dynamic;
        emit dynamicChanged();
    }
}

void LinkConfiguration::setAutoConnect(bool autoc)
{
    if (autoc != _autoConnect) {
        _autoConnect = autoc;
        emit autoConnectChanged();
        emit linkActiveChanged();
    }
}

void LinkConfiguration::setVehicleModel(const QString &vehicleModel)
{
    if (vehicleModel != _vehicleModel) {
        _vehicleModel = vehicleModel;
        emit vehicleModelChanged();
    }
}

void LinkConfiguration::setHighLatency(bool hl)
{
    if (hl != _highLatency) {
        _highLatency = hl;
        emit highLatencyChanged();
    }
}
