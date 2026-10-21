#pragma once

#include "hal/interfaces/IMotor.h"

class MockMotor final : public IMotor {
public:
    void start() override;
    void stop() override;
    bool isRunning() const override;

private:
    bool running_{false};
};
