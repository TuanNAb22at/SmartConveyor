#pragma once

#include "diagnostics/ILogger.h"

class ConsoleLogger final : public ILogger {
public:
    void info(const std::string& message) override;
    void error(const std::string& message) override;
};
