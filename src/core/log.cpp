#include "core/log.h"

#include <iostream>

namespace arp {

namespace {

const char* labelFor(LogLevel level)
{
    switch (level) {
    case LogLevel::Info:
        return "info";
    case LogLevel::Warning:
        return "warn";
    case LogLevel::Error:
        return "error";
    }

    return "unknown";
}

} // namespace

void log(LogLevel level, std::string_view message)
{
    std::cout << "[" << labelFor(level) << "] " << message << '\n';
}

void logInfo(std::string_view message)
{
    log(LogLevel::Info, message);
}

void logWarning(std::string_view message)
{
    log(LogLevel::Warning, message);
}

void logError(std::string_view message)
{
    log(LogLevel::Error, message);
}

} // namespace arp
