#pragma once

#include "domain/ProductType.h"

struct SensorReading {
    bool detected{false};
    bool valid{true};
    ProductType expectedType{ProductType::UNKNOWN};
};
