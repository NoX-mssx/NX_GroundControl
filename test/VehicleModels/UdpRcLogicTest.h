#pragma once

#include "UnitTest.h"

/// UDP RC frames of the TX12 Ethernet module ("Zmei"): values as zmiy_rc_emulator.py sends them and its safety rules
class UdpRcLogicTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _crsfRoundingTest();
    void _restFrameTest();
    void _stickValuesTest();
    void _throttleGuardTest();
    void _armTest();
    void _joystickLossTest();
    void _slewTest();
    void _throttleScaleTest();
    void _armGuardTest();
    void _settingsTest();
    void _screenArmTest();
    void _screenArmWithoutSwitchTest();
    void _controlsChangeTest();
};
