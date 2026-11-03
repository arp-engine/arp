#pragma once

#include "assets/glb_static_mesh_importer.h"
#include "resource/resource_manager.h"

namespace arp::resource {

struct EnvironmentLodSource final {
    core::pmr::String path;
    std::size_t byteCount = 0;
    core::io::FileVersionToken version;
    assets::NamedVariantSourceImportLimits limits;
    // Explicit alias to a preceding level, with an identical content identity.
    std::uint32_t sharedLod = UINT32_MAX;
};

struct EnvironmentLodSourceDesc final {
    static constexpr std::uint32_t maxLevels = 3;
    ResourceFileOrigin origin;
    core::pmr::Vector<EnvironmentLodSource> levels;
    core::pmr::Vector<core::pmr::String> variantNames;
    std::uint32_t residentLevels = 0;
    std::size_t maxTotalSourceBytes = 0, maxDecodedBytes = 0;
    std::uint64_t maxTotalImportBytes = 0;
    assets::NamedVariantGeometrySpace geometrySpace =
        assets::NamedVariantGeometrySpace::RootRelative;
};

struct EnvironmentLodData final {
    std::uint32_t lodIndex = 0, sourceLod = 0;
    core::pmr::Vector<assets::NamedVariant> variants;
    core::pmr::Vector<assets::ImportedMaterialAsset> materials;
    core::pmr::Vector<assets::ImportedTextureAsset> textures;
    core::pmr::Vector<assets::ImportedImageAsset> images;
};

struct EnvironmentLodPreparation final {
    bool success = false;
    core::pmr::Vector<EnvironmentLodData> levels;
    std::size_t decodedBytes = 0;
    std::uint64_t importPeakUpperBound = 0;
    core::pmr::String diagnostic;
};

// Shared Plant/Rock source preparation. It owns no registry or asynchronous IO;
// FileService acquisition and ResourceManager candidate publication stay shared.
class ARP_API EnvironmentLodSourceLoader final {
public:
    static const char* validate(const EnvironmentLodSourceDesc&);
    static bool collect(const EnvironmentLodSourceDesc&,
        core::pmr::Vector<ResourceFileReadIntent>&,
        core::pmr::String&);
    static EnvironmentLodPreparation prepare(
        const EnvironmentLodSourceDesc&, const ResourceLoadContext&);
    static void fingerprint(const EnvironmentLodSourceDesc&, core::StableHash64&) noexcept;
    static const EnvironmentLodData* resolve(
        const core::pmr::Vector<EnvironmentLodData>&, std::uint32_t) noexcept;
};
} // namespace arp::resource
