#pragma once

#include <QtCore/QDeadlineTimer>
#include <QtCore/QElapsedTimer>
#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtCore/QVariantMap>
#include <QtQmlIntegration/QtQmlIntegration>

class LinkInterface;

/// \brief Interface holding link specific settings.
///
class LinkConfiguration : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
    Q_MOC_INCLUDE("LinkInterface.h")

    Q_PROPERTY(QString          name            READ name           WRITE setName           NOTIFY nameChanged)
    Q_PROPERTY(LinkInterface    *link           READ link                                   NOTIFY linkChanged)
    Q_PROPERTY(bool             linkActive      READ linkActive                             NOTIFY linkActiveChanged)
    Q_PROPERTY(LinkType         linkType        READ type                                   CONSTANT)
    Q_PROPERTY(bool             dynamic         READ isDynamic      WRITE setDynamic        NOTIFY dynamicChanged)
    Q_PROPERTY(bool             autoConnect     READ isAutoConnect  WRITE setAutoConnect    NOTIFY autoConnectChanged)
    Q_PROPERTY(QString          settingsURL     READ settingsURL                            CONSTANT)
    Q_PROPERTY(QString          settingsTitle   READ settingsTitle                          CONSTANT)
    Q_PROPERTY(bool             highLatency     READ isHighLatency  WRITE setHighLatency    NOTIFY highLatencyChanged)
    Q_PROPERTY(QString          vehicleModel    READ vehicleModel   WRITE setVehicleModel   NOTIFY vehicleModelChanged)
    /// Name of the WireGuard tunnel profile (see WireGuardTunnel) started before this link connects and
    /// stopped when it disconnects, empty for none
    Q_PROPERTY(QString          wireGuardProfile    MEMBER _wireGuardProfile    NOTIFY tunnelSettingsChanged)
    /// Address pinged to measure the link round trip time (the vehicle's router inside the tunnel), empty for none
    Q_PROPERTY(QString          pingAddress         MEMBER _pingAddress         NOTIFY tunnelSettingsChanged)
    /// Scale joystick throttle down while the ping is above the thresholds below
    Q_PROPERTY(bool             throttleLimitEnabled    MEMBER _throttleLimitEnabled    NOTIFY tunnelSettingsChanged)
    Q_PROPERTY(int              pingThreshold1Ms        MEMBER _pingThreshold1Ms        NOTIFY tunnelSettingsChanged)
    Q_PROPERTY(int              throttlePercent1        MEMBER _throttlePercent1        NOTIFY tunnelSettingsChanged)
    Q_PROPERTY(int              pingThreshold2Ms        MEMBER _pingThreshold2Ms        NOTIFY tunnelSettingsChanged)
    Q_PROPERTY(int              throttlePercent2        MEMBER _throttlePercent2        NOTIFY tunnelSettingsChanged)

public:
    LinkConfiguration(const QString &name, QObject *parent = nullptr);
    LinkConfiguration(const LinkConfiguration *copy, QObject *parent = nullptr);
    virtual ~LinkConfiguration();

    QString name() const { return _name; }
    void setName(const QString &name);

    LinkInterface *link() const { return _link.lock().get(); }
    void setLink(const std::shared_ptr<LinkInterface> link);

    /// True while the link is connected or being kept connected by auto-reconnect.
    /// Stays true across reconnect attempts so UI doesn't flicker between retries.
    bool linkActive() const { return (link() != nullptr) || (_autoConnect && _autoConnectStarted && !_suppressAutoReconnect); }

    /// Is this a dynamic configuration?
    ///     @return True if not persisted
    bool isDynamic() const { return _dynamic; }

    /// Set if this is this a dynamic configuration. (decided at runtime)
    void setDynamic(bool dynamic = true);

    /// Is this a forwarding link configuration?
    ///     @return True if forwarding
    bool isForwarding() const { return _forwarding; }

    /// Set if this is this a forwarding link configuration. (decided at runtime)
    void setForwarding(bool forwarding = true) { _forwarding = forwarding; };

    /// Name of the vehicle model (see VehicleModelManager) describing the vehicle behind this link, empty for none.
    QString vehicleModel() const { return _vehicleModel; }
    void setVehicleModel(const QString &vehicleModel);

    QString wireGuardProfile() const { return _wireGuardProfile; }
    QString pingAddress() const { return _pingAddress; }
    bool throttleLimitEnabled() const { return _throttleLimitEnabled; }
    int pingThreshold1Ms() const { return _pingThreshold1Ms; }
    int throttlePercent1() const { return _throttlePercent1; }
    int pingThreshold2Ms() const { return _pingThreshold2Ms; }
    int throttlePercent2() const { return _throttlePercent2; }

    /// Loads/saves the tunnel, ping and throttle limit settings shared by all link types.
    void loadTunnelSettings(const QSettings &settings, const QString &root);
    void saveTunnelSettings(QSettings &settings, const QString &root) const;

    bool isAutoConnect() const { return _autoConnect; }

    /// Set if this is this an Auto Connect configuration.
    virtual void setAutoConnect(bool autoc = true);

    bool suppressAutoReconnect() const { return _suppressAutoReconnect; }
    void setSuppressAutoReconnect(bool suppress) {
        if (_suppressAutoReconnect != suppress) { _suppressAutoReconnect = suppress; emit linkActiveChanged(); }
    }

    bool autoConnectStarted() const { return _autoConnectStarted; }
    void setAutoConnectStarted(bool started) {
        if (_autoConnectStarted != started) { _autoConnectStarted = started; emit linkActiveChanged(); }
    }

    bool reconnectReady() const { return _nextReconnect.hasExpired(); }
    void noteReconnectAttempt() {
        const int exp = qMin(_reconnectAttempts, 16);
        _reconnectAttempts = qMin(_reconnectAttempts + 1, 17);
        _nextReconnect = QDeadlineTimer(qMin(_reconnectBaseMs << exp, _reconnectMaxMs));
    }
    void resetReconnectBackoff() { _reconnectAttempts = 0; _nextReconnect = QDeadlineTimer(); }
    void noteConnected() { _connectedTimer.start(); }
    /// Reset backoff only if the link stayed up long enough to count as working.
    void noteDisconnected() {
        if (_connectedTimer.isValid() && (_connectedTimer.elapsed() >= _reconnectStableMs)) {
            resetReconnectBackoff();
        }
        _connectedTimer.invalidate();
    }

    /// Is this a High Latency configuration?
    ///     @return True if this is an High Latency configuration (link with large delays).
    bool isHighLatency() const { return _highLatency; }

    /// Set if this is this an High Latency configuration.
    void setHighLatency(bool hl = false);

    /// Copy instance data, When manipulating data, you create a copy of the configuration using the copy constructor,
    /// edit it and then transfer its content to the original using this method.
    ///     @param[in] source The source instance (the edited copy)
    virtual void copyFrom(const LinkConfiguration *source);

    /// The link types supported by QGC
    /// Any changes here MUST be reflected in LinkManager::linkTypeStrings()
    enum LinkType {
#ifndef QGC_NO_SERIAL_LINK
        TypeSerial,     ///< Serial Link
#endif
        TypeUdp,        ///< UDP Link
        TypeTcp,        ///< TCP Link
        TypeBluetooth,  ///< Bluetooth Link
#ifdef QT_DEBUG
        TypeMock,       ///< Mock Link for Unitesting
#endif
        TypeLogReplay,
        TypeLast        // Last type value (type >= TypeLast == invalid)
    };
    Q_ENUM(LinkType)

    /// Connection type, pure virtual method returning one of the -TypeXxx types above.
    ///     @return The type of links these settings belong to.
    virtual LinkType type() const = 0;

    /// Load settings, Pure virtual method telling the instance to load its configuration.
    ///     @param[in] settings The QSettings instance to use
    ///     @param[in] root The root path of the setting.
    virtual void loadSettings(QSettings &settings, const QString &root) = 0;

    /// Save settings, Pure virtual method telling the instance to save its configuration.
    ///     @param[in] settings The QSettings instance to use
    ///     @param[in] root The root path of the setting.
    virtual void saveSettings(QSettings &settings, const QString &root) const = 0;

    /// Settings URL, Pure virtual method providing the URL for the (QML) settings dialog
    virtual QString settingsURL() const = 0;

    /// Settings Title, Pure virtual method providing the Title for the (QML) settings dialog
    virtual QString settingsTitle() const = 0;

    /// Configuration Factory to create new link configuration instance based on the given type.
    ///     @return A new instance of the given type
    static LinkConfiguration *createSettings(int type, const QString &name);

    /// Duplicate configuration instance. Helper method to create a new instance copy for editing.
    ///     @return A new copy of the given settings instance
    static LinkConfiguration *duplicateSettings(const LinkConfiguration *source);

    /// Root path for QSettings
    ///     @return The root path of the settings.
    static QString settingsRoot() { return QStringLiteral("LinkConfigurations"); }

signals:
    void nameChanged(const QString &name);
    void linkChanged();
    void linkActiveChanged();
    void dynamicChanged();
    void autoConnectChanged();
    void highLatencyChanged();
    void vehicleModelChanged();
    void tunnelSettingsChanged();

protected:
    std::weak_ptr<LinkInterface> _link; ///< Link currently using this configuration (if any)

private:
    QString _name;
    bool _dynamic = false;     ///< A connection added automatically and not persistent (unless it's edited).
    bool _forwarding = false;  ///< Automatically added Mavlink forwarding connection
    bool _autoConnect = false; ///< This connection is started automatically at boot
    bool _highLatency = false;
    QString _vehicleModel;
    QString _wireGuardProfile;
    QString _pingAddress;
    bool _throttleLimitEnabled = false;
    int _pingThreshold1Ms = 300;
    int _throttlePercent1 = 50;
    int _pingThreshold2Ms = 600;
    int _throttlePercent2 = 25;
    bool _suppressAutoReconnect = false; ///< User disconnected; skip auto-reconnect until manually reconnected (runtime only)
    bool _autoConnectStarted = false;    ///< Link was started at boot or manually connected; gates timer reconnect (runtime only)
    int _reconnectAttempts = 0;          ///< Consecutive failed auto-reconnect attempts (runtime only)
    QDeadlineTimer _nextReconnect;       ///< Earliest time the next auto-reconnect may run; default-expired = ready now
    QElapsedTimer _connectedTimer;       ///< Measures how long the current link has stayed connected (runtime only)

    static constexpr int _reconnectBaseMs = 1000;
    static constexpr int _reconnectMaxMs = 5000;
    static constexpr int _reconnectStableMs = 2000; ///< Min connected duration to count as a working link
};

typedef std::shared_ptr<LinkConfiguration> SharedLinkConfigurationPtr;
typedef std::weak_ptr<LinkConfiguration> WeakLinkConfigurationPtr;
