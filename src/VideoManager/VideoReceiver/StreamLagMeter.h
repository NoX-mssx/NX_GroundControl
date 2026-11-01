#pragma once

#include <QtCore/QtGlobal>

#include <algorithm>
#include <cstdint>
#include <cstdlib>

/// How far an RTP stream has fallen behind live since it started.
///
/// While the link keeps up, media time (the RTP time stamps) and arrival time advance together. When it does
/// not, for example RTSP over TCP on a link slower than the camera's bit rate, packets wait upstream and
/// arrive later and later, so arrival time pulls ahead of media time. The smallest difference seen is the
/// stream's normal delay; the meter reports how much the delay grew above it.
///
/// That baseline may rise by up to 1 ms per second, which absorbs a camera clock running slow against ours
/// without hiding a real backlog: a link that falls short of the bit rate even by 1 % adds 10 ms per second.
class StreamLagMeter
{
public:
    void reset() { *this = StreamLagMeter(); }

    /// @param rtpTimestamp time stamp of a received packet
    /// @param clockRate clock of the time stamps (90000 for video)
    /// @param nowMs a monotonic clock
    /// @return how far the stream is behind its normal delay, in ms
    qint64 add(uint32_t rtpTimestamp, int clockRate, qint64 nowMs)
    {
        if (clockRate <= 0) {
            return 0;
        }
        if (_started) {
            const int32_t delta = static_cast<int32_t>(rtpTimestamp - _lastTimestamp);
            if (std::llabs(delta) > static_cast<long long>(clockRate) * kMaxJumpSeconds) {
                // The camera restarted its time stamps: measure from here
                reset();
            }
        }
        if (!_started) {
            _started = true;
            _lastTimestamp = rtpTimestamp;
            _mediaTicks = 0;
            _baselineMs = static_cast<double>(nowMs);
            _lastNowMs = nowMs;
            return 0;
        }

        _mediaTicks += static_cast<int32_t>(rtpTimestamp - _lastTimestamp);
        _lastTimestamp = rtpTimestamp;
        const double lagMs = static_cast<double>(nowMs) - ((static_cast<double>(_mediaTicks) * 1000.0) / clockRate);
        _baselineMs = std::min(lagMs, _baselineMs + (static_cast<double>(nowMs - _lastNowMs) * kBaselineDriftPerMs));
        _lastNowMs = nowMs;
        return static_cast<qint64>(lagMs - _baselineMs);
    }

private:
    static constexpr int kMaxJumpSeconds = 10;
    static constexpr double kBaselineDriftPerMs = 0.001;

    bool _started = false;
    uint32_t _lastTimestamp = 0;
    qint64 _mediaTicks = 0;
    double _baselineMs = 0;
    qint64 _lastNowMs = 0;
};
