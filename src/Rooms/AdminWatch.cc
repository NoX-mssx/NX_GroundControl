#include "AdminWatch.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QtCore/QCoreApplication>
#include <QtQml/QJSEngine>

#include "LinkConfiguration.h"
#include "LinkInterface.h"
#include "LinkManager.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "RoomManager.h"
#include "WireGuardTunnel.h"

QGC_LOGGING_CATEGORY(AdminWatchLog, "NX.AdminWatch")

namespace {

AdminWatch* s_instance = nullptr;

}  // namespace

AdminWatch* AdminWatch::instance()
{
    if (!s_instance) {
        (void) new AdminWatch(QCoreApplication::instance());
    }
    return s_instance;
}

AdminWatch* AdminWatch::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    AdminWatch* const watch = instance();
    QJSEngine::setObjectOwnership(watch, QJSEngine::CppOwnership);
    return watch;
}

AdminWatch::AdminWatch(QObject* parent) : QObject(parent)
{
    s_instance = this;
    (void) connect(&_startWatcher, &QFutureWatcher<QString>::finished, this, &AdminWatch::_tunnelStarted);
    (void) connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged, this,
                   &AdminWatch::_activeVehicleChanged);
    if (QCoreApplication::instance()) {
        (void) connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &AdminWatch::stop);
    }
}

AdminWatch::~AdminWatch()
{
    _startWatcher.waitForFinished();
    if (_cancelPending) {
        _releaseTunnel();
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

LinkConfiguration* AdminWatch::watchedConfig()
{
    if (!s_instance || s_instance->_config.isNull() || MultiVehicleManager::instance()->activeVehicle()) {
        return nullptr;
    }
    return s_instance->_config.data();
}

LinkConfiguration* AdminWatch::_configForBoard(const QString& boardId) const
{
    const RoomManager* const rooms = RoomManager::instance();
    if (!rooms || boardId.isEmpty()) {
        return nullptr;
    }
    for (const SharedLinkConfigurationPtr& shared : LinkManager::instance()->linkConfigurationList()) {
        LinkConfiguration* const config = shared.get();
        if (config && !config->fromRoom() && (config->room() == rooms->currentRoom()) &&
            (config->roomBoardId() == boardId)) {
            return config;
        }
    }
    return nullptr;
}

QString AdminWatch::watchProblem(const QString& boardId) const
{
    const LinkConfiguration* const config = _configForBoard(boardId);
    if (!config) {
        return tr("This vehicle is not among your links of this room.");
    }
    if (config->adminWireGuardProfile().isEmpty()) {
        return tr("Link \"%1\" has no admin tunnel. Choose one in the link's settings (Rover → Links).")
            .arg(config->name());
    }
    if (WireGuardTunnel::tunnelForProfile(config->adminWireGuardProfile(), config->room()).isEmpty()) {
        return tr("The admin tunnel \"%1\" of link \"%2\" was not found.")
            .arg(config->adminWireGuardProfile(), config->name());
    }
    if (MultiVehicleManager::instance()->activeVehicle()) {
        return tr("Disconnect from your own vehicle first: the admin view shows one vehicle without its telemetry.");
    }
    return QString();
}

QString AdminWatch::start(const QString& boardId)
{
    if (_starting || _startWatcher.isRunning()) {
        return tr("The admin tunnel is still coming up or going down. Try again in a moment.");
    }
    const QString problem = watchProblem(boardId);
    if (!problem.isEmpty()) {
        return problem;
    }
    LinkConfiguration* const config = _configForBoard(boardId);
    if (_config == config) {
        return QString();
    }
    stop();

    _pending = config;
    _tunnel = WireGuardTunnel::tunnelForProfile(config->adminWireGuardProfile(), config->room());
    _linkName = config->name();
    _boardId = boardId;
    _starting = true;
    qCInfo(AdminWatchLog) << "Watching" << _linkName << "through" << config->adminWireGuardProfile();
    emit changed();

    // Up to half a minute when the service has to start: not on the GUI thread
    const QString tunnel = _tunnel;
    _startWatcher.setFuture(QtConcurrent::run([tunnel]() { return WireGuardTunnel::start(tunnel); }));
    return QString();
}

void AdminWatch::_tunnelStarted()
{
    if (_cancelPending) {
        _cancelPending = false;
        _releaseTunnel();
        return;
    }
    if (!_starting) {
        return;
    }
    _starting = false;
    const QString error = _startWatcher.result();
    if (!error.isEmpty() || _pending.isNull()) {
        qCWarning(AdminWatchLog) << "Admin tunnel of" << _linkName << "did not come up:" << error;
        emit problem(tr("Admin %1: %2").arg(_linkName, error.isEmpty() ? tr("the link was removed") : error), 2);
        _releaseTunnel();
        _pending.clear();
        _linkName.clear();
        _boardId.clear();
        emit changed();
        return;
    }
    _config = _pending;
    _pending.clear();
    emit changed();
    emit watchedConfigChanged();
}

void AdminWatch::stop()
{
    if (!_starting && _config.isNull()) {
        return;
    }
    qCInfo(AdminWatchLog) << "Admin view of" << _linkName << "ends";
    const bool hadConfig = !_config.isNull();
    if (_starting) {
        // The start cannot be broken off: its tunnel is stopped once it returns
        _starting = false;
        _cancelPending = true;
    } else {
        _releaseTunnel();
    }
    _config.clear();
    _pending.clear();
    _linkName.clear();
    _boardId.clear();
    emit changed();
    if (hadConfig) {
        emit watchedConfigChanged();
    }
}

void AdminWatch::_releaseTunnel()
{
    if (_tunnel.isEmpty()) {
        return;
    }
    // A link of this computer on the same tunnel keeps it up
    for (const SharedLinkInterfacePtr& link : LinkManager::instance()->links()) {
        const SharedLinkConfigurationPtr config = link ? link->linkConfiguration() : nullptr;
        if (config && !config->wireGuardProfile().isEmpty() &&
            (WireGuardTunnel::tunnelForProfile(config->wireGuardProfile(), config->room()) == _tunnel)) {
            _tunnel.clear();
            return;
        }
    }
    WireGuardTunnel::stop(_tunnel);
    _tunnel.clear();
}

void AdminWatch::_activeVehicleChanged(Vehicle* vehicle)
{
    // One vehicle on the screen: a vehicle of this computer ends the admin view
    if (vehicle && (active() || _starting)) {
        emit problem(tr("Admin view of %1 closed: a vehicle connected").arg(_linkName), 1);
        stop();
    }
}
