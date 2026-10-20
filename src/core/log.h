#pragma once

#include "core/api.h"

#include <string_view>

namespace arp {

enum class LogLevel { Info, Warning, Error };

ARP_API void log(LogLevel level, std::string_view message);
ARP_API void logInfo(std::string_view message);
ARP_API void logWarning(std::string_view message);
ARP_API void logError(std::string_view message);

} // namespace arp
