#include "IcmpPing.h"

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#include <vector>
#endif

int IcmpPing::ping(const QString &address, int timeoutMs)
{
#ifdef Q_OS_WIN
    IN_ADDR destination;
    if (InetPtonA(AF_INET, address.toLatin1().constData(), &destination) != 1) {
        return -1;
    }

    const HANDLE icmp = IcmpCreateFile();
    if (icmp == INVALID_HANDLE_VALUE) {
        return -1;
    }

    char payload[] = "NX-GroundControl";
    // Room for one reply plus the ICMP error payload IcmpSendEcho may append.
    std::vector<char> replyBuffer(sizeof(ICMP_ECHO_REPLY) + sizeof(payload) + 8);

    int roundTripMs = -1;
    const DWORD replies = IcmpSendEcho(icmp, destination.S_un.S_addr, payload, static_cast<WORD>(sizeof(payload)), nullptr,
                                       replyBuffer.data(), static_cast<DWORD>(replyBuffer.size()),
                                       static_cast<DWORD>(timeoutMs));
    if (replies > 0) {
        const ICMP_ECHO_REPLY *const reply = reinterpret_cast<const ICMP_ECHO_REPLY*>(replyBuffer.data());
        if (reply->Status == IP_SUCCESS) {
            roundTripMs = static_cast<int>(reply->RoundTripTime);
        }
    }

    (void) IcmpCloseHandle(icmp);
    return roundTripMs;
#else
    Q_UNUSED(address);
    Q_UNUSED(timeoutMs);
    return -1;
#endif
}
