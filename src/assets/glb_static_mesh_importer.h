#pragma once

#include "assets/asset_pipeline.h"
#include "core/api.h"
#include "core/container/array_view.h"

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

// Raw binary acquisition is owned by the caller. Requires a finite positive
// source/geometry/JSON budgets, performs no IO and shares the file adapter's
// identity parser. Unlimited legacy file defaults do not select this entry.
ARP_API StaticMeshSourceImportResult importStaticMeshWithSourceIdentityFromGlbBytes(
    core::ArrayView<const std::uint8_t> bytes, StaticMeshSourceImportLimits limits);

// Full static mesh/material import without source-identity retention. Uses the
// same bounded binary parser, preserving ordinary repeated-node semantics.
ARP_API StaticMeshImportResult importStaticMeshFromGlbBytes(
    core::ArrayView<const std::uint8_t> bytes, StaticMeshSourceImportLimits limits);

enum class StaticMeshVertexColorPolicy { Unspecified, RejectColor0, DiscardValidatedRgba };

// static-glb-scene-source/1: all selected scene nodes, including unnamed ones.
// Geometry limits must be finite and strictGeometryOnly=false; that other
// profile intentionally admits only identity-node, metadata-free geometry.
struct StaticMeshSceneSourceImportLimits final {
    StaticMeshSourceImportLimits geometry;
    std::uint64_t maxImportBytes = 0, maxDecodedBytes = 0;
    std::uint32_t maxNodeVisits = 0, maxNodeDepth = 0;
    StaticMeshVertexColorPolicy colorPolicy = StaticMeshVertexColorPolicy::Unspecified;
};

struct StaticMeshSceneSourceImportResult final {
    StaticMeshImportResult geometry;
    std::uint64_t decodedBytes = 0, importPeakBytes = 0;
};

ARP_API const char* validateStaticMeshSceneSourceImportLimits(
    const StaticMeshSceneSourceImportLimits& limits) noexcept;
ARP_API StaticMeshSceneSourceImportResult importStaticMeshSceneFromGlbBytes(
    core::ArrayView<const std::uint8_t> bytes, const StaticMeshSceneSourceImportLimits& limits);

/// Import a GLB file preserving named-node variants.
/// Each mesh-bearing named node becomes a separate NamedVariant entry,
/// and COLOR_0 (wind weights) is read where present.
/// This is the path used by PlantAssetSet and RockAssetSet loaders.
enum class NamedVariantGeometrySpace { Scene, RootRelative };

enum class NamedVariantColorPolicy { Unspecified, RejectColor0, PreserveRgbaWind };

// named-glb-source/1 budgets apply to emitted occurrences, including shared DAGs.
struct NamedVariantSourceImportLimits final {
    std::uint64_t maxSourceBytes = 0, maxImportBytes = 0;
    std::uint32_t maxVertices = 0, maxIndices = 0, maxPrimitives = 0, maxVariants = 0;
    std::uint32_t maxNodeVisits = 0, maxNodeDepth = 0, maxJsonDepth = 0, maxJsonValues = 0;
    NamedVariantColorPolicy colorPolicy = NamedVariantColorPolicy::Unspecified;
};

ARP_API const char* validateNamedVariantSourceImportLimits(
    NamedVariantSourceImportLimits limits) noexcept;

// Pure, bounded source profile. Converts node matrices to core row-major output;
// the old file adapter below retains its explicit Legacy transform convention.
ARP_API NamedVariantImportResult importNamedVariantsFromGlbBytes(
    core::ArrayView<const std::uint8_t> bytes,
    NamedVariantGeometrySpace geometrySpace,
    NamedVariantSourceImportLimits limits);

// Legacy cold-host import; it does not select the bounded source contract.
ARP_API NamedVariantImportResult importNamedVariantsFromGlbFile(const std::filesystem::path& path,
    NamedVariantGeometrySpace geometrySpace = NamedVariantGeometrySpace::Scene);

} // namespace arp::assets
