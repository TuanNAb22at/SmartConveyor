#include "platform/simulation/MockEncoder.h"

std::int64_t MockEncoder::positionTicks() const {
    return ticks_;
}

void MockEncoder::addTicks(std::int64_t ticks) {
    ticks_ += ticks;
}

void MockEncoder::reset() {
    ticks_ = 0;
}
