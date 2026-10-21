#pragma once

#include "hal/interfaces/IDiverter.h"

class MockDiverter final : public IDiverter {
public:
    void divertTo(LaneId lane) override;
    void reset() override;
    LaneId selectedLane() const;
    bool isActive() const;

private:
    LaneId selectedLane_{LaneId::REJECT_LANE};
    bool active_{false};
};
