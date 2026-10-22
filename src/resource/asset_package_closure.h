#pragma once

#include "resource/internal_asset_package.h"

namespace arp::resource {

struct AssetPackageClosureFile final {
    core::pmr::String path{&core::defaultCpuMemoryResource()};
    std::size_t byteCount = 0u;
    std::uint64_t contentHash = 0u;
};

struct AssetPackageClosureResult final {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    InternalAssetPackageDesc package;
    core::pmr::Vector<AssetPackageClosureFile> files{&core::defaultCpuMemoryResource()};
    core::pmr::String indexYaml{&core::defaultCpuMemoryResource()};
    std::size_t maxDecodedBytes = 0u;
};

struct AssetPackageClosureBudget final {
    std::size_t maxResources = 0u;
    std::size_t maxFiles = 0u;
    std::size_t maxFileBytes = 0u;
    std::size_t maxTotalFileBytes = 0u;
    std::size_t maxDecodedBytes = 0u;
};

// Cold tooling/resource preparation only. Validates an explicit file closure;
// never recursively scans directories, imports assets or invents missing files.
class ARP_API AssetPackageClosureService final {
public:
    static constexpr std::size_t maxMetadataBytes = 1024u * 1024u;
    static constexpr std::size_t maxResources = 4096u;
    AssetPackageClosureResult inspect(const std::filesystem::path& packageDirectory,
        const AssetPackageClosureBudget& limits = {}) const;
    // Cold producer operation on caller-owned preparation storage. Writes
    // control metadata from validated bindings, then verifies the full closure.
    // Only publish() commits a user-visible package directory.
    AssetPackageClosureResult prepareIndex(const InternalAssetPackageDesc& package,
        const std::filesystem::path& directory,
        const AssetPackageClosureBudget& budget) const;
    AssetPackageClosureResult publish(const std::filesystem::path& preparedDirectory,
        const std::filesystem::path& outputDirectory,
        const AssetPackageClosureBudget& limits = {}) const;
};

} // namespace arp::resource
