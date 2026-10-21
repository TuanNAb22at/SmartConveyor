#pragma once

#include <cstdint>

class IEncoder {
public:
    virtual ~IEncoder() = default;
    virtual std::int64_t positionTicks() const = 0;
};
