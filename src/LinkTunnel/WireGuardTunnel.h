#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtQmlIntegration/QtQmlIntegration>

/// Runs a link's WireGuard tunnel through WireGuard for Windows.
///
/// A tunnel is registered once, with administrator rights (one UAC prompt), as the Windows service
/// "WireGuardTunnel$<name>": manual start, and startable/stoppable by the interactive user. From then on
/// connecting and disconnecting a link only starts and stops that service, which needs no elevation.
/// The configuration (with its private key) is copied to a directory only SYSTEM and administrators can
/// read; this application never stores the key itself.
class WireGuardTunnel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    /// false when WireGuard for Windows is not installed (or on other platforms)
    Q_PROPERTY(bool         available       READ available      CONSTANT)
    /// Names of the saved tunnel profiles
    Q_PROPERTY(QStringList  profileNames    READ profileNames   NOTIFY profilesChanged)

public:
    explicit WireGuardTunnel(QObject *parent = nullptr);

    enum class State {
        NotInstalled,
        Stopped,
        Running,
    };
    Q_ENUM(State)

    static bool available();

    // Tunnel profiles: a named set of tunnel settings, each registered with Windows as its own tunnel.
    // A link refers to a profile by name. Profiles are stored in WireGuardTunnels.json in the app data
    // directory, with the keys encrypted (see SecretProtector).

    QStringList profileNames() const;

    /// @return the settings of a profile plus its "name", or an empty map
    Q_INVOKABLE QVariantMap profile(const QString &name) const;

    /// Adds a profile, or replaces the one called originalName, and registers its tunnel with Windows
    /// (prompts for elevation).
    /// @return an empty string on success, otherwise a message for the user
    Q_INVOKABLE QString saveProfile(const QString &originalName, const QVariantMap &profile);

    /// Unregisters a profile's tunnel (prompts for elevation) and deletes the profile.
    /// @return an empty string on success, otherwise a message for the user
    Q_INVOKABLE QString deleteProfile(const QString &name);

    /// @return the Windows tunnel name of a profile, empty if there is no such profile
    static QString tunnelForProfile(const QString &profileName);

    /// Tunnel name derived from a link name: WireGuard only allows a limited character set and length.
    Q_INVOKABLE static QString tunnelNameForLink(const QString &linkName);

    /// Registers (or replaces) a tunnel from a .conf file. Prompts for elevation.
    /// @return an empty string on success, otherwise a message for the user
    Q_INVOKABLE static QString install(const QString &tunnelName, const QString &confFilePath);

    /// Tunnel settings as edited in the link dialog:
    ///     { privateKey, address, dns, mtu, peerPublicKey, presharedKey, endpoint, allowedIps, keepalive }
    /// All values are strings, in the same form as in a WireGuard .conf file.

    /// Registers (or replaces) a tunnel from settings. Prompts for elevation.
    /// @return an empty string on success, otherwise a message for the user
    Q_INVOKABLE static QString installFromSettings(const QString &tunnelName, const QVariantMap &settings);

    /// Reads the settings out of a WireGuard .conf file (first [Peer] only).
    /// @return the settings, or an empty map if the file has no [Interface] private key
    Q_INVOKABLE static QVariantMap settingsFromConfFile(const QString &confFilePath);

    /// @return a new private key, empty if WireGuard's wg tool could not be run
    Q_INVOKABLE static QString generatePrivateKey();
    /// @return the public key matching a private key, empty if it is not a valid key
    Q_INVOKABLE static QString publicKey(const QString &privateKey);

    /// Unregisters a tunnel and deletes its stored configuration. Prompts for elevation.
    /// @return an empty string on success, otherwise a message for the user
    Q_INVOKABLE static QString remove(const QString &tunnelName);

    Q_INVOKABLE static State state(const QString &tunnelName);

    /// Starts the tunnel and waits until it is running.
    /// @return an empty string on success, otherwise a message for the user
    static QString start(const QString &tunnelName);
    static void stop(const QString &tunnelName);

signals:
    void profilesChanged();

private:
    static QVariantList &_profiles();
    static int _profileIndex(const QString &name);
    static bool _saveProfiles();
};
