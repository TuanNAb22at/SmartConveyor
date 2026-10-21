#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "domain/Product.h"
#include "hal/interfaces/IDiverter.h"
#include "hal/interfaces/IEncoder.h"
#include "hal/interfaces/IMotor.h"
#include "hal/interfaces/ISensor.h"

class ILogger;

enum class ControllerState { IDLE, RUNNING, FAULT };
enum class FaultCode { NONE, SENSOR_INVALID, CONVEYOR_JAM };

struct SortingStatistics {
    std::uint64_t detected{0};
    std::uint64_t lane1{0};
    std::uint64_t lane2{0};
    std::uint64_t lane3{0};
    std::uint64_t rejected{0};
    std::uint64_t faults{0};
};

struct SortingRecord {
    std::uint32_t id{0};
    ProductType type{ProductType::UNKNOWN};
    LaneId lane{LaneId::REJECT_LANE};
    std::int64_t detectedAtTicks{0};
    std::int64_t sortedAtTicks{0};
};

class ConveyorController {
public:
    ConveyorController(ISensor& entrySensor, IEncoder& encoder, IMotor& motor,
                       IDiverter& diverter, ILogger& logger,
                       std::int64_t destinationDistanceTicks = 100,
                       std::int64_t diverterHoldTicks = 5,
                       std::size_t jamTimeoutUpdates = 20);

    void initialize();
    void update();
    void shutdown();
    bool resetFault();
    bool setDestinationDistanceTicks(std::int64_t distanceTicks);

    ControllerState state() const;
    FaultCode fault() const;
    const SortingStatistics& statistics() const;
    const std::vector<Product>& pendingProducts() const;
    const std::vector<SortingRecord>& sortingRecords() const;
    std::int64_t encoderPosition() const;
    std::int64_t destinationDistanceTicks() const;
    std::size_t jamTimeoutUpdates() const;

    static LaneId laneFor(ProductType type);
    static const char* productTypeName(ProductType type);
    static const char* laneName(LaneId lane);
    static const char* stateName(ControllerState state);
    static const char* faultName(FaultCode fault);

private:
    void enterFault(FaultCode fault, const std::string& message);
    void countLane(LaneId lane);

    ISensor& entrySensor_;
    IEncoder& encoder_;
    IMotor& motor_;
    IDiverter& diverter_;
    ILogger& logger_;
    std::int64_t destinationDistanceTicks_;
    std::int64_t diverterHoldTicks_;
    std::size_t jamTimeoutUpdates_;
    std::uint32_t nextProductId_{1};
    bool previousDetected_{false};
    ControllerState state_{ControllerState::IDLE};
    FaultCode fault_{FaultCode::NONE};
    SortingStatistics statistics_;
    std::vector<Product> products_;
    std::vector<SortingRecord> sortingRecords_;
    std::int64_t previousEncoderPosition_{0};
    std::size_t stalledUpdates_{0};
    bool diverterActive_{false};
    std::int64_t diverterResetAt_{0};
};
