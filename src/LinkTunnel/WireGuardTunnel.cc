#include "WireGuardTunnel.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryFile>
#include <QtCore/QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

QGC_LOGGING_CATEGORY(WireGuardTunnelLog, "LinkTunnel.WireGuardTunnel")

namespace {

#ifdef Q_OS_WIN

QString wireGuardExePath()
{
    return QDir(qEnvironmentVariable("ProgramFiles")).filePath(QStringLiteral("WireGuard/wireguard.exe"));
}

QString serviceName(const QString &tunnelName)
{
    return QStringLiteral("WireGuardTunnel$") + tunnelName;
}

/// Single-quoted PowerShell literal
QString psQuote(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

/// Runs a PowerShell script elevated and waits for it.
/// @return the script's exit code, or -1 if it could not be started (e.g. the UAC prompt was declined)
int runElevated(const QString &script)
{
    const QString parameters = QStringLiteral("-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand %1")
        .arg(QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(script.utf16()),
                                            script.size() * static_cast<qsizetype>(sizeof(char16_t))).toBase64()));

    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
    info.lpVerb = L"runas";
    info.lpFile = L"powershell.exe";
    const std::wstring wideParameters = parameters.toStdWString();
    info.lpParameters = wideParameters.c_str();
    info.nShow = SW_HIDE;

    if (!ShellExecuteExW(&info) || !info.hProcess) {
        return -1;
    }

    (void) WaitForSingleObject(info.hProcess, 60000);
    DWORD exitCode = 1;
    (void) GetExitCodeProcess(info.hProcess, &exitCode);
    (void) CloseHandle(info.hProcess);
    return static_cast<int>(exitCode);
}

/// Opens the tunnel service with the given access, or returns nullptr.
SC_HANDLE openService(const QString &tunnelName, DWORD access)
{
    const SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) {
        return nullptr;
    }
    const SC_HANDLE service = OpenServiceW(manager, serviceName(tunnelName).toStdWString().c_str(), access);
    (void) CloseServiceHandle(manager);
    return service;
}

DWORD serviceState(SC_HANDLE service)
{
    SERVICE_STATUS status = {};
    return QueryServiceStatus(service, &status) ? status.dwCurrentState : SERVICE_STOPPED;
}

#endif // Q_OS_WIN

/// Runs WireGuard's wg tool, feeding it @p input, and returns its trimmed output (empty on failure).
QString runWg(const QStringList &arguments, const QByteArray &input = QByteArray())
{
#ifdef Q_OS_WIN
    const QString wgPath = QDir(qEnvironmentVariable("ProgramFiles")).filePath(QStringLiteral("WireGuard/wg.exe"));
    if (!QFileInfo::exists(wgPath)) {
        return QString();
    }

    constexpr int kTimeoutMs = 5000;
    QProcess process;
    process.start(wgPath, arguments);
    if (!process.waitForStarted(kTimeoutMs)) {
        return QString();
    }
    if (!input.isEmpty()) {
        (void) process.write(input);
    }
    process.closeWriteChannel();
    if (!process.waitForFinished(kTimeoutMs) || (process.exitStatus() != QProcess::NormalExit) || (process.exitCode() != 0)) {
        process.kill();
        return QString();
    }
    return QString::fromLatin1(process.readAllStandardOutput()).trimmed();
#else
    Q_UNUSED(arguments);
    Q_UNUSED(input);
    return QString();
#endif
}

QString settingValue(const QVariantMap &settings, const char *key)
{
    return settings.value(QLatin1String(key)).toString().trimmed();
}

// WireGuard for Windows accepts [a-zA-Z0-9_=+.-]{1,32} as a tunnel name.
constexpr int kMaxTunnelNameLength = 32;

} // namespace

WireGuardTunnel::WireGuardTunnel(QObject *parent)
    : QObject(parent)
{
}

bool WireGuardTunnel::available()
{
#ifdef Q_OS_WIN
    return QFileInfo::exists(wireGuardExePath());
#else
    return false;
#endif
}

QString WireGuardTunnel::tunnelNameForLink(const QString &linkName)
{
    static const QRegularExpression disallowed(QStringLiteral("[^a-zA-Z0-9_=+.-]"));

    // Non-ASCII link names (e.g. Cyrillic) reduce to underscores, so a hash of the full name keeps
    // tunnels of different links apart.
    QString name = linkName;
    name.replace(disallowed, QStringLiteral("_"));
    const QString suffix = QStringLiteral("-%1").arg(qHash(linkName) & 0xFFFF, 4, 16, QLatin1Char('0'));
    const QString prefix = QStringLiteral("NX-");
    name.truncate(kMaxTunnelNameLength - prefix.size() - suffix.size());
    return prefix + name + suffix;
}

QString WireGuardTunnel::install(const QString &tunnelName, const QString &confFilePath)
{
#ifdef Q_OS_WIN
    if (!available()) {
        return tr("WireGuard for Windows is not installed.");
    }
    if (tunnelName.isEmpty() || !QFileInfo::exists(confFilePath)) {
        return tr("The WireGuard configuration file was not found.");
    }

    // S-1-5-18 = SYSTEM, S-1-5-32-544 = Administrators: SIDs because the group names are localized.
    // The service DACL is the Windows default plus start/stop (RP/WP) for interactive users (IU).
    const QString script = QStringLiteral(
        "$ErrorActionPreference = 'Stop'\n"
        "$name = %1\n"
        "$service = 'WireGuardTunnel$' + $name\n"
        "$wireguard = %2\n"
        "$dir = Join-Path $env:ProgramData 'NX-GroundControl\\tunnels'\n"
        "New-Item -ItemType Directory -Force -Path $dir | Out-Null\n"
        "& icacls.exe $dir /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' | Out-Null\n"
        "if (Get-Service -Name $service -ErrorAction SilentlyContinue) {\n"
        "    & $wireguard /uninstalltunnelservice $name\n"
        "    for ($i = 0; $i -lt 50 -and (Get-Service -Name $service -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 200 }\n"
        "}\n"
        "$conf = Join-Path $dir ($name + '.conf')\n"
        "Copy-Item -LiteralPath %3 -Destination $conf -Force\n"
        "& $wireguard /installtunnelservice $conf\n"
        "for ($i = 0; $i -lt 50 -and -not (Get-Service -Name $service -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 200 }\n"
        "if (-not (Get-Service -Name $service -ErrorAction SilentlyContinue)) { exit 2 }\n"
        "& sc.exe config $service start= demand | Out-Null\n"
        "& sc.exe sdset $service 'D:(A;;CCLCSWRPWPDTLOCRRC;;;SY)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)(A;;CCLCSWRPWPLOCRRC;;;IU)(A;;CCLCSWLOCRRC;;;SU)' | Out-Null\n"
        "if ($LASTEXITCODE -ne 0) { exit 3 }\n"
        "Stop-Service -Name $service -Force -ErrorAction SilentlyContinue\n"
        "exit 0\n")
        .arg(psQuote(tunnelName), psQuote(QDir::toNativeSeparators(wireGuardExePath())),
             psQuote(QDir::toNativeSeparators(confFilePath)));

    const int exitCode = runElevated(script);
    if (exitCode == -1) {
        return tr("Administrator rights are required to register the tunnel.");
    }
    if (exitCode != 0) {
        qCWarning(WireGuardTunnelLog) << "Tunnel install script failed" << tunnelName << exitCode;
        return tr("Registering the WireGuard tunnel failed (code %1). Check that the file is a valid WireGuard configuration.").arg(exitCode);
    }
    return QString();
#else
    Q_UNUSED(tunnelName);
    Q_UNUSED(confFilePath);
    return tr("WireGuard tunnels are only supported on Windows.");
#endif
}

QString WireGuardTunnel::installFromSettings(const QString &tunnelName, const QVariantMap &settings)
{
    const QString privateKey = settingValue(settings, "privateKey");
    const QString address = settingValue(settings, "address");
    const QString peerPublicKey = settingValue(settings, "peerPublicKey");
    const QString endpoint = settingValue(settings, "endpoint");
    const QString allowedIps = settingValue(settings, "allowedIps");
    if (privateKey.isEmpty() || address.isEmpty() || peerPublicKey.isEmpty() || endpoint.isEmpty() || allowedIps.isEmpty()) {
        return tr("Fill in the address, the peer public key, the endpoint and the allowed IPs.");
    }

    QStringList lines = {
        QStringLiteral("[Interface]"),
        QStringLiteral("PrivateKey = ") + privateKey,
        QStringLiteral("Address = ") + address,
    };
    if (const QString dns = settingValue(settings, "dns"); !dns.isEmpty()) {
        lines.append(QStringLiteral("DNS = ") + dns);
    }
    if (const QString mtu = settingValue(settings, "mtu"); !mtu.isEmpty()) {
        lines.append(QStringLiteral("MTU = ") + mtu);
    }
    lines.append(QString());
    lines.append(QStringLiteral("[Peer]"));
    lines.append(QStringLiteral("PublicKey = ") + peerPublicKey);
    if (const QString presharedKey = settingValue(settings, "presharedKey"); !presharedKey.isEmpty()) {
        lines.append(QStringLiteral("PresharedKey = ") + presharedKey);
    }
    lines.append(QStringLiteral("Endpoint = ") + endpoint);
    lines.append(QStringLiteral("AllowedIPs = ") + allowedIps);
    if (const QString keepalive = settingValue(settings, "keepalive"); !keepalive.isEmpty()) {
        lines.append(QStringLiteral("PersistentKeepalive = ") + keepalive);
    }

    // The elevated installer copies this into the protected tunnel directory; the temporary copy, which
    // holds the private key, is removed again when this function returns.
    QTemporaryFile confFile(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("nx-tunnel-XXXXXX.conf")));
    if (!confFile.open()) {
        return tr("Could not write the tunnel configuration.");
    }
    (void) confFile.write(lines.join(QLatin1Char('\n')).toUtf8());
    (void) confFile.write("\n");
    confFile.close();

    return install(tunnelName, confFile.fileName());
}

QVariantMap WireGuardTunnel::settingsFromConfFile(const QString &confFilePath)
{
    QFile file(confFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QVariantMap();
    }

    static const QHash<QString, QString> interfaceKeys = {
        {QStringLiteral("privatekey"), QStringLiteral("privateKey")},
        {QStringLiteral("address"), QStringLiteral("address")},
        {QStringLiteral("dns"), QStringLiteral("dns")},
        {QStringLiteral("mtu"), QStringLiteral("mtu")},
    };
    static const QHash<QString, QString> peerKeys = {
        {QStringLiteral("publickey"), QStringLiteral("peerPublicKey")},
        {QStringLiteral("presharedkey"), QStringLiteral("presharedKey")},
        {QStringLiteral("endpoint"), QStringLiteral("endpoint")},
        {QStringLiteral("allowedips"), QStringLiteral("allowedIps")},
        {QStringLiteral("persistentkeepalive"), QStringLiteral("keepalive")},
    };

    QVariantMap settings;
    const QHash<QString, QString> *sectionKeys = nullptr;
    int peerCount = 0;

    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.section(QLatin1Char('#'), 0, 0).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QLatin1Char('['))) {
            const QString section = line.toLower();
            if (section == QLatin1String("[interface]")) {
                sectionKeys = &interfaceKeys;
            } else if (section == QLatin1String("[peer]")) {
                // Only the first peer is represented in the settings.
                sectionKeys = (peerCount++ == 0) ? &peerKeys : nullptr;
            } else {
                sectionKeys = nullptr;
            }
            continue;
        }
        if (!sectionKeys) {
            continue;
        }

        // Split on the first '=' only: base64 keys end in '='.
        const qsizetype separator = line.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            continue;
        }
        const QString key = line.left(separator).trimmed().toLower();
        const QString value = line.mid(separator + 1).trimmed();
        const auto it = sectionKeys->constFind(key);
        if (it == sectionKeys->constEnd()) {
            continue;
        }
        // Address/AllowedIPs/DNS may be repeated across lines; a .conf treats that as one list.
        const QString existing = settings.value(*it).toString();
        settings[*it] = existing.isEmpty() ? value : (existing + QStringLiteral(", ") + value);
    }

    return settings.contains(QStringLiteral("privateKey")) ? settings : QVariantMap();
}

QString WireGuardTunnel::generatePrivateKey()
{
    return runWg({QStringLiteral("genkey")});
}

QString WireGuardTunnel::publicKey(const QString &privateKey)
{
    if (privateKey.trimmed().isEmpty()) {
        return QString();
    }
    return runWg({QStringLiteral("pubkey")}, privateKey.trimmed().toLatin1() + '\n');
}

QString WireGuardTunnel::remove(const QString &tunnelName)
{
#ifdef Q_OS_WIN
    if (tunnelName.isEmpty() || (state(tunnelName) == State::NotInstalled)) {
        return QString();
    }

    const QString script = QStringLiteral(
        "$name = %1\n"
        "& %2 /uninstalltunnelservice $name\n"
        "$conf = Join-Path $env:ProgramData ('NX-GroundControl\\tunnels\\' + $name + '.conf')\n"
        "Remove-Item -LiteralPath $conf -Force -ErrorAction SilentlyContinue\n"
        "exit 0\n")
        .arg(psQuote(tunnelName), psQuote(QDir::toNativeSeparators(wireGuardExePath())));

    if (runElevated(script) != 0) {
        return tr("Administrator rights are required to remove the tunnel.");
    }
    return QString();
#else
    Q_UNUSED(tunnelName);
    return QString();
#endif
}

WireGuardTunnel::State WireGuardTunnel::state(const QString &tunnelName)
{
#ifdef Q_OS_WIN
    if (tunnelName.isEmpty()) {
        return State::NotInstalled;
    }
    const SC_HANDLE service = openService(tunnelName, SERVICE_QUERY_STATUS);
    if (!service) {
        return State::NotInstalled;
    }
    const DWORD current = serviceState(service);
    (void) CloseServiceHandle(service);
    return (current == SERVICE_STOPPED) ? State::Stopped : State::Running;
#else
    Q_UNUSED(tunnelName);
    return State::NotInstalled;
#endif
}

QString WireGuardTunnel::start(const QString &tunnelName)
{
#ifdef Q_OS_WIN
    const SC_HANDLE service = openService(tunnelName, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!service) {
        return tr("The WireGuard tunnel of this link is not registered. Open the link settings and import its configuration again.");
    }

    QString error;
    if (!StartServiceW(service, 0, nullptr) && (GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)) {
        error = tr("The WireGuard tunnel could not be started (error %1).").arg(GetLastError());
    } else {
        // The tunnel has to be up before the link opens its socket.
        constexpr int kPollMs = 100;
        constexpr int kMaxPolls = 100;
        int polls = 0;
        while ((serviceState(service) != SERVICE_RUNNING) && (polls++ < kMaxPolls)) {
            QThread::msleep(kPollMs);
        }
        if (serviceState(service) != SERVICE_RUNNING) {
            error = tr("The WireGuard tunnel did not come up.");
        }
    }

    (void) CloseServiceHandle(service);
    return error;
#else
    Q_UNUSED(tunnelName);
    return tr("WireGuard tunnels are only supported on Windows.");
#endif
}

void WireGuardTunnel::stop(const QString &tunnelName)
{
#ifdef Q_OS_WIN
    const SC_HANDLE service = openService(tunnelName, SERVICE_STOP);
    if (!service) {
        return;
    }
    SERVICE_STATUS status = {};
    (void) ControlService(service, SERVICE_CONTROL_STOP, &status);
    (void) CloseServiceHandle(service);
#else
    Q_UNUSED(tunnelName);
#endif
}
