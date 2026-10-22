#pragma once

#include "resource/vegetation_catalog_cooked.h"

namespace arp::resource {
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
    VegetationCatalogPrepareResult prepare(
        std::string_view yaml, const std::filesystem::path& sourceRoot) const;
    VegetationCatalogCookedResult build(const std::filesystem::path& authoringFile,
        const std::filesystem::path& outputDirectory) const;
};
} // namespace arp::resource
