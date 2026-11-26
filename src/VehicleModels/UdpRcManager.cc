#include "UdpRcManager.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDeadlineTimer>
#include <QtCore/QElapsedTimer>
#include <QtCore/QMutex>
#include <QtCore/QSet>
#include <QtNetwork/QHostInfo>
#include <QtQml/QJSEngine>

#include "JoystickManager.h"
#include "LinkConfiguration.h"
#include "LinkInterface.h"
#include "LinkManager.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "QmlObjectListModel.h"
#include "TCPLink.h"
#include "UDPLink.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"
#include "VehicleModelManager.h"

QGC_LOGGING_CATEGORY(UdpRcManagerLog, "NX.UdpRcManager")

namespace {

UdpRcManager* s_instance = nullptr;

QMutex s_controlledMutex;
QSet<const LinkConfiguration*> s_controlled;

constexpr int kPollMs = 250;
/// The last second of neutral and disarm, plus a margin: the tunnel of the link stays up this long
constexpr int kFinishMs = 1150;
constexpr qint64 kArmMismatchMs = 2000;
constexpr qint64 kThrottleOffMs = 2000;
constexpr qint64 kSlowMs = 2000;
/// Below this share of the set rate the stream counts as slow
constexpr double kSlowShare = 0.9;

qint64 monotonicMs()
{
    static QElapsedTimer clock = [] {
        QElapsedTimer started;
        started.start();
        return started;
    }();
    return clock.elapsed();
}

}  // namespace

UdpRcManager* UdpRcManager::instance()
{
    if (!s_instance) {
        (void) new UdpRcManager(QCoreApplication::instance());
    }
    return s_instance;
}

UdpRcManager* UdpRcManager::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    UdpRcManager* const manager = instance();
    QJSEngine::setObjectOwnership(manager, QJSEngine::CppOwnership);
    return manager;
}

UdpRcManager::UdpRcManager(QObject* parent) : QObject(parent)
{
    s_instance = this;
    _pollTimer.setInterval(kPollMs);
    (void) connect(&_pollTimer, &QTimer::timeout, this, &UdpRcManager::_poll);
    _pollTimer.start();
    (void) connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged, this,
                   &UdpRcManager::_poll);
}

UdpRcManager::~UdpRcManager()
{
    // Normally stopAllBlocking() ran at shutdown; this is the abrupt case, the vehicle's failsafe takes over
    _entries.clear();
    qDeleteAll(_finishing);
    _finishing.clear();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool UdpRcManager::controlsLink(const LinkConfiguration* config)
{
    QMutexLocker locker(&s_controlledMutex);
    return config && s_controlled.contains(config);
}

void UdpRcManager::setArmed(bool arm)
{
    Vehicle* const activeVehicle = MultiVehicleManager::instance()->activeVehicle();
    if (!activeVehicle) {
        return;
    }
    const SharedLinkInterfacePtr link = activeVehicle->vehicleLinkManager()->primaryLink().lock();
    const auto it = link ? _entries.find(link->linkConfiguration().get()) : _entries.end();
    if (it == _entries.end()) {
        return;
    }
    qCInfo(UdpRcManagerLog) << (arm ? "Arm" : "Disarm") << "from the screen for" << it->second.name;
    it->second.stream->requestArm(arm);
}

QString UdpRcManager::rcProblem(const QVariantMap& rcControl) const
{
    const UdpRcSettings settings = UdpRcSettings::fromModel(rcControl);
    return settings.enabled ? settings.problem() : QString();
}

QVariantMap UdpRcManager::rcDefaults() const
{
    QVariantMap defaults = UdpRcSettings().toModel();
    defaults.insert(QStringLiteral("type"), QStringLiteral("mavlink"));
    // MAVLink only: a local HotRC remote on the flight controller's SBUS, with a TX12 / HotRC switch on screen
    defaults.insert(QStringLiteral("localRc"), false);
    return defaults;
}

QString UdpRcManager::armBlock() const
{
    switch (_shown.armBlock) {
        case UdpRcLogic::ArmBlock::SwitchOnAtStart:
            return QStringLiteral("switch");
        case UdpRcLogic::ArmBlock::ThrottleNotCentered:
            return QStringLiteral("throttle");
        default:
            return QString();
    }
}

UdpRcSettings UdpRcManager::_settingsFor(const LinkConfiguration* config) const
{
    const VehicleModelManager* const models = VehicleModelManager::instance();
    if (!models || !config || config->vehicleModel().isEmpty()) {
        return UdpRcSettings();
    }
    const QVariantMap model = models->modelForLink(config->vehicleModel(), config->room());
    return UdpRcSettings::fromModel(model.value(QStringLiteral("rcControl")).toMap());
}

QHostAddress UdpRcManager::_targetFor(const LinkConfiguration* config, const UdpRcSettings& settings)
{
    QString host = settings.host;
    if (host.isEmpty()) {
        // The vehicle address of the link: the MAVLink server of a UDP link, the host of a TCP link
        if (const UDPConfiguration* const udp = qobject_cast<const UDPConfiguration*>(config)) {
            const QStringList hosts = udp->hostList();
            if (!hosts.isEmpty()) {
                host = hosts.first().section(QLatin1Char(':'), 0, 0);
            }
        } else if (const TCPConfiguration* const tcp = qobject_cast<const TCPConfiguration*>(config)) {
            host = tcp->host();
        }
    }
    const QHostAddress address(host);
    if (!address.isNull() || host.isEmpty()) {
        return address;
    }
    // A name: looked up without blocking, then everything is evaluated again
    if (_resolved.contains(host)) {
        return _resolved.value(host);
    }
    if (!_resolving.contains(host)) {
        _resolving.insert(host);
        (void) QHostInfo::lookupHost(host, this, [this, host](const QHostInfo& info) {
            _resolving.remove(host);
            QHostAddress found;
            for (const QHostAddress& candidate : info.addresses()) {
                if (candidate.protocol() == QAbstractSocket::IPv4Protocol) {
                    found = candidate;
                    break;
                }
            }
            if (found.isNull()) {
                qCWarning(UdpRcManagerLog) << "RC: cannot resolve" << host << info.errorString();
                return;
            }
            _resolved.insert(host, found);
            _reevaluate();
        });
    }
    return QHostAddress();
}

Vehicle* UdpRcManager::_vehicleFor(const LinkConfiguration* config) const
{
    if (!config) {
        return nullptr;
    }
    QmlObjectListModel* const vehicles = MultiVehicleManager::instance()->vehicles();
    for (int i = 0; i < vehicles->count(); i++) {
        Vehicle* const vehicle = qobject_cast<Vehicle*>(vehicles->get(i));
        if (!vehicle) {
            continue;
        }
        const SharedLinkInterfacePtr link = vehicle->vehicleLinkManager()->primaryLink().lock();
        if (link && (link->linkConfiguration().get() == config)) {
            return vehicle;
        }
    }
    return nullptr;
}

void UdpRcManager::linkConnected(LinkConfiguration* config)
{
    if (config) {
        _warned.remove(config);
    }
    _reevaluate();
}

int UdpRcManager::linkClosing(LinkConfiguration* config)
{
    if (_entries.find(config) != _entries.end()) {
        _stop(config, true);
    }
    // Stopped here or a moment ago by the periodic check: the tunnel waits for the rest of that second
    const qint64 until = _finishingUntil.value(config, -1);
    return (until > monotonicMs()) ? static_cast<int>(until - monotonicMs()) : 0;
}

void UdpRcManager::_reevaluate()
{
    if (_shuttingDown) {
        return;
    }
    LinkManager* const linkManager = LinkManager::instance();
    QSet<LinkConfiguration*> wanted;
    for (const SharedLinkConfigurationPtr& shared : linkManager->linkConfigurationList()) {
        LinkConfiguration* const config = shared.get();
        if (!config || !linkManager->linkSwitchedOn(config)) {
            continue;
        }
        const UdpRcSettings settings = _settingsFor(config);
        if (!settings.enabled) {
            continue;
        }
        const QString problem = settings.problem();
        if (!problem.isEmpty()) {
            if (!_warned.contains(config)) {
                _warned.insert(config);
                qCWarning(UdpRcManagerLog)
                    << "RC settings of model" << config->vehicleModel() << "unusable:" << problem;
                emit this->problem(tr("RC %1: the model's RC settings are not usable: %2").arg(config->name(), problem),
                                   2);
            }
            continue;
        }
        wanted.insert(config);
        auto it = _entries.find(config);
        if (it == _entries.end()) {
            _start(config, settings);
        } else if (!(it->second.settings == settings)) {
            it->second.settings = settings;
            it->second.stream->updateSettings(settings, _targetFor(config, settings));
            qCInfo(UdpRcManagerLog) << "RC settings of" << config->name() << "changed";
        }
    }

    QList<LinkConfiguration*> gone;
    for (const auto& [config, entry] : _entries) {
        if (!wanted.contains(config)) {
            gone.append(config);
        }
    }
    for (LinkConfiguration* const config : std::as_const(gone)) {
        _stop(config, true);
    }
}

void UdpRcManager::_start(LinkConfiguration* config, const UdpRcSettings& settings)
{
    const QHostAddress target = _targetFor(config, settings);
    if (target.isNull()) {
        if (!_warned.contains(config) && !_resolving.contains(settings.host)) {
            _warned.insert(config);
            qCWarning(UdpRcManagerLog) << "No vehicle address for the RC stream of" << config->name();
            emit problem(tr("RC %1: no vehicle address for the RC stream").arg(config->name()), 2);
        }
        return;
    }
    _warned.remove(config);
    Entry entry;
    entry.config = config;
    entry.settings = settings;
    entry.name = config->name();
    entry.stream = std::make_unique<UdpRcStream>(settings, target);
    entry.stream->start(QThread::TimeCriticalPriority);
    _entries.emplace(config, std::move(entry));
    JoystickManager::instance()->setRcStreamPolling(true);
    _updateControlledLinks();
    qCInfo(UdpRcManagerLog) << "RC stream for" << config->name() << "to" << target.toString() << settings.port;
}

void UdpRcManager::_stop(LinkConfiguration* config, bool graceful)
{
    auto it = _entries.find(config);
    if (it == _entries.end()) {
        return;
    }
    UdpRcStream* const stream = it->second.stream.release();
    qCInfo(UdpRcManagerLog) << "RC stream for" << it->second.name
                            << (graceful ? "ends with a second of disarm" : "stops");
    _entries.erase(it);
    if (graceful) {
        stream->stopGracefully();
        _finishingUntil.insert(config, monotonicMs() + kFinishMs);
    } else {
        stream->stopNow();
    }
    _finishing.append(stream);
    (void) connect(stream, &QThread::finished, this, [this, stream, config]() {
        _finishing.removeAll(stream);
        if (_finishingUntil.value(config, 0) <= monotonicMs()) {
            _finishingUntil.remove(config);
        }
        stream->deleteLater();
    });
    if (_entries.empty()) {
        JoystickManager::instance()->setRcStreamPolling(false);
    }
    _updateControlledLinks();
}

void UdpRcManager::stopAllBlocking()
{
    _pollTimer.stop();
    _shuttingDown = true;
    while (!_entries.empty()) {
        _stop(_entries.begin()->first, true);
    }
    // The last second of each stream, so the vehicle is disarmed before the tunnel goes down
    QDeadlineTimer deadline(kFinishMs + 500);
    for (UdpRcStream* const stream : std::as_const(_finishing)) {
        if (!stream->wait(deadline)) {
            stream->stopNow();
            (void) stream->wait(500);
        }
    }
    _finishingUntil.clear();
    emit statusChanged();
}

void UdpRcManager::_updateControlledLinks()
{
    QMutexLocker locker(&s_controlledMutex);
    s_controlled.clear();
    for (const auto& [config, entry] : _entries) {
        s_controlled.insert(config);
    }
}

void UdpRcManager::_poll()
{
    if (_shuttingDown) {
        return;
    }
    _reevaluate();

    const qint64 now = monotonicMs();
    Vehicle* const activeVehicle = MultiVehicleManager::instance()->activeVehicle();
    const LinkConfiguration* activeConfig = nullptr;
    if (activeVehicle) {
        const SharedLinkInterfacePtr link = activeVehicle->vehicleLinkManager()->primaryLink().lock();
        activeConfig = link ? link->linkConfiguration().get() : nullptr;
    }

    // The indicator shows the stream of the active vehicle's link, or the first one
    UdpRcStream::Status shown;
    int shownRate = 0;
    int level = 0;
    bool armMismatch = false;
    QString linkName;
    QString target;
    bool haveShown = false;
    for (auto& [config, entry] : _entries) {
        const UdpRcStream::Status status = entry.stream->status();
        const int entryLevel = _check(entry, status, now);
        if (!haveShown || (config == activeConfig)) {
            shown = status;
            shownRate = qRound(entry.settings.rateHz);
            level = entryLevel;
            armMismatch = entry.armMismatchReported;
            linkName = entry.name;
            target = QStringLiteral("%1:%2").arg(entry.stream->target().toString()).arg(entry.settings.port);
            haveShown = true;
        }
    }

    _shown = shown;
    _shownTargetRate = shownRate;
    _controlsActiveVehicle = activeConfig && controlsLink(activeConfig);
    _level = level;
    _armMismatch = armMismatch;
    _linkName = linkName;
    _target = target;
    emit statusChanged();
}

int UdpRcManager::_check(Entry& entry, const UdpRcStream::Status& status, qint64 now)
{
    const QString name = entry.name;

    // Joystick gone: the stream sent neutral and disarm at once
    if (entry.hadJoystick && !status.joystick) {
        emit problem(tr("RC %1: joystick lost, neutral and disarm sent").arg(name), 2);
    }
    entry.hadJoystick = status.joystick;

    // Arm sent, vehicle not armed after 2 s
    Vehicle* const vehicle = _vehicleFor(entry.config.data());
    if (status.armed && vehicle && !vehicle->armed()) {
        if (entry.armSentSinceMs < 0) {
            entry.armSentSinceMs = now;
        } else if (!entry.armMismatchReported && ((now - entry.armSentSinceMs) >= kArmMismatchMs)) {
            entry.armMismatchReported = true;
            emit problem(tr("RC %1: arm sent, the vehicle did not arm within 2 s").arg(name), 1);
        }
    } else {
        entry.armSentSinceMs = -1;
        entry.armMismatchReported = false;
    }

    // Throttle off centre for a while: the stream sends neutral until it comes to the centre
    if (status.joystick && !status.throttleCentered) {
        if (entry.throttleOffSinceMs < 0) {
            entry.throttleOffSinceMs = now;
        } else if (!entry.throttleReported && ((now - entry.throttleOffSinceMs) >= kThrottleOffMs)) {
            entry.throttleReported = true;
            emit problem(tr("RC %1: throttle not centred, neutral sent until it is").arg(name), 1);
        }
    } else {
        entry.throttleOffSinceMs = -1;
        entry.throttleReported = false;
    }

    // Too slow (a second of samples after the start)
    if (status.running && (status.sent > static_cast<quint64>(entry.settings.rateHz)) &&
        (status.rateHz < entry.settings.rateHz * kSlowShare)) {
        if (entry.slowSinceMs < 0) {
            entry.slowSinceMs = now;
        } else if (!entry.slowReported && ((now - entry.slowSinceMs) >= kSlowMs)) {
            entry.slowReported = true;
            emit problem(tr("RC %1: sending at %2 Hz instead of %3")
                             .arg(name)
                             .arg(qRound(status.rateHz))
                             .arg(qRound(entry.settings.rateHz)),
                         1);
        }
    } else {
        entry.slowSinceMs = -1;
        entry.slowReported = false;
    }

    // Send errors (no route through the tunnel and the like): once, until 5 s pass without any
    if (status.errors > entry.seenErrors) {
        entry.seenErrors = status.errors;
        entry.lastErrorMs = now;
        if (!entry.errorsReported) {
            entry.errorsReported = true;
            emit problem(tr("RC %1: frames are not going out (%2)").arg(name, status.lastError), 1);
        }
    } else if (entry.errorsReported && ((now - entry.lastErrorMs) >= 5000)) {
        entry.errorsReported = false;
    }

    // An arm from the screen the stream did not do
    if (status.screenArmRefusals > entry.seenScreenArmRefusals) {
        entry.seenScreenArmRefusals = status.screenArmRefusals;
        const QString reason = (status.lastScreenArmRefusal == UdpRcLogic::ArmBlock::NoJoystick)
                                   ? tr("no joystick")
                                   : tr("centre the throttle first");
        emit problem(tr("RC %1: no arm: %2").arg(name, reason), 1);
    }

    if (status.armGuard && !entry.guardReported) {
        entry.guardReported = true;
        emit problem(tr("RC %1: the arm channel was to carry a value above 1500; disarm sent instead").arg(name), 2);
    }

    const bool stopped = !status.running && entry.stream->isFinished();
    if (stopped && !entry.stoppedReported) {
        entry.stoppedReported = true;
        emit problem(tr("RC %1: the stream stopped while the link is on").arg(name), 2);
    }

    if (stopped || !status.joystick || status.armGuard) {
        return 2;
    }
    if ((entry.slowSinceMs >= 0) || !status.throttleCentered || (status.armBlock != UdpRcLogic::ArmBlock::None) ||
        entry.armMismatchReported || entry.errorsReported) {
        return 1;
    }
    return 0;
}
