#include "platform/simulation/MockPhotoelectricSensor.h"

SensorReading MockPhotoelectricSensor::read() {
    return SensorReading{detected_, valid_, expectedType_};
}

void MockPhotoelectricSensor::setDetected(bool detected, ProductType expectedType) {
    detected_ = detected;
    expectedType_ = detected ? expectedType : ProductType::UNKNOWN;
}

void MockPhotoelectricSensor::setValid(bool valid) {
    valid_ = valid;
}
