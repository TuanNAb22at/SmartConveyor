#pragma once

#include <cstdint>
#include "LaneId.h"
#include "ProductType.h"

struct Product {
    std::uint32_t id{0};
    ProductType type{ProductType::UNKNOWN};
    // Relative encoder ticks from a chosen reference point.
    std::int64_t positionTicks{0};
    LaneId targetLane{LaneId::REJECT_LANE};
    bool diverted{false};
};
