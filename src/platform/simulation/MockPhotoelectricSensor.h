#pragma once

#include "hal/interfaces/ISensor.h"
#include "domain/ProductType.h"

class MockPhotoelectricSensor final : public ISensor {
public:
    SensorReading read() override;
    void setDetected(bool detected, ProductType expectedType = ProductType::UNKNOWN);
    void setValid(bool valid);

private:
    bool detected_{false};
    bool valid_{true};
    ProductType expectedType_{ProductType::UNKNOWN};
};
