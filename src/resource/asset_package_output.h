#pragma once

#include "core/memory/cpu_allocator.h"

#include <cstddef>
#include <cstdint>

namespace arp::resource {

enum class AssetPackageOutputRole : std::uint8_t { Manifest, Envelope, Payload, Closure };

struct AssetPackageOutputFile {
    AssetPackageOutputRole role = AssetPackageOutputRole::Payload;
    core::pmr::String path{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint8_t> bytes{&core::defaultCpuMemoryResource()};
};

// All limits are explicit. Metadata limits exclude payload files; output limits
// cover every role. Preparation limits cover scoped CPU work and retained output.
struct AssetPackageOutputBudget {
    std::uint32_t contractVersion = 1;
    std::size_t maxResources = 0;
    std::size_t maxMetadataFileBytes = 0;
    std::size_t maxMetadataTotalBytes = 0;
    std::size_t maxOutputFiles = 0;
    std::size_t maxOutputBytes = 0;
    std::size_t maxPreparationBytes = 0;
};

// Failure never publishes partial files. The stable ID remains usable when an
// allocation failure prevents constructing an owned detail string.
struct AssetPackageOutputResult {
    bool success = false;
    const char* diagnosticId = nullptr;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<AssetPackageOutputFile> files{&core::defaultCpuMemoryResource()};
};

} // namespace arp::resource
