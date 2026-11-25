#include "StarlinkPositionTest.h"

#include <QtCore/QtEndian>
#include <QtPositioning/QGeoCoordinate>
#include <QtTest/QTest>

#include <cstring>

#include "StarlinkPosition.h"

namespace {

QByteArray varint(quint64 value)
{
    QByteArray out;
    while (value >= 0x80) {
        out.append(static_cast<char>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.append(static_cast<char>(value));
    return out;
}

QByteArray key(quint32 field, int wire)
{
    return varint((static_cast<quint64>(field) << 3) | static_cast<quint64>(wire));
}

QByteArray bytesField(quint32 field, const QByteArray &bytes)
{
    return key(field, 2) + varint(static_cast<quint64>(bytes.size())) + bytes;
}

QByteArray doubleField(quint32 field, double value)
{
    quint64 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    QByteArray out(8, '\0');
    qToLittleEndian<quint64>(bits, out.data());
    return key(field, 1) + out;
}

/// A gRPC answer frame around a Response message
QByteArray frame(const QByteArray &message)
{
    QByteArray out(5, '\0');
    qToBigEndian<quint32>(static_cast<quint32>(message.size()), out.data() + 1);
    return out + message;
}

/// Response { status = {}, api_version = 40, get_location = <location> }, as the dish answers
QByteArray response(const QByteArray &location)
{
    return frame(bytesField(2, QByteArray()) + key(3, 0) + varint(40) + bytesField(1017, location));
}

} // namespace

void StarlinkPositionTest::_testLocationRequest()
{
    // Not compressed, length 3, Request { get_location (1017) = {} }
    QCOMPARE(StarlinkPosition::locationRequest(), QByteArray::fromHex("0000000003ca3f00"));
}

void StarlinkPositionTest::_testParseLocation()
{
    const QByteArray lla = doubleField(1, 50.4501) + doubleField(2, 30.5234) + doubleField(3, 179.5);
    // A source field after lla, as newer dishes send, is skipped
    const QByteArray body = response(bytesField(1, lla) + key(3, 0) + varint(5));

    QString error = QStringLiteral("stale");
    const QGeoCoordinate coordinate = StarlinkPosition::parseLocationResponse(body, &error);
    QVERIFY(coordinate.isValid());
    QVERIFY(error.isEmpty());
    QCOMPARE(coordinate.latitude(), 50.4501);
    QCOMPARE(coordinate.longitude(), 30.5234);
    QCOMPARE(coordinate.altitude(), 179.5);
}

void StarlinkPositionTest::_testParseStatusError()
{
    const QByteArray status = key(1, 0) + varint(7) + bytesField(2, QByteArrayLiteral("Location data not enabled"));
    const QByteArray body = frame(bytesField(2, status));

    QString error;
    QVERIFY(!StarlinkPosition::parseLocationResponse(body, &error).isValid());
    QVERIFY(error.contains(QStringLiteral("Location data not enabled")));
}

void StarlinkPositionTest::_testParseNoPosition()
{
    QString error;
    QVERIFY(!StarlinkPosition::parseLocationResponse(response(QByteArray()), &error).isValid());
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(!StarlinkPosition::parseLocationResponse(QByteArray(), &error).isValid());
    QVERIFY(!error.isEmpty());
}

void StarlinkPositionTest::_testParseDamaged()
{
    const QByteArray lla = doubleField(1, 50.4501) + doubleField(2, 30.5234);
    QByteArray body = response(bytesField(1, lla));
    // The frame claims more than arrived
    body.chop(4);

    QString error;
    QVERIFY(!StarlinkPosition::parseLocationResponse(body, &error).isValid());
    QVERIFY(!error.isEmpty());

    // A field running past the end of the message
    error.clear();
    const QByteArray overrun = frame(key(1017, 2) + varint(50) + QByteArray(3, 'x'));
    QVERIFY(!StarlinkPosition::parseLocationResponse(overrun, &error).isValid());
    QVERIFY(!error.isEmpty());
}

void StarlinkPositionTest::_testGrpcStatus()
{
    QVERIFY(StarlinkPosition::grpcStatusOk(QByteArray()));
    QVERIFY(StarlinkPosition::grpcStatusOk("0"));
    // The same header seen twice, as Qt lists it after the trailers
    QVERIFY(StarlinkPosition::grpcStatusOk("0, 0"));
    QVERIFY(!StarlinkPosition::grpcStatusOk("7"));
    QVERIFY(!StarlinkPosition::grpcStatusOk("0, 7"));
}

void StarlinkPositionTest::_testTrackSpeed()
{
    const QGeoCoordinate start(50.0, 30.0);
    using Track = QList<StarlinkPosition::TrackPoint>;

    // 10 m/s due north over 3 s, with a point in between that does not change the result
    const Track moving{
        {0, start}, {1000, start.atDistanceAndAzimuth(10, 0)}, {3000, start.atDistanceAndAzimuth(30, 0)}};
    QVERIFY(qAbs(StarlinkPosition::trackSpeed(moving) - 10.0) < 0.01);

    // A single position, or two too close in time: no speed yet
    QCOMPARE(StarlinkPosition::trackSpeed(Track{{0, start}}), 0.0);
    QCOMPARE(StarlinkPosition::trackSpeed(Track{{0, start}, {500, start.atDistanceAndAzimuth(10, 0)}}), 0.0);

    // A standing dish wanders by a metre: still
    QCOMPARE(StarlinkPosition::trackSpeed(Track{{0, start}, {3000, start.atDistanceAndAzimuth(1.0, 90)}}), 0.0);

    // Walking pace still counts: 2 m over 2 s
    const Track walking{{0, start}, {2000, start.atDistanceAndAzimuth(2.0, 45)}};
    QVERIFY(qAbs(StarlinkPosition::trackSpeed(walking) - 1.0) < 0.01);
}

UT_REGISTER_TEST_LIGHTWEIGHT(StarlinkPositionTest, TestLabel::Unit, TestLabel::Comms)
