#pragma once

#include "UnitTest.h"

/// The tunnel text editor: .conf text as pasted from WireGuard, and the text made from a stored tunnel
class WireGuardConfTextTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _parseTest();
    void _missingAndIgnoredTest();
    void _roundTripTest();
};
