#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>

#include "application/ConveyorController.h"
#include "diagnostics/ConsoleLogger.h"
#include "platform/simulation/MockDiverter.h"
#include "platform/simulation/MockEncoder.h"
#include "platform/simulation/MockMotor.h"
#include "platform/simulation/MockPhotoelectricSensor.h"

namespace {
ProductType parseType(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (value == "A") return ProductType::TYPE_A;
    if (value == "B") return ProductType::TYPE_B;
    if (value == "C") return ProductType::TYPE_C;
    return ProductType::UNKNOWN;
}

void showStatus(const ConveyorController& controller, const MockMotor& motor,
                const MockDiverter& diverter) {
    std::cout << "\nTrang thai: " << ConveyorController::stateName(controller.state())
              << " | dong co: " << (motor.isRunning() ? "BAT" : "TAT")
              << " | encoder: " << controller.encoderPosition() << " xung"
              << " | co cau gat: " << ConveyorController::laneName(diverter.selectedLane())
              << " | loi: " << ConveyorController::faultName(controller.fault()) << '\n';
    const auto& pending = controller.pendingProducts();
    std::cout << "San pham tren bang tai: " << pending.size();
    if (!pending.empty()) {
        std::cout << "\n  Ma  Loai  Xung dich  Lan\n";
        for (const auto& product : pending) {
            std::cout << "  " << product.id << "   "
                      << ConveyorController::productTypeName(product.type) << "     "
                      << product.positionTicks + controller.destinationDistanceTicks() << "       "
                      << ConveyorController::laneName(product.targetLane) << '\n';
        }
    } else {
        std::cout << '\n';
    }
}

void showStatistics(const ConveyorController& controller) {
    const auto& s = controller.statistics();
    std::cout << "Da phat hien: " << s.detected << " | Lan 1: " << s.lane1
              << " | Lan 2: " << s.lane2 << " | Lan 3: " << s.lane3
              << " | Loai bo: " << s.rejected << " | So loi: " << s.faults << '\n';
}

void showHelp() {
    std::cout << "Cac lenh:\n"
              << "  them A|B|C|X      Them san pham (X/khong ro se vao lan loai bo)\n"
              << "  tien N            Cho bang tai tien N xung encoder\n"
              << "  trangthai         Xem dong co, encoder, hang doi va loi\n"
              << "  thongke           Xem tong so san pham theo tung lan\n"
              << "  loi-cam-bien      Mo phong du lieu cam bien khong hop le\n"
              << "  cam-bien-ok       Khoi phuc du lieu cam bien\n"
              << "  ket-bang-tai      Mo phong encoder dung khi co san pham\n"
              << "  khoi-phuc         Xoa loi va khoi dong lai bang tai\n"
              << "  dung | chay       Dung hoac chay bang tai\n"
              << "  trogiup           Hien danh sach lenh\n"
              << "  thoat             Dong chuong trinh\n\n"
              << "San pham den co cau gat sau 100 xung ke tu luc duoc phat hien.\n";
}
}

int main() {
    ConsoleLogger logger;
    MockPhotoelectricSensor entrySensor;
    MockEncoder encoder;
    MockMotor motor;
    MockDiverter diverter;
    ConveyorController controller(entrySensor, encoder, motor, diverter, logger);
    controller.initialize();

    std::cout << "Mo phong bang tai phan loai san pham\n"
              << "Nhap 'trogiup' de xem lenh. Loai A/B/C tuong ung lan 1/2/3.\n";
    showStatus(controller, motor, diverter);

    std::string line;
    while (std::cout << "\nbang-tai> " && std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::string command;
        input >> command;
        if (command.empty()) continue;
        std::transform(command.begin(), command.end(), command.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (command == "thoat") break;
        if (command == "trogiup") {
            showHelp();
        } else if (command == "them") {
            std::string typeText;
            if (!(input >> typeText)) {
                std::cout << "Cach dung: them A|B|C|X\n";
            } else if (controller.state() != ControllerState::RUNNING) {
                std::cout << "Hay chay hoac khoi phuc bang tai truoc khi them san pham.\n";
            } else {
                const auto type = parseType(typeText);
                entrySensor.setDetected(true, type);
                controller.update();
                entrySensor.setDetected(false);
                controller.update();
            }
        } else if (command == "tien") {
            long long ticks = 0;
            if (!(input >> ticks) || ticks < 0 || ticks > 100000) {
                std::cout << "Cach dung: tien N (N tu 0 den 100000)\n";
            } else if (controller.state() != ControllerState::RUNNING) {
                std::cout << "Bang tai dang dung. Hay go 'chay' hoac 'khoi-phuc'.\n";
            } else {
                for (long long i = 0; i < ticks && controller.state() == ControllerState::RUNNING; ++i) {
                    encoder.addTicks(1);
                    controller.update();
                }
                if (ticks == 0) controller.update();
            }
        } else if (command == "trangthai") {
            showStatus(controller, motor, diverter);
        } else if (command == "thongke") {
            showStatistics(controller);
        } else if (command == "loi-cam-bien") {
            entrySensor.setValid(false);
            controller.update();
        } else if (command == "cam-bien-ok") {
            entrySensor.setValid(true);
            std::cout << "Cam bien da hoat dong lai. Go 'khoi-phuc' de chay lai neu dang bao loi.\n";
        } else if (command == "ket-bang-tai") {
            if (controller.pendingProducts().empty()) {
                std::cout << "Hay them san pham truoc de mo phong bang tai bi ket.\n";
            } else if (controller.state() != ControllerState::RUNNING) {
                std::cout << "Bang tai dang dung.\n";
            } else {
                for (std::size_t i = 0; i < controller.jamTimeoutUpdates() &&
                     controller.state() == ControllerState::RUNNING; ++i) controller.update();
            }
        } else if (command == "khoi-phuc") {
            if (controller.state() != ControllerState::FAULT) {
                std::cout << "He thong khong co loi can khoi phuc.\n";
            } else {
                controller.resetFault();
            }
        } else if (command == "dung") {
            controller.shutdown();
        } else if (command == "chay") {
            if (controller.state() == ControllerState::FAULT) {
                std::cout << "Hay khoi phuc loi bang lenh 'khoi-phuc' truoc.\n";
            } else {
                controller.initialize();
            }
        } else {
            std::cout << "Lenh khong hop le. Go 'trogiup' de xem danh sach lenh.\n";
        }

        if (command != "trangthai" && command != "thongke" && command != "trogiup")
            showStatus(controller, motor, diverter);
    }

    controller.shutdown();
    showStatistics(controller);
    return 0;
}
