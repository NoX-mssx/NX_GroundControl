#pragma once

#include "UnitTest.h"

/// The Starlink dish's get_location request and the reading of its answer
class StarlinkPositionTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _testLocationRequest();
    void _testParseLocation();
    void _testParseStatusError();
    void _testParseNoPosition();
    void _testParseDamaged();
};
