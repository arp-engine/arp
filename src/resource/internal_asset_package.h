#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/reflection.h"
#include "resource/asset_package_output.h"
#include "resource/resource_manager.h"

#include <cstdint>
#include <filesystem>

namespace arp::resource {

class AssetPackageCodecRegistry;

// Complete acquired bytes with explicit logical root and mount generation.
// Source entry points never inspect the host filesystem or fetch missing data.
struct AssetPackageSourceContext {
    ResourceLoadContext resources;
    ResourceFileOrigin origin;
};

struct AssetPackageReadBudget {
    std::size_t maxMetadataBytes = 0;
    std::size_t maxResources = 0;
    std::size_t maxFileReads = 0;
    std::size_t maxFileBytes = 0;
    std::size_t maxTotalBytes = 0;
    std::size_t maxDecodedBytes = 0;
};

struct AssetPackageReadPlan {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ResourceFileReadIntent> reads{&core::defaultCpuMemoryResource()};
};

enum class AssetPackageReadPhase { Complete, Metadata };

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

// CPU candidates only: no manager registration or Ready residency. All strong
// dependencies must be in this package. Keep the acquired sources alive until
// normal candidate load/admission finishes; no borrowed context is stored here.
struct AssetPackagePreparationResult {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    InternalAssetPackageDesc package;
    core::pmr::Vector<core::UniquePtr<ResourceBase>> resources{&core::defaultCpuMemoryResource()};
    std::size_t maxDecodedBytes = 0;
};

// Validated metadata/dependency plan before runtime binary decode. Success is
// neither decoded readiness nor registration. File/content validation still runs
// in each repeatable typed descriptor before owner publication.
struct AssetPackagePreparationPlan {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    InternalAssetPackageDesc package;
    std::size_t maxDecodedBytes = 0;
};

ARP_API ResourceEnvelopeBuildResult buildResourceEnvelope(
    const ResourceEnvelopeBuildDesc& desc, const reflection::object& payload);
// Explicit domain YAML payloads share the canonical hash/envelope writer.
ARP_API ResourceEnvelopeBuildResult buildResourceEnvelopeFromYaml(
    const ResourceEnvelopeBuildDesc& desc, NameView payloadYaml);

class ARP_API InternalAssetPackageWriter final {
public:
    AssetPackageOutputResult prepare(
        const InternalAssetPackageDesc& desc, const AssetPackageOutputBudget& budget) const;
    InternalAssetPackageValidationResult write(
        const InternalAssetPackageDesc& desc, const std::filesystem::path& outputDirectory) const;
};

class ARP_API InternalAssetPackageReader final {
public:
    InternalAssetPackageValidationResult read(const std::filesystem::path& packageDirectory,
        std::size_t maxMetadataBytes = 0u,
        std::size_t maxResources = 0u) const;
    InternalAssetPackageValidationResult readSources(const std::filesystem::path& packageDirectory,
        const AssetPackageSourceContext& sources,
        std::size_t maxMetadataBytes,
        std::size_t maxResources) const;
    AssetPackageReadPlan planSourceReads(const std::filesystem::path& directory,
        const AssetPackageSourceContext& sources,
        const AssetPackageReadBudget& budget,
        core::SharedPtr<const AssetPackageCodecRegistry> codecs = {}) const;
    AssetPackageReadPlan planSourceReads(const std::filesystem::path& directory,
        const AssetPackageSourceContext& sources,
        const AssetPackageReadBudget& budget,
        core::SharedPtr<const AssetPackageCodecRegistry> codecs,
        AssetPackageReadPhase phase) const;

    // Source-only, registry-free CPU preparation. Initialize reflection schemas
    // and freeze codecs before calling from a worker. Publication is separate.
    AssetPackagePreparationResult prepareSources(const std::filesystem::path& directory,
        const AssetPackageSourceContext& sources,
        std::size_t maxMetadataBytes,
        std::size_t maxResources,
        core::SharedPtr<const AssetPackageCodecRegistry> codecs = {}) const;

    AssetPackagePreparationPlan planPreparationSources(const std::filesystem::path& directory,
        const AssetPackageSourceContext& sources,
        std::size_t maxMetadataBytes,
        std::size_t maxResources,
        core::SharedPtr<const AssetPackageCodecRegistry> codecs = {}) const;

    ResourceLoadResult load(const std::filesystem::path& packageDirectory,
        ResourceManager& manager,
        ResourceLoadPolicy policy = {}) const;
    ResourceLoadResult load(const std::filesystem::path& packageDirectory,
        ResourceManager& manager,
        core::SharedPtr<const AssetPackageCodecRegistry> codecs,
        ResourceLoadPolicy policy = {}) const;
    ResourceLoadResult loadSources(const std::filesystem::path& packageDirectory,
        ResourceManager& manager,
        const AssetPackageSourceContext& sources,
        core::SharedPtr<const AssetPackageCodecRegistry> codecs = {}) const;
    ResourceLoadResult loadSources(const std::filesystem::path& packageDirectory,
        ResourceManager& manager,
        const AssetPackageSourceContext& sources,
        std::size_t maxMetadataBytes,
        std::size_t maxResources,
        core::SharedPtr<const AssetPackageCodecRegistry> codecs = {}) const;

private:
    InternalAssetPackageValidationResult readImpl(const std::filesystem::path& packageDirectory,
        std::size_t maxMetadataBytes,
        std::size_t maxResources,
        const AssetPackageSourceContext* sources) const;
    ResourceLoadResult loadImpl(const std::filesystem::path& packageDirectory,
        ResourceManager& manager,
        core::SharedPtr<const AssetPackageCodecRegistry> codecs,
        ResourceLoadPolicy policy,
        const AssetPackageSourceContext* sources) const;
    ResourceLoadResult loadImplBounded(const std::filesystem::path&,
        ResourceManager&,
        core::SharedPtr<const AssetPackageCodecRegistry>,
        ResourceLoadPolicy,
        const AssetPackageSourceContext*,
        std::size_t maxMetadataBytes,
        std::size_t maxResources) const;
    ResourceLoadResult processImplBounded(const std::filesystem::path&,
        ResourceManager*,
        core::SharedPtr<const AssetPackageCodecRegistry>,
        ResourceLoadPolicy,
        const AssetPackageSourceContext*,
        std::size_t maxMetadataBytes,
        std::size_t maxResources,
        AssetPackagePreparationResult*,
        AssetPackagePreparationPlan* = nullptr) const;
};

} // namespace arp::resource
