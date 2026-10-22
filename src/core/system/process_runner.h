#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"

#include <filesystem>

namespace arp::core {

struct ProcessRunRequest {
    std::filesystem::path executable;
    pmr::Vector<pmr::String> arguments{&defaultCpuMemoryResource()};
    std::filesystem::path workingDirectory;
    std::filesystem::path outputFile;
};

struct ProcessRunResult {
    bool launched = false;
    std::uint32_t exitCode = 0u;
    pmr::String diagnostic{&defaultCpuMemoryResource()};

    bool ok() const noexcept { return launched && exitCode == 0u; }
};

class ARP_API ProcessRunner final {
public:
    // Executes one host tool with an explicit argv contract. Implementations
    // never invoke a shell; Emscripten reports deterministic unavailability.
    ProcessRunResult run(const ProcessRunRequest& request) const;
};

} // namespace arp::core
