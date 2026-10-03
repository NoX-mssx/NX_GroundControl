#pragma once

#include <QtCore/QString>

namespace IcmpPing
{
/// Sends one ICMP echo request and waits for the reply. Blocks for up to @p timeoutMs.
/// @return round trip time in milliseconds, or -1 if there was no reply (or on unsupported platforms)
int ping(const QString &address, int timeoutMs);
}
