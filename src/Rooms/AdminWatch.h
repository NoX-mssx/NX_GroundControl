#pragma once

#include <QtCore/QFutureWatcher>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtQmlIntegration/QtQmlIntegration>

class LinkConfiguration;
class QJSEngine;
class QQmlEngine;
class Vehicle;

/// NX: the room admin watches a vehicle an operator is driving, through the admin's own WireGuard tunnel of that
/// vehicle's link (LinkConfiguration::adminWireGuardProfile). No MAVLink link is opened, so the operator keeps the
/// vehicle and nothing is sent to it: the main screen shows the link's model cameras, the position and speed from
/// its Starlink dish and the ping through the tunnel (VehicleModelManager, StarlinkPosition and LinkQualityMonitor
/// take watchedConfig() while no vehicle is active). Ends with stop(), or when a vehicle of this computer connects.
class AdminWatch : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Watching: the tunnel is up (or coming up, see starting)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool starting READ starting NOTIFY changed)
    /// The watched link's name and its vehicle id in the room
    Q_PROPERTY(QString linkName READ linkName NOTIFY changed)
    Q_PROPERTY(QString boardId READ boardId NOTIFY changed)

public:
    static AdminWatch* instance();
    static AdminWatch* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    /// The link being watched, once its tunnel is up and while no vehicle is active; GUI thread only
    static LinkConfiguration* watchedConfig();

    bool active() const { return !_config.isNull(); }

    bool starting() const { return _starting; }

    QString linkName() const { return _linkName; }

    QString boardId() const { return _boardId; }

    /// Why the room vehicle @p boardId cannot be watched now, empty if it can
    Q_INVOKABLE QString watchProblem(const QString& boardId) const;
    /// Starts watching the room vehicle @p boardId; the tunnel comes up in the background.
    /// @return why it cannot start, empty when it is starting
    Q_INVOKABLE QString start(const QString& boardId);
    Q_INVOKABLE void stop();

signals:
    void changed();
    /// watchedConfig() changed
    void watchedConfigChanged();
    /// For the "!" of the main screen: level 1 orange, 2 red
    void problem(const QString& text, int level);

private:
    explicit AdminWatch(QObject* parent = nullptr);
    ~AdminWatch() override;

    LinkConfiguration* _configForBoard(const QString& boardId) const;
    void _tunnelStarted();
    void _activeVehicleChanged(Vehicle* vehicle);
    /// Stops the admin tunnel unless a connected link of this computer uses the same one
    void _releaseTunnel();

    QPointer<LinkConfiguration> _config;   ///< set once the tunnel is up
    QPointer<LinkConfiguration> _pending;  ///< the link whose tunnel is coming up
    QString _tunnel;                       ///< WireGuard service of the watched link's admin tunnel
    QString _linkName;
    QString _boardId;
    bool _starting = false;
    bool _cancelPending = false;  ///< left while the tunnel was coming up: stop it once it is up
    QFutureWatcher<QString> _startWatcher;
};
