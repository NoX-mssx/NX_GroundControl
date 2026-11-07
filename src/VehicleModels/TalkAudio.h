#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QtGlobal>

#include <cstdint>
#include <cstring>

/// Audio helpers for talking to a camera: capture formats to 8 kHz mono, and G.711 companding
namespace TalkAudio {

constexpr int kSampleRate = 8000;
/// Samples in one RTP packet: 20 ms
constexpr int kPacketSamples = 160;

enum class SampleFormat { UInt8, Int16, Int32, Float };

inline int bytesPerSample(SampleFormat format)
{
    switch (format) {
    case SampleFormat::UInt8:
        return 1;
    case SampleFormat::Int16:
        return 2;
    case SampleFormat::Int32:
    case SampleFormat::Float:
        return 4;
    }
    return 2;
}

/// G.711 A-law of a 16 bit sample
inline uint8_t encodeAlaw(int16_t pcm)
{
    static constexpr int16_t kSegmentEnd[8] = {0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF};
    int value = pcm >> 3;
    int mask = 0xD5;
    if (value < 0) {
        mask = 0x55;
        value = -value - 1;
    }
    int segment = 0;
    while ((segment < 8) && (value > kSegmentEnd[segment])) {
        segment++;
    }
    if (segment >= 8) {
        return static_cast<uint8_t>(0x7F ^ mask);
    }
    int code = segment << 4;
    code |= (segment < 2) ? ((value >> 1) & 0x0F) : ((value >> segment) & 0x0F);
    return static_cast<uint8_t>(code ^ mask);
}

/// G.711 mu-law of a 16 bit sample
inline uint8_t encodeUlaw(int16_t pcm)
{
    static constexpr int16_t kSegmentEnd[8] = {0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF};
    int value = pcm >> 2;
    int mask = 0xFF;
    if (value < 0) {
        value = -value;
        mask = 0x7F;
    }
    value = qMin(value, 8159) + 0x21;
    int segment = 0;
    while ((segment < 8) && (value > kSegmentEnd[segment])) {
        segment++;
    }
    if (segment >= 8) {
        return static_cast<uint8_t>(0x7F ^ mask);
    }
    return static_cast<uint8_t>(((segment << 4) | ((value >> (segment + 1)) & 0x0F)) ^ mask);
}

/// Turns what a microphone delivers (any rate, channels and sample format) into 16 bit mono at 8 kHz.
/// Each output sample is the mean of the input samples it covers, which is enough for speech.
class Resampler
{
public:
    void configure(int sampleRate, int channels, SampleFormat format)
    {
        _step = static_cast<double>(kSampleRate) / qMax(1, sampleRate);
        _channels = qMax(1, channels);
        _format = format;
        _phase = 0;
        _sum = 0;
        _count = 0;
        _last = 0;
        _partial.clear();
    }

    /// @return little endian 16 bit samples
    QByteArray process(const QByteArray &input)
    {
        QByteArray data = _partial + input;
        const int frameSize = bytesPerSample(_format) * _channels;
        const int frames = static_cast<int>(data.size() / frameSize);
        _partial = data.mid(frames * frameSize);

        QByteArray output;
        output.reserve(static_cast<qsizetype>(frames * _step * 2) + 4);
        const char *raw = data.constData();
        for (int i = 0; i < frames; i++) {
            double mono = 0;
            for (int channel = 0; channel < _channels; channel++) {
                mono += sample(raw + (static_cast<qsizetype>(i) * frameSize) + (channel * bytesPerSample(_format)));
            }
            _sum += mono / _channels;
            _count++;
            _phase += _step;
            while (_phase >= 1.0) {
                _phase -= 1.0;
                if (_count > 0) {
                    _last = _sum / _count;
                }
                _sum = 0;
                _count = 0;
                const int16_t out = static_cast<int16_t>(qBound(-32768.0, _last * 32767.0, 32767.0));
                output.append(reinterpret_cast<const char *>(&out), 2);
            }
        }
        return output;
    }

private:
    /// One sample as -1..1
    double sample(const char *p) const
    {
        switch (_format) {
        case SampleFormat::UInt8:
            return (static_cast<uint8_t>(*p) - 128) / 128.0;
        case SampleFormat::Int16: {
            int16_t v;
            std::memcpy(&v, p, 2);
            return v / 32768.0;
        }
        case SampleFormat::Int32: {
            int32_t v;
            std::memcpy(&v, p, 4);
            return v / 2147483648.0;
        }
        case SampleFormat::Float: {
            float v;
            std::memcpy(&v, p, 4);
            return v;
        }
        }
        return 0;
    }

    double _step = 1;
    int _channels = 1;
    SampleFormat _format = SampleFormat::Int16;
    double _phase = 0;
    double _sum = 0;
    int _count = 0;
    double _last = 0;
    QByteArray _partial;
};

}  // namespace TalkAudio
