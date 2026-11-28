#include "WireGuardConfTextTest.h"

#include <QtTest/QTest>

#include "WireGuardTunnel.h"

namespace {

// Example keys and documentation addresses, not real ones
const QString kConf = QStringLiteral(
    "[Interface]\r\n"
    "PrivateKey = cGFydC1vbmUtb2YtYS10ZXN0LWtleS0wMDAwMDAwMDA=\r\n"
    "Address = 10.30.1.252/24\r\n"
    "DNS = 1.1.1.1\r\n"
    "\r\n"
    "[Peer]\r\n"
    "PublicKey = cGVlci1rZXktb2YtYS10ZXN0LXR1bm5lbC0wMDAwMDA=\r\n"
    "AllowedIPs = 10.30.1.0/24\r\n"
    "AllowedIPs = 192.168.106.0/24  # cameras\r\n"
    "Endpoint = 203.0.113.5:51821\r\n"
    "PersistentKeepalive = 25\r\n");

}  // namespace

void WireGuardConfTextTest::_parseTest()
{
    const QVariantMap parsed = WireGuardTunnel::parseConfText(kConf);
    const QVariantMap settings = parsed.value(QStringLiteral("settings")).toMap();
    QCOMPARE(settings.value(QStringLiteral("privateKey")).toString(),
             QStringLiteral("cGFydC1vbmUtb2YtYS10ZXN0LWtleS0wMDAwMDAwMDA="));
    QCOMPARE(settings.value(QStringLiteral("address")).toString(), QStringLiteral("10.30.1.252/24"));
    QCOMPARE(settings.value(QStringLiteral("dns")).toString(), QStringLiteral("1.1.1.1"));
    QCOMPARE(settings.value(QStringLiteral("peerPublicKey")).toString(),
             QStringLiteral("cGVlci1rZXktb2YtYS10ZXN0LXR1bm5lbC0wMDAwMDA="));
    QCOMPARE(settings.value(QStringLiteral("allowedIps")).toString(),
             QStringLiteral("10.30.1.0/24, 192.168.106.0/24"));
    QCOMPARE(settings.value(QStringLiteral("endpoint")).toString(), QStringLiteral("203.0.113.5:51821"));
    QCOMPARE(settings.value(QStringLiteral("keepalive")).toString(), QStringLiteral("25"));
    QVERIFY(parsed.value(QStringLiteral("missing")).toStringList().isEmpty());
    QVERIFY(parsed.value(QStringLiteral("ignored")).toStringList().isEmpty());
}

void WireGuardConfTextTest::_missingAndIgnoredTest()
{
    const QVariantMap parsed = WireGuardTunnel::parseConfText(QStringLiteral(
        "[Interface]\n"
        "PrivateKey = cGFydC1vbmUtb2YtYS10ZXN0LWtleS0wMDAwMDAwMDA=\n"
        "ListenPort = 51820\n"
        "[Peer]\n"
        "PublicKey = cGVlci1rZXktb2YtYS10ZXN0LXR1bm5lbC0wMDAwMDA=\n"
        "[Peer]\n"
        "PublicKey = c2Vjb25kLXBlZXItb2YtYS10ZXN0LXR1bm5lbC0wMDA=\n"
        "Endpoint = 198.51.100.7:51820\n"));
    QCOMPARE(parsed.value(QStringLiteral("missing")).toStringList(),
             QStringList({QStringLiteral("Address"), QStringLiteral("AllowedIPs"), QStringLiteral("Endpoint")}));
    // ListenPort is not kept; the second peer is listed once, by its header
    QCOMPARE(parsed.value(QStringLiteral("ignored")).toStringList(),
             QStringList({QStringLiteral("ListenPort = 51820"), QStringLiteral("[Peer]")}));
    // The second peer's endpoint is not taken for the first
    QVERIFY(!parsed.value(QStringLiteral("settings")).toMap().contains(QStringLiteral("endpoint")));
}

void WireGuardConfTextTest::_roundTripTest()
{
    const QVariantMap settings = WireGuardTunnel::parseConfText(kConf).value(QStringLiteral("settings")).toMap();
    const QString text = WireGuardTunnel::confText(settings);
    QVERIFY(text.startsWith(QStringLiteral("[Interface]\nPrivateKey = ")));
    QVERIFY(text.contains(QStringLiteral("\n\n[Peer]\nPublicKey = ")));
    QVERIFY(!text.contains(QStringLiteral("MTU")));
    QCOMPARE(WireGuardTunnel::parseConfText(text).value(QStringLiteral("settings")).toMap(), settings);
}

UT_REGISTER_TEST_LIGHTWEIGHT(WireGuardConfTextTest, TestLabel::Unit, TestLabel::Comms)
