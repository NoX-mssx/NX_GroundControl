#pragma once

#include <QtCore/QString>
#include <QtCore/QUrl>

typedef struct _GstElement GstElement;

/// WebRTC video from a WHEP server (MediaMTX): http://<server>:8889/<path>/whep.
///
/// webrtcbin receives the stream; the signalling is the WHEP exchange over Qt network: the offer (receive only,
/// video and the camera audio, all ICE candidates in it, no trickle ICE) is POSTed as application/sdp, the answer
/// comes back with 201 and the session address in Location, which gets a DELETE when the element goes away.
/// The RTP pads of webrtcbin feed the same parser, decoder, recording and window as the other sources; webrtcbin
/// has its own jitter buffer, so none is added in front of the parser.
namespace GStreamer::Whep {

/// http(s) addresses (MediaMTX page or WHEP endpoint) and whep(s)://
bool isWhepUrl(const QString& uri);

/// Address the offer goes to: http(s), user and password removed, ending in /whep
QUrl endpointUrl(const QString& uri);

/// Short name of the stream for messages: the path without /whep ("front-sd")
QString streamName(const QString& uri);

/// Makes the configured webrtcbin, named "source". Its src pads appear once media arrives ("pad-added").
/// @param audio offer to receive the camera audio as well (PCMU, PCMA, Opus)
/// @param dropLate late packets are dropped by the jitter buffer instead of waited for
GstElement* createSource(const QString& uri, bool audio, unsigned latencyMs, bool dropLate);

}  // namespace GStreamer::Whep
