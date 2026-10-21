#pragma once

#include <cstdint>
#include "hal/interfaces/IEncoder.h"

class MockEncoder final : public IEncoder {
public:
    std::int64_t positionTicks() const override;
    void addTicks(std::int64_t ticks);
    void reset();

private:
    std::int64_t ticks_{0};
};
