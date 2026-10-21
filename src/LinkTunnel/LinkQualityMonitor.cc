#include "LinkQualityMonitor.h"
#include "IcmpPing.h"
#include "LinkConfiguration.h"
#include "LinkInterface.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "VehicleLinkManager.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QTextStream>
#include <QtCore/QVariantMap>
#include <QtCore/QThread>

#include <algorithm>
#include <cmath>

QGC_LOGGING_CATEGORY(LinkQualityMonitorLog, "LinkTunnel.LinkQualityMonitor")

std::atomic<float> LinkQualityMonitor::_throttleScale{1.0f};

LinkQualityMonitor::LinkQualityMonitor(QObject *parent)
    : QObject(parent)
{
    _pingTimer.setInterval(kPingIntervalMs);
    (void) connect(&_pingTimer, &QTimer::timeout, this, &LinkQualityMonitor::_sendPing);

    _rampTimer.setInterval(kRampIntervalMs);
    (void) connect(&_rampTimer, &QTimer::timeout, this, &LinkQualityMonitor::_rampThrottle);

    (void) connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged,
                   this, &LinkQualityMonitor::_activeVehicleChanged);
    _activeVehicleChanged(MultiVehicleManager::instance()->activeVehicle());
}

int LinkQualityMonitor::throttlePercent() const
{
    return static_cast<int>(std::lround(throttleScale() * 100.0f));
}

void LinkQualityMonitor::_activeVehicleChanged(Vehicle *vehicle)
{
    if (_activeVehicle) {
        (void) disconnect(_activeVehicle->vehicleLinkManager(), nullptr, this, nullptr);
    }

    _activeVehicle = vehicle;
    if (_activeVehicle) {
        (void) connect(_activeVehicle->vehicleLinkManager(), &VehicleLinkManager::primaryLinkChanged,
                       this, &LinkQualityMonitor::_updateConfiguration);
    }

    _updateConfiguration();
}

void LinkQualityMonitor::_updateConfiguration()
{
    QString pingAddress;
    bool throttleLimitEnabled = false;

    if (_activeVehicle) {
        const SharedLinkInterfacePtr link = _activeVehicle->vehicleLinkManager()->primaryLink().lock();
        const SharedLinkConfigurationPtr config = link ? link->linkConfiguration() : nullptr;
        if (config) {
            pingAddress = config->pingAddress().trimmed();
            throttleLimitEnabled = config->throttleLimitEnabled() && !pingAddress.isEmpty();
            // Keep the thresholds ordered and the percentages sane whatever was typed in.
            _pingThreshold1Ms = std::max(1, config->pingThreshold1Ms());
            _pingThreshold2Ms = std::max(_pingThreshold1Ms, config->pingThreshold2Ms());
            _throttlePercent1 = std::clamp(config->throttlePercent1(), 0, 100);
            _throttlePercent2 = std::clamp(config->throttlePercent2(), 0, _throttlePercent1);
        }
    }

    const bool changed = (pingAddress != _pingAddress) || (throttleLimitEnabled != _throttleLimitEnabled);
    _pingAddress = pingAddress;
    _throttleLimitEnabled = throttleLimitEnabled;
    if (!changed) {
        return;
    }

    _reset();
    if (monitoring()) {
        _pingTimer.start();
        _rampTimer.start();
        _sendPing();
    } else {
        _pingTimer.stop();
        _rampTimer.stop();
    }
    emit configurationChanged();
}

QVariantList LinkQualityMonitor::events() const
{
    QVariantList list;
    auto toMap = [](const Event &e, bool ongoing) {
        QVariantMap m;
        m.insert(QStringLiteral("kind"), e.kind);
        m.insert(QStringLiteral("startMs"), e.startMs);
        m.insert(QStringLiteral("endMs"), e.endMs);
        m.insert(QStringLiteral("count"), e.count);
        m.insert(QStringLiteral("maxMs"), e.maxMs);
        m.insert(QStringLiteral("ongoing"), ongoing);
        return m;
    };
    if (!_current.kind.isEmpty() && _current.count >= ((_current.kind == QLatin1String("loss")) ? kLossLogMin : kLagSamples)) {
        list.append(toMap(_current, true));
    }
    for (auto it = _events.crbegin(); it != _events.crend(); ++it) {
        list.append(toMap(*it, false));
    }
    return list;
}

void LinkQualityMonitor::_writeLog(const QVariantMap &event) const
{
    // One line per finished event, kept next to the other application data; trimmed when it grows large.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        return;
    }
    QFile file(dir + QStringLiteral("/link-quality.log"));
    if (file.size() > 1024 * 1024) {
        (void) QFile::remove(file.fileName() + QStringLiteral(".old"));
        (void) file.rename(file.fileName() + QStringLiteral(".old"));
    }
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream out(&file);
    const qint64 start = event.value(QStringLiteral("startMs")).toLongLong();
    const qint64 end = event.value(QStringLiteral("endMs")).toLongLong();
    out << QDateTime::fromMSecsSinceEpoch(start).toString(Qt::ISODate)
        << ' ' << _pingAddress
        << ' ' << event.value(QStringLiteral("kind")).toString()
        << " duration_s=" << QString::number(static_cast<double>(end - start) / 1000.0, 'f', 1)
        << " count=" << event.value(QStringLiteral("count")).toInt()
        << " max_ms=" << event.value(QStringLiteral("maxMs")).toInt()
        << '\n';
}

void LinkQualityMonitor::_closeEvent()
{
    const bool loss = (_current.kind == QLatin1String("loss"));
    if (!_current.kind.isEmpty() && (_current.count >= (loss ? kLossLogMin : kLagSamples))) {
        _events.append(_current);
        while (_events.count() > kMaxEvents) {
            _events.removeFirst();
        }
        QVariantMap m;
        m.insert(QStringLiteral("kind"), _current.kind);
        m.insert(QStringLiteral("startMs"), _current.startMs);
        m.insert(QStringLiteral("endMs"), _current.endMs);
        m.insert(QStringLiteral("count"), _current.count);
        m.insert(QStringLiteral("maxMs"), _current.maxMs);
        _writeLog(m);
    }
    _current = Event();
}

/// Builds the local event list from the ping results: runs of lost pings and runs of slow replies.
void LinkQualityMonitor::_trackEvents(int roundTripMs)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool lost = (roundTripMs < 0);
    const bool slow = !lost && (roundTripMs >= kLagThresholdMs);
    const QString wanted = lost ? QStringLiteral("loss") : (slow ? QStringLiteral("lag") : QString());

    if (wanted != _current.kind) {
        _closeEvent();
        if (!wanted.isEmpty()) {
            _current.kind = wanted;
            _current.startMs = now;
        }
    }
    if (!wanted.isEmpty()) {
        _current.count++;
        _current.endMs = now;
        _current.maxMs = std::max(_current.maxMs, roundTripMs);
    }
    emit eventsChanged();
}

void LinkQualityMonitor::_reset()
{
    _closeEvent();
    _samples.clear();
    _limitLevel = 0;
    if (_pingMs != -1) {
        _pingMs = -1;
        emit pingChanged();
    }
    _throttleScale.store(1.0f, std::memory_order_relaxed);
    emit throttlePercentChanged();
}

void LinkQualityMonitor::_sendPing()
{
    // A ping still waiting for its reply counts as the sample for this interval when it returns.
    if (_pingInFlight || _pingAddress.isEmpty()) {
        return;
    }
    _pingInFlight = true;

    const QString address = _pingAddress;
    const QPointer<LinkQualityMonitor> self(this);
    // The blocking echo runs on a worker thread; the result is handed back on the application thread,
    // which is also the only place the monitor pointer is touched.
    QThread *const thread = QThread::create([self, address]() {
        const int roundTripMs = IcmpPing::ping(address, kPingTimeoutMs);
        (void) QMetaObject::invokeMethod(QCoreApplication::instance(), [self, address, roundTripMs]() {
            if (self) {
                self->_pingFinished(address, roundTripMs);
            }
        }, Qt::QueuedConnection);
    });
    (void) connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void LinkQualityMonitor::_pingFinished(const QString &address, int roundTripMs)
{
    _pingInFlight = false;
    if (address != _pingAddress) {
        return;
    }

    _trackEvents(roundTripMs);
    _samples.append(roundTripMs);
    while (_samples.count() > kSampleCount) {
        _samples.removeFirst();
    }

    int total = 0;
    int received = 0;
    for (const int sample : std::as_const(_samples)) {
        if (sample >= 0) {
            total += sample;
            received++;
        }
    }

    int consecutiveLost = 0;
    for (auto it = _samples.crbegin(); (it != _samples.crend()) && (*it < 0); ++it) {
        consecutiveLost++;
    }

    const int pingMs = ((received == 0) || (consecutiveLost >= kLostPingCutoff)) ? -1 : (total / received);
    if (pingMs != _pingMs) {
        _pingMs = pingMs;
        emit pingChanged();
    }

    if (_pingMs < 0) {
        return;
    }

    // Limits engage as soon as a threshold is crossed and release only below it by the hysteresis.
    if (_pingMs > _pingThreshold2Ms) {
        _limitLevel = 2;
    } else if (_pingMs > _pingThreshold1Ms) {
        if ((_limitLevel < 1) || (_pingMs < (_pingThreshold2Ms - kHysteresisMs))) {
            _limitLevel = 1;
        }
    } else if (_pingMs < (_pingThreshold1Ms - kHysteresisMs)) {
        _limitLevel = 0;
    } else if (_limitLevel == 2) {
        _limitLevel = 1;
    }
}

float LinkQualityMonitor::_targetScale() const
{
    if (!_throttleLimitEnabled) {
        return 1.0f;
    }
    // No reply for several pings: the vehicle cannot be reached reliably, so do not drive it.
    if ((_pingMs < 0) && (_samples.count() >= kLostPingCutoff)) {
        return 0.0f;
    }
    switch (_limitLevel) {
    case 2:
        return static_cast<float>(_throttlePercent2) / 100.0f;
    case 1:
        return static_cast<float>(_throttlePercent1) / 100.0f;
    default:
        return 1.0f;
    }
}

void LinkQualityMonitor::_rampThrottle()
{
    const float target = _targetScale();
    const float current = throttleScale();
    if (qFuzzyCompare(current + 1.0f, target + 1.0f)) {
        return;
    }

    // Slowing down is applied at once; speed is given back gradually.
    const float next = (target < current) ? target : std::min(target, current + kRampStep);
    _throttleScale.store(next, std::memory_order_relaxed);
    emit throttlePercentChanged();
}
