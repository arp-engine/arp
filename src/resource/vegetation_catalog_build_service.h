#pragma once

#include "resource/resource_manager.h"
#include "resource/vegetation_catalog_cooked.h"

namespace arp::resource {
// Logical root and source ownership are explicit; this profile never opens files.
struct VegetationCatalogSourceDesc final {
    core::pmr::String sourceRoot;
    ResourceFileOrigin origin;
};

struct VegetationCatalogPrepareResult final {
    bool success = false;
    ResourceReference reference;
    std::shared_ptr<VegetationCatalogResourceData> data;
    std::size_t decodedBytes = 0u;
    std::uint64_t sourceBytes = 0u, importPeakBytes = 0u;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

// Cold YAML authoring -> strict GLB geometry -> cooked typed catalog package.
// Source material appearance and vertex wind inference are never imported.
class ARP_API VegetationCatalogBuildService final {
public:
    static constexpr std::size_t maxAuthoringBytes = 1024u * 1024u;
    // Replaces reads only on success; semantic template validation occurs in prepare.
    bool collectSourceReadIntents(std::string_view yaml,
        const VegetationCatalogSourceDesc& source,
        core::pmr::Vector<ResourceFileReadIntent>& reads,
        core::pmr::String& diagnostic) const;
    VegetationCatalogPrepareResult prepareFromSources(std::string_view yaml,
        const VegetationCatalogSourceDesc& source,
        const ResourceLoadContext& context) const;
    // Legacy cold-host adapter; package publication remains an explicit native tool operation.
    VegetationCatalogPrepareResult prepare(
        std::string_view yaml, const std::filesystem::path& sourceRoot) const;
    VegetationCatalogCookedResult build(const std::filesystem::path& authoringFile,
        const std::filesystem::path& outputDirectory) const;

private:
    VegetationCatalogPrepareResult prepareImpl(std::string_view yaml,
        const std::filesystem::path& sourceRoot,
        const ResourceLoadContext* context,
        const ResourceFileOrigin* origin) const;
};
} // namespace arp::resource
