#pragma once

#include "assets/asset_pipeline.h"
#include "core/api.h"

#include <filesystem>

namespace arp::assets {

struct GlbMaterialImportResult {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ImportedMaterialAsset> materials{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ImportedTextureAsset> textures{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ImportedImageAsset> images{&core::defaultCpuMemoryResource()};
};

/// Import only material metadata and encoded image payloads, using the same
/// parser as mesh import. No accessor expansion, node traversal or tangent
/// generation is performed. Success validates material assets, not geometry;
/// consumers requiring drawable geometry must load a mesh resource separately.
ARP_API GlbMaterialImportResult importGlbMaterialsFromFile(const std::filesystem::path& path);

/// Import a GLB file as a single merged mesh (legacy/compatibility path).
/// All named nodes are flattened into one vertex/index buffer.
ARP_API StaticMeshImportResult importStaticMeshFromGlbFile(const std::filesystem::path& path);

/// Import a GLB file preserving named-node variants.
/// Each mesh-bearing named node becomes a separate NamedVariant entry,
/// and COLOR_0 (wind weights) is read where present.
/// This is the path used by PlantAssetSet and RockAssetSet loaders.
enum class NamedVariantGeometrySpace { Scene, RootRelative };
ARP_API NamedVariantImportResult importNamedVariantsFromGlbFile(const std::filesystem::path& path,
    NamedVariantGeometrySpace geometrySpace = NamedVariantGeometrySpace::Scene);

} // namespace arp::assets
