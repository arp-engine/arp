#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/reflection.h"
#include "resource/resource_manager.h"

#include <cstdint>
#include <filesystem>

namespace arp::resource {

class AssetPackageCodecRegistry;

constexpr NameView kInternalAssetPackageFormat = "ArpInternalAssetPackage";
constexpr std::uint32_t kInternalAssetPackageVersion = 1;

struct ResourceEnvelopeBuildDesc {
    core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
    core::pmr::String resourceType{&core::defaultCpuMemoryResource()};
    core::pmr::String alias{&core::defaultCpuMemoryResource()};
    core::pmr::String displayName{&core::defaultCpuMemoryResource()};
    core::pmr::String status{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ResourceDependency> dependencies{&core::defaultCpuMemoryResource()};
};

struct ResourceEnvelopeBuildResult {
    bool success = false;
    core::pmr::String envelopeYaml{&core::defaultCpuMemoryResource()};
    std::uint32_t contentHash = 0;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

struct InternalAssetPackageEntry {
    core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
    core::pmr::String resourceType{&core::defaultCpuMemoryResource()};
    core::pmr::String alias{&core::defaultCpuMemoryResource()};
    core::pmr::String envelopePath{&core::defaultCpuMemoryResource()};
    core::pmr::String status{&core::defaultCpuMemoryResource()};
    std::uint32_t contentHash = 0;
    core::pmr::Vector<ResourceDependency> dependencies{&core::defaultCpuMemoryResource()};
    core::pmr::String envelopeYaml{&core::defaultCpuMemoryResource()};
};

struct InternalAssetPackageDesc {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    core::pmr::String status{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<InternalAssetPackageEntry> entries{&core::defaultCpuMemoryResource()};
    // Explicit opt-in to resource.package.closure/v1; never inferred from file existence.
    core::pmr::String closurePath{&core::defaultCpuMemoryResource()};
};

struct InternalAssetPackageValidationResult {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    InternalAssetPackageDesc package;
};

ARP_API ResourceEnvelopeBuildResult buildResourceEnvelope(
    const ResourceEnvelopeBuildDesc& desc, const reflection::object& payload);
// Explicit domain YAML payloads share the canonical hash/envelope writer.
ARP_API ResourceEnvelopeBuildResult buildResourceEnvelopeFromYaml(
    const ResourceEnvelopeBuildDesc& desc, NameView payloadYaml);

class ARP_API InternalAssetPackageWriter final {
public:
    InternalAssetPackageValidationResult write(
        const InternalAssetPackageDesc& desc, const std::filesystem::path& outputDirectory) const;
};

class ARP_API InternalAssetPackageReader final {
public:
    InternalAssetPackageValidationResult read(const std::filesystem::path& packageDirectory,
        std::size_t maxMetadataBytes = 0u,
        std::size_t maxResources = 0u) const;

    ResourceLoadResult load(const std::filesystem::path& packageDirectory,
        ResourceManager& manager,
        ResourceLoadPolicy policy = {}) const;
    ResourceLoadResult load(const std::filesystem::path& packageDirectory,
        ResourceManager& manager,
        core::SharedPtr<const AssetPackageCodecRegistry> codecs,
        ResourceLoadPolicy policy = {}) const;
};

} // namespace arp::resource
