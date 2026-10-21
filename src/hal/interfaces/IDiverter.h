#pragma once

#include "domain/LaneId.h"

class IDiverter {
public:
    virtual ~IDiverter() = default;
    virtual void divertTo(LaneId lane) = 0;
    virtual void reset() = 0;
};
