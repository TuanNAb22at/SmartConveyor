#pragma once

#include <cstdint>

class IClock {
public:
    virtual ~IClock() = default;
    virtual std::uint64_t nowMs() const = 0;
};
