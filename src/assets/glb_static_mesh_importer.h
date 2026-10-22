#pragma once

#include "assets/asset_pipeline.h"
#include "core/api.h"

#include <array>
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

// StaticMeshSourceIdentity/1. Sidecar for the exact pre-grouping primitive
// interval. The full importer-private transform is converted once to canonical
// row-major storage here. A repeated (node,primitive) DAG occurrence is rejected.
struct StaticMeshSourceIdentity {
    std::uint32_t node = 0, mesh = 0, primitive = 0;
    Submesh sourceIndices{};
    float worldAffine[16]{};
    double localAbsolutePosition[3]{};
    std::array<std::uint8_t, 32> localSurfaceDigest{};
};

struct StaticMeshSourceImportResult {
    StaticMeshImportResult geometry;
    std::uint32_t identityVersion = 0;
    std::array<std::uint8_t, 32> sourceContentDigest{};
    core::pmr::Vector<StaticMeshSourceIdentity> identities{&core::defaultCpuMemoryResource()};
};

struct StaticMeshSourceImportLimits {
    std::uint64_t maxSourceBytes = UINT64_MAX;
    std::uint32_t maxVertices = UINT32_MAX, maxIndices = UINT32_MAX, maxPrimitives = UINT32_MAX;
    // Explicit static-glb-geometry/v1 subset. Legacy callers retain their API.
    bool strictGeometryOnly = false;
    std::uint32_t maxJsonDepth = 0, maxJsonValues = 0;
};

ARP_API StaticMeshSourceImportResult importStaticMeshWithSourceIdentityFromGlbFile(
    const std::filesystem::path& path, StaticMeshSourceImportLimits limits = {});

/// Import a GLB file preserving named-node variants.
/// Each mesh-bearing named node becomes a separate NamedVariant entry,
/// and COLOR_0 (wind weights) is read where present.
/// This is the path used by PlantAssetSet and RockAssetSet loaders.
enum class NamedVariantGeometrySpace { Scene, RootRelative };
ARP_API NamedVariantImportResult importNamedVariantsFromGlbFile(const std::filesystem::path& path,
    NamedVariantGeometrySpace geometrySpace = NamedVariantGeometrySpace::Scene);

} // namespace arp::assets
