#include "diagnostics/ConsoleLogger.h"

#include <iostream>

void ConsoleLogger::info(const std::string& message) {
    std::cout << "[THONG TIN] " << message << '\n';
}

void ConsoleLogger::error(const std::string& message) {
    std::cerr << "[LOI] " << message << '\n';
}
