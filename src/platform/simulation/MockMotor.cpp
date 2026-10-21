#include "platform/simulation/MockMotor.h"

void MockMotor::start() {
    running_ = true;
}

void MockMotor::stop() {
    running_ = false;
}

bool MockMotor::isRunning() const {
    return running_;
}
