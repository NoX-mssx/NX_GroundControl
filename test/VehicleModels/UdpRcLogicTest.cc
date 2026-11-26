#include "UdpRcLogicTest.h"

#include <QtTest/QTest>
#include <cmath>

#include "UdpRcStream.h"

namespace {

using Input = UdpRcLogic::Input;

constexpr std::array<quint16, UdpRcSettings::kChannels> kRest{
    {992, 992, 173, 173, 173, 420, 173, 173, 1811, 173, 992, 992, 992, 992, 992, 992}};
constexpr double kFullForward = 32767.0 / 32768.0;  // a stick at its end, as pygame reports it
constexpr double kFrame = 0.01;                     // 100 Hz

/// Axes in the order of the default settings: 0 throttle, 1 steering, 6 arm switch
Input joystick(quint64 generation, double throttle, double steer, double armSwitch)
{
    Input input;
    input.joystick = true;
    input.generation = generation;
    input.axes = {throttle, steer, 0, 0, 0, 0, armSwitch};
    return input;
}

UdpRcLogic::Output runFrames(UdpRcLogic& logic, const Input& input, double& now, int frames)
{
    UdpRcLogic::Output output;
    for (int i = 0; i < frames; i++) {
        output = logic.step(input, now);
        now += kFrame;
    }
    return output;
}

}  // namespace

void UdpRcLogicTest::_crsfRoundingTest()
{
    QCOMPARE(UdpRcLogic::toCrsf(0.0), quint16(992));
    QCOMPARE(UdpRcLogic::toCrsf(1.0), quint16(1811));
    QCOMPARE(UdpRcLogic::toCrsf(-1.0), quint16(173));
    QCOMPARE(UdpRcLogic::toCrsf(-3.0), quint16(172));
    QCOMPARE(UdpRcLogic::toCrsf(3.0), quint16(1811));
    // Python's round(): a half goes to the even neighbour
    QCOMPARE(UdpRcLogic::toCrsf(0.5 / 819.0), quint16(992));
    QCOMPARE(UdpRcLogic::toCrsf(1.5 / 819.0), quint16(994));
}

void UdpRcLogicTest::_restFrameTest()
{
    const UdpRcLogic logic;
    const QByteArray frame = UdpRcLogic::frame(logic.neutralDisarmed());
    QCOMPARE(frame.size(), 32);
    // struct.pack(">16H", ...) of the script's rest values with channel 8 disarmed
    QCOMPARE(frame.toHex(), QByteArray("03e003e000ad00ad00ad01a400ad00ad071300ad03e003e003e003e003e003e0"));
}

void UdpRcLogicTest::_stickValuesTest()
{
    UdpRcLogic logic;  // throttle limit 30 %, steering 50 %
    double now = 0;
    (void) runFrames(logic, joystick(1, 0, 0, -1), now, 5);

    UdpRcLogic::Output output = runFrames(logic, joystick(1, kFullForward, 0, -1), now, 50);
    QCOMPARE(output.channels[0], quint16(1238));
    output = runFrames(logic, joystick(1, -1.0, 0, -1), now, 50);
    QCOMPARE(output.channels[0], quint16(746));
    output = runFrames(logic, joystick(1, 0, kFullForward, -1), now, 50);
    QCOMPARE(output.channels[0], quint16(992));
    QCOMPARE(output.channels[1], quint16(1401));
    for (size_t i = 2; i < kRest.size(); i++) {
        QCOMPARE(output.channels[i], (i == 7) ? quint16(173) : kRest[i]);
    }
}

void UdpRcLogicTest::_throttleGuardTest()
{
    UdpRcLogic logic;
    double now = 0;
    // Throttle held at the start: neutral on both sticks
    UdpRcLogic::Output output = runFrames(logic, joystick(1, 0.9, 0.9, -1), now, 50);
    QVERIFY(!output.throttleCentered);
    QCOMPARE(output.channels[0], quint16(992));
    QCOMPARE(output.channels[1], quint16(992));
    // The dead zone counts as centred
    output = runFrames(logic, joystick(1, 0.05, 0.9, -1), now, 50);
    QVERIFY(output.throttleCentered);
    QVERIFY(output.channels[1] > 1300);
    output = runFrames(logic, joystick(1, 0.9, 0, -1), now, 50);
    QVERIFY(output.channels[0] > 1150);
    // The joystick back (a new generation) with the throttle held: neutral again
    output = runFrames(logic, joystick(2, 0.9, 0, -1), now, 50);
    QVERIFY(!output.throttleCentered);
    QCOMPARE(output.channels[0], quint16(992));
}

void UdpRcLogicTest::_armTest()
{
    UdpRcLogic logic;
    double now = 0;
    // Switch on at the start: no arm until it has been off
    UdpRcLogic::Output output = runFrames(logic, joystick(1, 0, 0, 1.0), now, 5);
    QVERIFY(!output.armed);
    QVERIFY(output.armBlock == UdpRcLogic::ArmBlock::SwitchOnAtStart);
    QCOMPARE(output.channels[7], quint16(173));
    (void) runFrames(logic, joystick(1, 0, 0, -1.0), now, 5);
    output = runFrames(logic, joystick(1, 0, 0, 1.0), now, 5);
    QVERIFY(output.armed);
    QCOMPARE(output.channels[7], quint16(992));
    // Disarm at once, whatever the throttle
    (void) runFrames(logic, joystick(1, 0.9, 0, 1.0), now, 20);
    output = logic.step(joystick(1, 0.9, 0, -1.0), now);
    now += kFrame;
    QVERIFY(!output.armed);
    QCOMPARE(output.channels[7], quint16(173));
    // Switch on with the throttle off centre: no arm until it is centred
    output = runFrames(logic, joystick(1, 0.9, 0, 1.0), now, 5);
    QVERIFY(!output.armed);
    QVERIFY(output.armBlock == UdpRcLogic::ArmBlock::ThrottleNotCentered);
    output = runFrames(logic, joystick(1, 0, 0, 1.0), now, 5);
    QVERIFY(output.armed);
    // The middle of a three-position switch is disarm
    output = runFrames(logic, joystick(1, 0, 0, 0.0), now, 1);
    QVERIFY(!output.armed);
}

void UdpRcLogicTest::_joystickLossTest()
{
    UdpRcLogic logic;
    double now = 0;
    (void) runFrames(logic, joystick(1, 0, 0, -1), now, 5);
    (void) runFrames(logic, joystick(1, 0, 0, 1.0), now, 5);
    UdpRcLogic::Output output = runFrames(logic, joystick(1, 0.9, 0.9, 1.0), now, 50);
    QVERIFY(output.armed);

    // Lost: neutral and disarm in the same frame
    output = logic.step(Input(), now);
    now += kFrame;
    QVERIFY(!output.armed);
    QCOMPARE(output.channels[0], quint16(992));
    QCOMPARE(output.channels[1], quint16(992));
    QCOMPARE(output.channels[7], quint16(173));

    // Back with the switch still on: it has to go through disarm again
    output = runFrames(logic, joystick(2, 0, 0, 1.0), now, 10);
    QVERIFY(!output.armed);
    QVERIFY(output.armBlock == UdpRcLogic::ArmBlock::SwitchOnAtStart);
}

void UdpRcLogicTest::_slewTest()
{
    UdpRcLogic logic;  // 4 full strokes a second
    double now = 0;
    (void) runFrames(logic, joystick(1, 0, 0, -1), now, 5);
    // 0 to 0.3 takes 75 ms
    UdpRcLogic::Output output = runFrames(logic, joystick(1, 1.0, 0, -1), now, 4);
    QVERIFY((output.throttle > 0.1) && (output.throttle < 0.3));
    output = runFrames(logic, joystick(1, 1.0, 0, -1), now, 6);
    QVERIFY(std::abs(output.throttle - 0.3) < 1e-9);
    // Release: neutral in one frame
    output = runFrames(logic, joystick(1, 0, 0, -1), now, 1);
    QCOMPARE(output.channels[0], quint16(992));
    // Reversal: to zero at once, then the ramp
    QCOMPARE(UdpRcLogic::slewTo(0.3, -0.3, 0.04), -0.04);
    QCOMPARE(UdpRcLogic::slewTo(0.3, 0.1, 0.04), 0.1);
}

void UdpRcLogicTest::_throttleScaleTest()
{
    UdpRcLogic logic;
    double now = 0;
    (void) runFrames(logic, joystick(1, 0, 0, -1), now, 5);
    UdpRcLogic::Output output;
    for (int i = 0; i < 50; i++) {
        output = logic.step(joystick(1, 1.0, 0, -1), now, 0.5);
        now += kFrame;
    }
    QCOMPARE(output.channels[0], UdpRcLogic::toCrsf(0.15));
}

void UdpRcLogicTest::_armGuardTest()
{
    UdpRcSettings settings;
    settings.armValue = 1811;  // arm + Auto: must never go out
    UdpRcLogic logic(settings);
    double now = 0;
    (void) runFrames(logic, joystick(1, 0, 0, -1), now, 5);
    const UdpRcLogic::Output output = runFrames(logic, joystick(1, 0, 0, 1.0), now, 5);
    QVERIFY(output.armGuard);
    QVERIFY(!output.armed);
    QVERIFY(output.channels[7] <= UdpRcSettings::kArmChannelMax);
}

void UdpRcLogicTest::_settingsTest()
{
    UdpRcSettings settings;
    settings.enabled = true;
    QVERIFY(settings.problem().isEmpty());
    QVERIFY(UdpRcSettings::fromModel(settings.toModel()) == settings);
    // Channel 6 is free to change
    settings.rest[5] = 600;
    QVERIFY(settings.problem().isEmpty());
    QCOMPARE(UdpRcLogic(settings).neutralDisarmed()[5], quint16(600));

    UdpRcSettings bad = settings;
    bad.armValue = 1811;
    QVERIFY(!bad.problem().isEmpty());
    bad = settings;
    bad.steerChannel = bad.throttleChannel;
    QVERIFY(!bad.problem().isEmpty());
    bad = settings;
    bad.rest[3] = 2000;
    QVERIFY(!bad.problem().isEmpty());
    bad = settings;
    bad.rateHz = 20;
    QVERIFY(!bad.problem().isEmpty());
    bad = settings;
    bad.armAxis = bad.throttleAxis;
    QVERIFY(!bad.problem().isEmpty());
}

namespace {

Input screen(Input input, UdpRcLogic::ArmRequest request)
{
    input.armRequest = request;
    return input;
}

}  // namespace

void UdpRcLogicTest::_screenArmTest()
{
    using Request = UdpRcLogic::ArmRequest;
    UdpRcLogic logic;
    double now = 0;
    (void) runFrames(logic, joystick(1, 0, 0, -1), now, 5);

    // Throttle held: refused, nothing armed
    UdpRcLogic::Output output = logic.step(screen(joystick(1, 0.9, 0, -1), Request::Arm), now);
    now += kFrame;
    QVERIFY(output.screenArmRefused == UdpRcLogic::ArmBlock::ThrottleNotCentered);
    QVERIFY(!output.armed);

    // Centred: armed from the screen with the switch off, and it stays armed
    output = logic.step(screen(joystick(1, 0, 0, -1), Request::Arm), now);
    now += kFrame;
    QVERIFY(output.armed);
    QCOMPARE(output.channels[7], quint16(992));
    output = runFrames(logic, joystick(1, 0.9, 0, -1), now, 20);
    QVERIFY(output.armed);

    // The switch on and off again ends it
    output = runFrames(logic, joystick(1, 0, 0, 1.0), now, 2);
    QVERIFY(output.armed);
    output = runFrames(logic, joystick(1, 0, 0, -1.0), now, 1);
    QVERIFY(!output.armed);
    QCOMPARE(output.channels[7], quint16(173));

    // Armed by the switch, disarmed from the screen: the switch has to go off and on to arm again
    output = runFrames(logic, joystick(1, 0, 0, 1.0), now, 2);
    QVERIFY(output.armed);
    output = logic.step(screen(joystick(1, 0, 0, 1.0), Request::Disarm), now);
    now += kFrame;
    QVERIFY(!output.armed);
    output = runFrames(logic, joystick(1, 0, 0, 1.0), now, 10);
    QVERIFY(!output.armed);
    QVERIFY(output.armBlock == UdpRcLogic::ArmBlock::SwitchOnAtStart);
    (void) runFrames(logic, joystick(1, 0, 0, -1.0), now, 1);
    output = runFrames(logic, joystick(1, 0, 0, 1.0), now, 1);
    QVERIFY(output.armed);

    // Armed from the screen, the joystick lost: disarm, and it does not come back with the joystick
    (void) runFrames(logic, joystick(1, 0, 0, -1.0), now, 1);
    output = logic.step(screen(joystick(1, 0, 0, -1), Request::Arm), now);
    now += kFrame;
    QVERIFY(output.armed);
    output = logic.step(Input(), now);
    now += kFrame;
    QVERIFY(!output.armed);
    output = runFrames(logic, joystick(2, 0, 0, -1), now, 10);
    QVERIFY(!output.armed);

    // No joystick: refused
    output = logic.step(screen(Input(), Request::Arm), now);
    QVERIFY(output.screenArmRefused == UdpRcLogic::ArmBlock::NoJoystick);
    QVERIFY(!output.armed);
}

void UdpRcLogicTest::_screenArmWithoutSwitchTest()
{
    using Request = UdpRcLogic::ArmRequest;
    UdpRcSettings settings;
    settings.armAxis = -1;  // no arm switch on this joystick
    UdpRcLogic logic(settings);
    double now = 0;
    UdpRcLogic::Output output = runFrames(logic, joystick(1, 0, 0, 1.0), now, 5);
    QVERIFY(!output.armed);
    output = logic.step(screen(joystick(1, 0, 0, 1.0), Request::Arm), now);
    now += kFrame;
    QVERIFY(output.armed);
    output = runFrames(logic, joystick(1, 0.5, 0.5, 1.0), now, 20);
    QVERIFY(output.armed);
    output = logic.step(screen(joystick(1, 0.5, 0.5, 1.0), Request::Disarm), now);
    QVERIFY(!output.armed);
    QCOMPARE(output.channels[7], quint16(173));
}

UT_REGISTER_TEST_LIGHTWEIGHT(UdpRcLogicTest, TestLabel::Unit, TestLabel::Joystick)
