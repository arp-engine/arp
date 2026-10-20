#pragma once

#include "core/cpu_allocator.h"

#include <cstdint>
#include <filesystem>

namespace arp::core {

struct TextFileReadResult {
    bool success = false;
    pmr::String text{&defaultCpuMemoryResource()};
    pmr::String diagnostics{&defaultCpuMemoryResource()};
};

struct BinaryFileReadResult {
    bool success = false;
    pmr::Vector<std::uint8_t> bytes{&defaultCpuMemoryResource()};
    pmr::String diagnostics{&defaultCpuMemoryResource()};
};

ARP_API TextFileReadResult readTextFile(
    const std::filesystem::path& path, std::uint64_t maxBytes = UINT64_MAX);
ARP_API BinaryFileReadResult readBinaryFile(
    const std::filesystem::path& path, std::uint64_t maxBytes = UINT64_MAX);

} // namespace arp::core
