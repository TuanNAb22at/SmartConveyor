#include "platform/simulation/MockDiverter.h"

void MockDiverter::divertTo(LaneId lane) {
    selectedLane_ = lane;
    active_ = true;
}

void MockDiverter::reset() {
    selectedLane_ = LaneId::REJECT_LANE;
    active_ = false;
}

LaneId MockDiverter::selectedLane() const {
    return selectedLane_;
}

bool MockDiverter::isActive() const {
    return active_;
}
