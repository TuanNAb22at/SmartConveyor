#pragma once

class IMotor {
public:
    virtual ~IMotor() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
};
