#include "UdpRcStream.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QtEndian>
#include <QtNetwork/QUdpSocket>
#include <algorithm>
#include <cmath>
#include <thread>

#include "LinkQualityMonitor.h"
#include "QGCLoggingCategory.h"
#include "RcJoystickFeed.h"

#ifdef Q_OS_WIN
// windows.h first: timeapi.h needs its types
// clang-format off
#include <windows.h>
#include <timeapi.h>
// clang-format on
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

QGC_LOGGING_CATEGORY(UdpRcStreamLog, "NX.UdpRcStream")

// ---- settings -------------------------------------------------------------------------------------------------

namespace {

int intOf(const QVariantMap& map, const char* key, int fallback)
{
    bool ok = false;
    const int value = map.value(QLatin1String(key)).toInt(&ok);
    return ok ? value : fallback;
}

double realOf(const QVariantMap& map, const char* key, double fallback)
{
    bool ok = false;
    const double value = map.value(QLatin1String(key)).toDouble(&ok);
    return (ok && std::isfinite(value)) ? value : fallback;
}

bool boolOf(const QVariantMap& map, const char* key, bool fallback)
{
    return map.contains(QLatin1String(key)) ? map.value(QLatin1String(key)).toBool() : fallback;
}

}  // namespace

UdpRcSettings UdpRcSettings::fromModel(const QVariantMap& rcControl)
{
    UdpRcSettings settings;
    settings.enabled = rcControl.value(QStringLiteral("type")).toString() == QLatin1String("udp");
    settings.host = rcControl.value(QStringLiteral("host")).toString().trimmed();
    settings.port = static_cast<quint16>(std::clamp(intOf(rcControl, "port", settings.port), 1, 65535));
    settings.sourcePort =
        static_cast<quint16>(std::clamp(intOf(rcControl, "sourcePort", settings.sourcePort), 0, 65535));
    settings.rateHz = realOf(rcControl, "rateHz", settings.rateHz);
    const QVariantList rest = rcControl.value(QStringLiteral("rest")).toList();
    for (int i = 0; (i < rest.size()) && (i < kChannels); i++) {
        bool ok = false;
        const int value = rest.at(i).toInt(&ok);
        if (ok) {
            settings.rest[static_cast<size_t>(i)] = static_cast<quint16>(std::clamp(value, 0, 65535));
        }
    }
    settings.throttleChannel = intOf(rcControl, "throttleChannel", settings.throttleChannel);
    settings.steerChannel = intOf(rcControl, "steerChannel", settings.steerChannel);
    settings.throttleAxis = intOf(rcControl, "throttleAxis", settings.throttleAxis);
    settings.steerAxis = intOf(rcControl, "steerAxis", settings.steerAxis);
    settings.invertThrottle = boolOf(rcControl, "invertThrottle", settings.invertThrottle);
    settings.invertSteer = boolOf(rcControl, "invertSteer", settings.invertSteer);
    settings.deadzone = realOf(rcControl, "deadzone", settings.deadzone);
    settings.throttleLimit = realOf(rcControl, "throttleLimitPct", settings.throttleLimit * 100.0) / 100.0;
    settings.steerLimit = realOf(rcControl, "steerLimitPct", settings.steerLimit * 100.0) / 100.0;
    settings.slew = realOf(rcControl, "slew", settings.slew);
    settings.armAxis = intOf(rcControl, "armAxis", settings.armAxis);
    settings.armThreshold = realOf(rcControl, "armThreshold", settings.armThreshold);
    settings.armChannel = intOf(rcControl, "armChannel", settings.armChannel);
    settings.armValue = static_cast<quint16>(std::clamp(intOf(rcControl, "armValue", settings.armValue), 0, 65535));
    settings.disarmValue =
        static_cast<quint16>(std::clamp(intOf(rcControl, "disarmValue", settings.disarmValue), 0, 65535));
    return settings;
}

QVariantMap UdpRcSettings::toModel() const
{
    QVariantList restList;
    for (const quint16 value : rest) {
        restList.append(value);
    }
    return {
        {QStringLiteral("type"), enabled ? QStringLiteral("udp") : QStringLiteral("mavlink")},
        {QStringLiteral("host"), host},
        {QStringLiteral("port"), port},
        {QStringLiteral("sourcePort"), sourcePort},
        {QStringLiteral("rateHz"), rateHz},
        {QStringLiteral("rest"), restList},
        {QStringLiteral("throttleChannel"), throttleChannel},
        {QStringLiteral("steerChannel"), steerChannel},
        {QStringLiteral("throttleAxis"), throttleAxis},
        {QStringLiteral("steerAxis"), steerAxis},
        {QStringLiteral("invertThrottle"), invertThrottle},
        {QStringLiteral("invertSteer"), invertSteer},
        {QStringLiteral("deadzone"), deadzone},
        {QStringLiteral("throttleLimitPct"), std::round(throttleLimit * 1000.0) / 10.0},
        {QStringLiteral("steerLimitPct"), std::round(steerLimit * 1000.0) / 10.0},
        {QStringLiteral("slew"), slew},
        {QStringLiteral("armAxis"), armAxis},
        {QStringLiteral("armThreshold"), armThreshold},
        {QStringLiteral("armChannel"), armChannel},
        {QStringLiteral("armValue"), armValue},
        {QStringLiteral("disarmValue"), disarmValue},
    };
}

QString UdpRcSettings::problem() const
{
    const auto channelOk = [](int channel) { return (channel >= 1) && (channel <= kChannels); };
    if (!channelOk(throttleChannel) || !channelOk(steerChannel) || !channelOk(armChannel)) {
        return QCoreApplication::translate("UdpRcSettings", "channels are 1 to 16");
    }
    if ((throttleChannel == steerChannel) || (throttleChannel == armChannel) || (steerChannel == armChannel)) {
        return QCoreApplication::translate("UdpRcSettings", "throttle, steering and arm need different channels");
    }
    for (const quint16 value : rest) {
        if ((value < kCrsfMin) || (value > kCrsfMax)) {
            return QCoreApplication::translate("UdpRcSettings", "rest values are 172 to 1811");
        }
    }
    if ((armValue < kCrsfMin) || (armValue > kArmChannelMax) || (disarmValue < kCrsfMin) ||
        (disarmValue > kArmChannelMax)) {
        return QCoreApplication::translate("UdpRcSettings",
                                           "arm and disarm values are 172 to 1500 (1811 is arm + Auto)");
    }
    if ((rateHz < 50.0) || (rateHz > 500.0)) {
        return QCoreApplication::translate("UdpRcSettings", "rate is 50 to 500 Hz");
    }
    if ((throttleAxis < 0) || (steerAxis < 0) || (throttleAxis == steerAxis) || (armAxis == throttleAxis) ||
        (armAxis == steerAxis) || (armAxis < -1)) {
        return QCoreApplication::translate("UdpRcSettings", "throttle, steering and arm need different joystick axes");
    }
    if ((deadzone < 0.0) || (deadzone >= 0.5) || (throttleLimit < 0.0) || (throttleLimit > 1.0) || (steerLimit < 0.0) ||
        (steerLimit > 1.0) || (slew <= 0.0) || (armThreshold <= -1.0) || (armThreshold >= 1.0)) {
        return QCoreApplication::translate("UdpRcSettings", "dead zone, limits, slew or arm threshold out of range");
    }
    return QString();
}

// ---- per-frame logic ------------------------------------------------------------------------------------------

UdpRcLogic::UdpRcLogic(const UdpRcSettings& settings) : _settings(settings) {}

quint16 UdpRcLogic::toCrsf(double value)
{
    // round() in Python rounds a half to the even neighbour, as nearbyint does in the default rounding mode
    const double crsf =
        std::nearbyint(UdpRcSettings::kCrsfMid + value * (UdpRcSettings::kCrsfMax - UdpRcSettings::kCrsfMid));
    return static_cast<quint16>(std::clamp(crsf, double(UdpRcSettings::kCrsfMin), double(UdpRcSettings::kCrsfMax)));
}

double UdpRcLogic::shapeAxis(double value, double deadzone, bool invert)
{
    if (std::abs(value) < deadzone) {
        return 0.0;
    }
    // Outside the dead zone the stroke is stretched back to 0..1, so the command does not jump at its edge
    const double sign = (value > 0) ? 1.0 : -1.0;
    const double shaped = sign * (std::abs(value) - deadzone) / (1.0 - deadzone);
    return invert ? -shaped : shaped;
}

double UdpRcLogic::slewTo(double current, double target, double step)
{
    if (current * target < 0.0) {
        // Reversing: the old direction stops at once, the new one grows like any other
        current = 0.0;
    }
    if (std::abs(target) <= std::abs(current)) {
        return target;
    }
    return (target > current) ? std::min(target, current + step) : std::max(target, current - step);
}

QByteArray UdpRcLogic::frame(const std::array<quint16, UdpRcSettings::kChannels>& channels)
{
    QByteArray bytes(UdpRcSettings::kChannels * 2, Qt::Uninitialized);
    for (int i = 0; i < UdpRcSettings::kChannels; i++) {
        qToBigEndian<quint16>(channels[static_cast<size_t>(i)], bytes.data() + i * 2);
    }
    return bytes;
}

std::array<quint16, UdpRcSettings::kChannels> UdpRcLogic::_build(double throttle, double steer, bool armed,
                                                                 bool& armGuard) const
{
    std::array<quint16, UdpRcSettings::kChannels> channels = _settings.rest;
    const size_t armIndex = static_cast<size_t>(_settings.armChannel - 1);
    channels[armIndex] = armed ? _settings.armValue : _settings.disarmValue;
    channels[static_cast<size_t>(_settings.throttleChannel - 1)] = toCrsf(throttle);
    channels[static_cast<size_t>(_settings.steerChannel - 1)] = toCrsf(steer);
    // Last guard: 1811 in the arm channel is arm + Auto. Whatever led there, disarm goes instead.
    armGuard = channels[armIndex] > UdpRcSettings::kArmChannelMax;
    if (armGuard) {
        channels[armIndex] = std::min<quint16>(_settings.disarmValue, UdpRcSettings::kArmChannelMax);
    }
    return channels;
}

std::array<quint16, UdpRcSettings::kChannels> UdpRcLogic::neutralDisarmed() const
{
    bool armGuard = false;
    return _build(0.0, 0.0, false, armGuard);
}

UdpRcLogic::Output UdpRcLogic::step(const Input& input, double nowSeconds, double throttleScale)
{
    Output output;
    double targetThrottle = 0.0;
    double targetSteer = 0.0;
    const auto axis = [&input](int index) {
        return ((index >= 0) && (index < input.axes.size())) ? input.axes.at(index) : 0.0;
    };

    if (!input.joystick) {
        // No joystick: neutral and disarm, and everything to be shown again once it is back
        _throttleOk = false;
        _armed = false;
        _armReady = false;
    } else {
        if (input.generation != _generation) {
            _generation = input.generation;
            _throttleOk = false;
            _armed = false;
            _armReady = false;
        }
        const double rawThrottle =
            shapeAxis(axis(_settings.throttleAxis), _settings.deadzone, _settings.invertThrottle);
        targetThrottle = rawThrottle * _settings.throttleLimit * std::clamp(throttleScale, 0.0, 1.0);
        targetSteer =
            shapeAxis(axis(_settings.steerAxis), _settings.deadzone, _settings.invertSteer) * _settings.steerLimit;

        // Until the throttle has been in its dead zone once (after the start or a joystick coming back), neutral:
        // an axis stuck at one end, or the wrong axis, cannot drive the vehicle
        if (!_throttleOk) {
            if (rawThrottle == 0.0) {
                _throttleOk = true;
            } else {
                targetThrottle = 0.0;
                targetSteer = 0.0;
            }
        }

        // Arm switch: arm only once it has been in "disarm" since the start, and with the throttle centred.
        // Disarm at once, always.
        if (_settings.armAxis >= 0) {
            const bool switchOn =
                (_settings.armAxis < input.axes.size()) && (axis(_settings.armAxis) > _settings.armThreshold);
            if (!switchOn) {
                _armed = false;
                _armReady = true;
            } else if (!_armed) {
                if (!_armReady) {
                    output.armBlock = ArmBlock::SwitchOnAtStart;
                } else if (!_throttleOk || (rawThrottle != 0.0)) {
                    output.armBlock = ArmBlock::ThrottleNotCentered;
                } else {
                    _armed = true;
                }
            }
        } else {
            _armed = false;
        }
    }

    const double step = (_lastFrame >= 0.0) ? _settings.slew * std::max(nowSeconds - _lastFrame, 0.0) : 0.0;
    _throttle = slewTo(_throttle, targetThrottle, step);
    _steer = slewTo(_steer, targetSteer, step);
    _lastFrame = nowSeconds;

    output.channels = _build(_throttle, _steer, _armed, output.armGuard);
    output.armed = _armed && !output.armGuard;
    output.throttleCentered = _throttleOk;
    output.throttle = _throttle;
    output.steer = _steer;
    return output;
}

// ---- sender thread --------------------------------------------------------------------------------------------

namespace {

/// Sleeps until a point of a monotonic clock with about half a millisecond of error. Windows: a high resolution
/// waitable timer (Windows 10 1803 and later) with the system timer at 1 ms; elsewhere sleep_until.
class PreciseSleeper
{
public:
    PreciseSleeper()
    {
#ifdef Q_OS_WIN
        (void) timeBeginPeriod(1);
        _timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!_timer) {
            _timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        }
#endif
    }

    ~PreciseSleeper()
    {
#ifdef Q_OS_WIN
        if (_timer) {
            CloseHandle(_timer);
        }
        (void) timeEndPeriod(1);
#endif
    }

    Q_DISABLE_COPY_MOVE(PreciseSleeper)

    void sleepFor(qint64 nanoseconds)
    {
        if (nanoseconds <= 0) {
            return;
        }
#ifdef Q_OS_WIN
        if (_timer) {
            LARGE_INTEGER due;
            due.QuadPart = -(nanoseconds / 100);  // relative, in 100 ns units
            if (SetWaitableTimer(_timer, &due, 0, nullptr, nullptr, FALSE)) {
                (void) WaitForSingleObject(_timer, INFINITE);
                return;
            }
        }
#endif
        std::this_thread::sleep_for(std::chrono::nanoseconds(nanoseconds));
    }

private:
#ifdef Q_OS_WIN
    HANDLE _timer = nullptr;
#endif
};

}  // namespace

UdpRcStream::UdpRcStream(const UdpRcSettings& settings, const QHostAddress& target, QObject* parent)
    : QThread(parent), _settings(settings), _target(target)
{}

UdpRcStream::~UdpRcStream()
{
    stopNow();
    (void) wait();
}

void UdpRcStream::updateSettings(const UdpRcSettings& settings, const QHostAddress& target)
{
    QMutexLocker locker(&_mutex);
    _settings = settings;
    _target = target;
    _settingsChanged = true;
}

void UdpRcStream::stopGracefully()
{
    _graceful.store(true);
}

void UdpRcStream::stopNow()
{
    _abort.store(true);
}

UdpRcStream::Status UdpRcStream::status() const
{
    QMutexLocker locker(&_mutex);
    return _status;
}

QHostAddress UdpRcStream::target() const
{
    QMutexLocker locker(&_mutex);
    return _target;
}

void UdpRcStream::run()
{
    UdpRcSettings settings;
    QHostAddress target;
    {
        QMutexLocker locker(&_mutex);
        settings = _settings;
        target = _target;
        _settingsChanged = false;
        _status = Status();
        _status.running = true;
    }

    QUdpSocket socket;
    if (!socket.bind(QHostAddress::AnyIPv4, settings.sourcePort)) {
        qCInfo(UdpRcStreamLog) << "Source port" << settings.sourcePort << "taken (" << socket.errorString()
                               << "), using any";
        (void) socket.bind(QHostAddress::AnyIPv4, 0);
    }
    const quint16 sourcePort = socket.localPort();
    qCInfo(UdpRcStreamLog) << "RC stream to" << target.toString() << settings.port << "from port" << sourcePort << "at"
                           << settings.rateHz << "Hz";

    PreciseSleeper sleeper;
    UdpRcLogic logic(settings);
    QElapsedTimer clock;
    clock.start();
    const auto seconds = [&clock]() { return static_cast<double>(clock.nsecsElapsed()) / 1e9; };

    quint64 sent = 0;
    quint64 errors = 0;
    QString lastError;
    int sentThisSecond = 0;
    double secondStart = seconds();
    double rate = 0;
    double next = seconds();

    const auto send = [&](const QByteArray& bytes) {
        if (socket.writeDatagram(bytes, target, settings.port) == bytes.size()) {
            sent++;
            sentThisSecond++;
        } else {
            errors++;
            lastError = socket.errorString();
        }
    };

    while (!_abort.load() && !_graceful.load()) {
        {
            QMutexLocker locker(&_mutex);
            if (_settingsChanged) {
                settings = _settings;
                target = _target;
                _settingsChanged = false;
                logic.setSettings(settings);
            }
        }

        const RcJoystickFeed::Snapshot joystick = RcJoystickFeed::read();
        UdpRcLogic::Input input;
        input.joystick = joystick.valid;
        input.generation = joystick.generation;
        input.axes = joystick.axes;
        const double now = seconds();
        const UdpRcLogic::Output output = logic.step(input, now, LinkQualityMonitor::throttleScale());
        send(UdpRcLogic::frame(output.channels));

        if ((now - secondStart) >= 1.0) {
            rate = sentThisSecond / (now - secondStart);
            sentThisSecond = 0;
            secondStart = now;
        }
        {
            QMutexLocker locker(&_mutex);
            _status.rateHz = rate;
            _status.sent = sent;
            _status.errors = errors;
            _status.lastError = lastError;
            _status.joystick = input.joystick;
            _status.armed = output.armed;
            _status.throttleCentered = output.throttleCentered;
            _status.armBlock = output.armBlock;
            _status.throttle = output.throttle;
            _status.steer = output.steer;
            _status.armGuard = _status.armGuard || output.armGuard;
            _status.sourcePort = sourcePort;
        }

        // Even pace; after a long stall (the computer slept) the count starts again instead of catching up
        const double period = 1.0 / std::clamp(settings.rateHz, 50.0, 500.0);
        next += period;
        const double afterWork = seconds();
        if (next < afterWork - 0.1) {
            next = afterWork + period;
        }
        sleeper.sleepFor(static_cast<qint64>((next - afterWork) * 1e9));
    }

    // Last second: neutral and disarm, so the vehicle does not lose the stream armed and twitch its wheels
    if (_graceful.load() && !_abort.load()) {
        const QByteArray last = UdpRcLogic::frame(logic.neutralDisarmed());
        const double period = 1.0 / std::clamp(settings.rateHz, 50.0, 500.0);
        const int frames = static_cast<int>(settings.rateHz) + 1;
        double tick = seconds();
        for (int i = 0; (i < frames) && !_abort.load(); i++) {
            send(last);
            tick += period;
            sleeper.sleepFor(static_cast<qint64>((tick - seconds()) * 1e9));
        }
        qCInfo(UdpRcStreamLog) << "RC stream stopped after" << frames << "frames of disarm and neutral";
    } else {
        qCInfo(UdpRcStreamLog) << "RC stream stopped at once";
    }

    QMutexLocker locker(&_mutex);
    _status.running = false;
    _status.sent = sent;
    _status.errors = errors;
    _status.armed = false;
}
