#include "application/ConveyorController.h"

#include <algorithm>
#include <string>

#include "diagnostics/ILogger.h"

ConveyorController::ConveyorController(ISensor& entrySensor, IEncoder& encoder,
    IMotor& motor, IDiverter& diverter, ILogger& logger,
    std::int64_t destinationDistanceTicks, std::int64_t diverterHoldTicks,
    std::size_t jamTimeoutUpdates)
    : entrySensor_(entrySensor), encoder_(encoder), motor_(motor),
      diverter_(diverter), logger_(logger),
      destinationDistanceTicks_(std::max<std::int64_t>(1, destinationDistanceTicks)),
      diverterHoldTicks_(std::max<std::int64_t>(1, diverterHoldTicks)),
      jamTimeoutUpdates_(std::max<std::size_t>(1, jamTimeoutUpdates)),
      previousEncoderPosition_(encoder.positionTicks()) {}

void ConveyorController::initialize() {
    if (state_ == ControllerState::FAULT) return;
    diverter_.reset();
    diverterActive_ = false;
    motor_.start();
    state_ = ControllerState::RUNNING;
    previousEncoderPosition_ = encoder_.positionTicks();
    logger_.info("Bang tai dang chay");
}

void ConveyorController::update() {
    if (state_ != ControllerState::RUNNING) return;

    const auto currentPosition = encoder_.positionTicks();
    const auto reading = entrySensor_.read();
    if (!reading.valid) {
        enterFault(FaultCode::SENSOR_INVALID, "Du lieu cam bien dau vao khong hop le");
        return;
    }

    if (reading.detected && !previousDetected_) {
        Product product;
        product.id = nextProductId_++;
        product.type = reading.expectedType;
        product.positionTicks = currentPosition;
        product.targetLane = laneFor(product.type);
        products_.push_back(product);
        ++statistics_.detected;
        logger_.info("Da phat hien san pham #" + std::to_string(product.id) + " (" +
            productTypeName(product.type) + ") tai " + std::to_string(currentPosition) +
            " xung; dich den=" + laneName(product.targetLane));
    }
    previousDetected_ = reading.detected;

    if (diverterActive_ && currentPosition >= diverterResetAt_) {
        diverter_.reset();
        diverterActive_ = false;
        logger_.info("Co cau gat da ve vi tri ban dau");
    }

    if (!diverterActive_) {
        for (auto it = products_.begin(); it != products_.end(); ++it) {
            const auto targetPosition = it->positionTicks + destinationDistanceTicks_;
            if (currentPosition < targetPosition) continue;

            diverter_.divertTo(it->targetLane);
            diverterActive_ = true;
            diverterResetAt_ = currentPosition + diverterHoldTicks_;
            it->diverted = true;
            countLane(it->targetLane);
            sortingRecords_.push_back(SortingRecord{
                it->id, it->type, it->targetLane, it->positionTicks, currentPosition
            });
            logger_.info("San pham #" + std::to_string(it->id) + " da duoc gat sang " +
                laneName(it->targetLane) + " tai " + std::to_string(currentPosition) + " xung");
            products_.erase(it);
            break;
        }
    }

    if (!products_.empty() && currentPosition == previousEncoderPosition_ && motor_.isRunning()) {
        if (++stalledUpdates_ >= jamTimeoutUpdates_) {
            enterFault(FaultCode::CONVEYOR_JAM, "Bang tai bi ket: encoder khong tang xung khi co san pham");
            return;
        }
    } else {
        stalledUpdates_ = 0;
    }
    previousEncoderPosition_ = currentPosition;
}

void ConveyorController::shutdown() {
    motor_.stop();
    diverter_.reset();
    diverterActive_ = false;
    if (state_ != ControllerState::FAULT) state_ = ControllerState::IDLE;
    logger_.info("Bang tai da dung");
}

bool ConveyorController::resetFault() {
    if (fault_ == FaultCode::SENSOR_INVALID && !entrySensor_.read().valid) {
        logger_.error("Khong the khoi phuc: cam bien van bao du lieu loi");
        return false;
    }
    fault_ = FaultCode::NONE;
    stalledUpdates_ = 0;
    previousDetected_ = false;
    previousEncoderPosition_ = encoder_.positionTicks();
    state_ = ControllerState::IDLE;
    initialize();
    logger_.info("Da xoa loi va khoi dong lai bang tai");
    return true;
}

bool ConveyorController::setDestinationDistanceTicks(std::int64_t distanceTicks) {
    if (distanceTicks <= 0 || !products_.empty()) return false;
    destinationDistanceTicks_ = distanceTicks;
    logger_.info("Da cap nhat khoang cach gat: " + std::to_string(distanceTicks) + " xung");
    return true;
}

void ConveyorController::enterFault(FaultCode fault, const std::string& message) {
    fault_ = fault;
    state_ = ControllerState::FAULT;
    ++statistics_.faults;
    motor_.stop();
    diverter_.reset();
    diverterActive_ = false;
    logger_.error(std::string("HE THONG GAP LOI: ") + message);
}

void ConveyorController::countLane(LaneId lane) {
    switch (lane) {
    case LaneId::LANE_1: ++statistics_.lane1; break;
    case LaneId::LANE_2: ++statistics_.lane2; break;
    case LaneId::LANE_3: ++statistics_.lane3; break;
    case LaneId::REJECT_LANE: ++statistics_.rejected; break;
    }
}

ControllerState ConveyorController::state() const { return state_; }
FaultCode ConveyorController::fault() const { return fault_; }
const SortingStatistics& ConveyorController::statistics() const { return statistics_; }
const std::vector<Product>& ConveyorController::pendingProducts() const { return products_; }
const std::vector<SortingRecord>& ConveyorController::sortingRecords() const { return sortingRecords_; }
std::int64_t ConveyorController::encoderPosition() const { return encoder_.positionTicks(); }
std::int64_t ConveyorController::destinationDistanceTicks() const { return destinationDistanceTicks_; }
std::size_t ConveyorController::jamTimeoutUpdates() const { return jamTimeoutUpdates_; }

LaneId ConveyorController::laneFor(ProductType type) {
    switch (type) {
    case ProductType::TYPE_A: return LaneId::LANE_1;
    case ProductType::TYPE_B: return LaneId::LANE_2;
    case ProductType::TYPE_C: return LaneId::LANE_3;
    case ProductType::UNKNOWN: return LaneId::REJECT_LANE;
    }
    return LaneId::REJECT_LANE;
}

const char* ConveyorController::productTypeName(ProductType type) {
    switch (type) {
    case ProductType::TYPE_A: return "A";
    case ProductType::TYPE_B: return "B";
    case ProductType::TYPE_C: return "C";
    case ProductType::UNKNOWN: return "Khong ro";
    }
    return "Khong ro";
}

const char* ConveyorController::laneName(LaneId lane) {
    switch (lane) {
    case LaneId::LANE_1: return "Lan 1";
    case LaneId::LANE_2: return "Lan 2";
    case LaneId::LANE_3: return "Lan 3";
    case LaneId::REJECT_LANE: return "Loai bo";
    }
    return "Loai bo";
}

const char* ConveyorController::stateName(ControllerState state) {
    switch (state) {
    case ControllerState::IDLE: return "DA DUNG";
    case ControllerState::RUNNING: return "DANG CHAY";
    case ControllerState::FAULT: return "BAO LOI";
    }
    return "KHONG RO";
}

const char* ConveyorController::faultName(FaultCode fault) {
    switch (fault) {
    case FaultCode::NONE: return "Khong co";
    case FaultCode::SENSOR_INVALID: return "Cam bien khong hop le";
    case FaultCode::CONVEYOR_JAM: return "Bang tai bi ket";
    }
    return "Loi khong xac dinh";
}
