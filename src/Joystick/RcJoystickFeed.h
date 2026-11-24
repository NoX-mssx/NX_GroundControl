#pragma once

#include <QtCore/QVector>
#include <atomic>

class Joystick;

/// NX: the raw axes of the active joystick for the UDP RC stream (UdpRcStream). The joystick thread publishes them
/// after every poll; the stream thread reads the latest. Raw means the joystick's own axis numbers and values
/// divided by 32768, without the calibration of the joystick settings, as pygame reads them.
class RcJoystickFeed
{
public:
    struct Snapshot
    {
        bool valid = false;
        quint64 generation = 0;  ///< changes whenever a joystick (re)appears
        qint64 timeMs = 0;       ///< monotonic, see now()
        QVector<double> axes;
    };

    /// Axes older than this count as a joystick gone (its thread stopped or hangs)
    static constexpr qint64 kStaleMs = 250;

    static void publish(const Joystick* source, const QVector<double>& axes);
    /// The joystick is gone or no longer polled
    static void invalidate(const Joystick* source);
    static Snapshot read();
    static qint64 now();

    /// Set while a stream runs: the active joystick is then polled with or without a vehicle
    static void setWanted(bool wanted) { _wanted.store(wanted, std::memory_order_relaxed); }

    static bool wanted() { return _wanted.load(std::memory_order_relaxed); }

private:
    static std::atomic<bool> _wanted;
};
