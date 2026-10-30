#pragma once

#include "resource/vegetation_catalog_resource.h"

namespace arp::resource {

inline constexpr ResourceContractDescriptor kVegetationCatalogCookedContract{
    "VegetationCatalogResource", "vegetation.catalog.cooked/v1"};

struct VegetationCatalogCookedResult final {
    bool success = false;
    std::size_t decodedBytes = 0u;
    std::uint64_t contentFingerprint = 0u;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ResourceFileReadIntent> files{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ResourceDependency> dependencies{&core::defaultCpuMemoryResource()};
    std::shared_ptr<VegetationCatalogResourceData> data;
};

// CPU preparation/serialization, shared by offline producers and typed loading.
// No GLB importer, procedural generation, renderer handle or GPU upload.
class ARP_API VegetationCatalogCookedCodec final {
public:
    static constexpr std::size_t maxManifestBytes = 1024u * 1024u;
    static constexpr std::size_t maxTemplates = 4096u;

    VegetationCatalogCookedResult inspect(const ResourceReference& reference,
        const std::filesystem::path& manifest,
        const ResourceLoadContext& context = {}) const;
    VegetationCatalogCookedResult inspect(const ResourceReference& reference,
        const std::filesystem::path& manifest,
        const ResourceLoadContext& context,
        ResourceFileOrigin origin) const;
    VegetationCatalogCookedResult decode(const ResourceReference& reference,
        const std::filesystem::path& manifest,
        const ResourceLoadContext& context = {}) const;
    VegetationCatalogCookedResult decode(const ResourceReference& reference,
        const std::filesystem::path& manifest,
        const ResourceLoadContext& context,
        ResourceFileOrigin origin) const;
    // Persists assets/templates and validates the one-LOD template config.
    // Legacy catalog/source/import/validation aggregates are outside this schema.
    VegetationCatalogCookedResult write(const ResourceReference& reference,
        const VegetationCatalogResourceData& data,
        const std::filesystem::path& outputDirectory,
        std::size_t maxDecodedBytes) const;
};

} // namespace arp::resource
