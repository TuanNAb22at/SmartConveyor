#pragma once

#include "SensorReading.h"

class ISensor {
public:
    virtual ~ISensor() = default;
    virtual SensorReading read() = 0;
};
