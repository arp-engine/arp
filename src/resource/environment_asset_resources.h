#pragma once

// Environment Asset Resources (P1)
//
// Resource types for the prepared environment assets:
//   - TerrainStampAsset: DEM height-field stamp (height_f32.raw + metadata)
//   - TerrainSurfaceAsset: surface material (albedo/normal/roughness/height/ao)
//   - NamedMeshVariantSet: GLB with named-node variants (base for plant/rock)
//   - PlantAssetSet: tree model set with wind data (extends NamedMeshVariantSet)
//   - RockAssetSet: rock model set (extends NamedMeshVariantSet)
//
// Each type derives from ResourceBase and follows the same pattern as
// MeshAssetResource: construct with source path, load() reads from disk,
// ResourceManager owns the lifetime.
//
// Plan reference: complete_procedural_scene_implementation_plan.md P1.

#include "assets/asset_pipeline.h"
#include "core/cpu_allocator.h"
#include "core/stable_hash.h"
#include "resource/resource_contract.h"
#include "resource/resource_manager.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace arp::resource {

// Use the same PMR types as asset_pipeline.h for seamless interop with
// the GLB import result structs.
using core::pmr::String;
using core::pmr::Vector;

// ---------------------------------------------------------------------------
// TerrainStampAsset - DEM height-field stamp for landform composition
// ---------------------------------------------------------------------------
inline constexpr ResourceContractDescriptor kTerrainStampAssetContract{
    "TerrainStampAsset",
    "terrain.stamp/v1",
};

/// A terrain DEM stamp loaded from height_f32.raw.
/// Stores the float32 height grid and metadata (samples, elevation range,
/// horizontal size). Used by the landform layer stack (P2) to compose
/// real terrain features into the world height-field.
class TerrainStampAsset final : public ResourceBase {
public:
    static constexpr const char* resourceTypeName() noexcept { return "TerrainStampAsset"; }

    TerrainStampAsset(core::pmr::String resourceId,
        core::pmr::String alias,
        std::filesystem::path rawHeightPath,
        std::uint32_t widthSamples,
        std::uint32_t heightSamples,
        float horizontalSizeMeters,
        float minElevationMeters,
        float maxElevationMeters)
        : ResourceBase(ResourceReference{
              core::pmr::String(std::move(resourceId), &core::defaultCpuMemoryResource()),
              core::pmr::String(resourceTypeName(), &core::defaultCpuMemoryResource()),
              core::pmr::String(std::move(alias), &core::defaultCpuMemoryResource()),
              core::pmr::String("Contract Complete", &core::defaultCpuMemoryResource()),
              {},
          }),
          rawHeightPath_(std::move(rawHeightPath)), widthSamples_(widthSamples),
          heightSamples_(heightSamples), horizontalSizeMeters_(horizontalSizeMeters),
          minElevationMeters_(minElevationMeters), maxElevationMeters_(maxElevationMeters)
    {}

    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override;

    /// Sample the stamp at normalized UV [0,1]×[0,1]. Returns elevation
    /// in meters. Returns minElevation for out-of-bounds.
    float sampleNormalized(float u, float v) const;

    /// Sample at world-space offset within the stamp's horizontal extent.
    /// (x, z) in [0, horizontalSizeMeters]. Returns elevation in meters.
    float sampleWorld(float x, float z) const;

    std::uint32_t widthSamples() const { return widthSamples_; }

    std::uint32_t heightSamples() const { return heightSamples_; }

    float horizontalSizeMeters() const { return horizontalSizeMeters_; }

    float minElevationMeters() const { return minElevationMeters_; }

    float maxElevationMeters() const { return maxElevationMeters_; }

    const std::pmr::vector<float>& heights() const { return heights_; }

    bool hasData() const { return !heights_.empty(); }

    std::size_t processedByteCount() const noexcept override
    {
        return heights_.size() * sizeof(float);
    }

    ResourceGraphStage processingStage() const noexcept override
    {
        return ResourceGraphStage::TypedProcessing;
    }

    void appendContentFingerprint(core::StableHash64& hasher) const noexcept override
    {
        const auto pathStr = rawHeightPath_.string();
        hasher.appendBytes(pathStr.data(), pathStr.size());
        hasher.appendU64(widthSamples_);
        hasher.appendU64(heightSamples_);
        hasher.appendFloat(horizontalSizeMeters_);
        hasher.appendFloat(minElevationMeters_);
        hasher.appendFloat(maxElevationMeters_);
    }

private:
    std::filesystem::path rawHeightPath_;
    std::uint32_t widthSamples_ = 0;
    std::uint32_t heightSamples_ = 0;
    float horizontalSizeMeters_ = 0.0f;
    float minElevationMeters_ = 0.0f;
    float maxElevationMeters_ = 0.0f;
    std::pmr::vector<float> heights_{
        &core::defaultCpuMemoryResource()}; // row-major, [y * widthSamples + x]
};

// ---------------------------------------------------------------------------
// TerrainSurfaceAsset - surface material (albedo/normal/roughness/height/ao)
// ---------------------------------------------------------------------------
inline constexpr ResourceContractDescriptor kTerrainSurfaceAssetContract{
    "TerrainSurfaceAsset",
    "surface/v1",
};

/// A surface material asset. Stores file paths to the 5 canonical maps
/// (albedo, normal_gl, roughness, height, ao). The actual texture loading
/// happens in the renderer; this resource is the metadata + path registry.
class TerrainSurfaceAsset final : public ResourceBase {
public:
    static constexpr const char* resourceTypeName() noexcept { return "TerrainSurfaceAsset"; }

    struct MapPaths {
        std::filesystem::path albedo;
        std::filesystem::path normalGl;
        std::filesystem::path roughness;
        std::filesystem::path height;
        std::filesystem::path ao; // may be empty if not present
    };

    TerrainSurfaceAsset(core::pmr::String resourceId,
        core::pmr::String alias,
        std::string surfaceId,
        MapPaths mapPaths,
        float physicalSizeMeters,
        float heightScale)
        : ResourceBase(ResourceReference{
              core::pmr::String(std::move(resourceId), &core::defaultCpuMemoryResource()),
              core::pmr::String(resourceTypeName(), &core::defaultCpuMemoryResource()),
              core::pmr::String(std::move(alias), &core::defaultCpuMemoryResource()),
              core::pmr::String("Contract Complete", &core::defaultCpuMemoryResource()),
              {},
          }),
          surfaceId_(std::move(surfaceId)), mapPaths_(std::move(mapPaths)),
          physicalSizeMeters_(physicalSizeMeters), heightScale_(heightScale)
    {}

    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override;

    const std::string& surfaceId() const { return surfaceId_; }

    const MapPaths& mapPaths() const { return mapPaths_; }

    float physicalSizeMeters() const { return physicalSizeMeters_; }

    float heightScale() const { return heightScale_; }

    bool hasAo() const { return !mapPaths_.ao.empty(); }

    std::size_t processedByteCount() const noexcept override { return 0u; }

    ResourceGraphStage processingStage() const noexcept override
    {
        return ResourceGraphStage::TypedProcessing;
    }

    void appendContentFingerprint(core::StableHash64& hasher) const noexcept override
    {
        hasher.appendBytes(surfaceId_.data(), surfaceId_.size());
        auto appendPath = [&hasher](const std::filesystem::path& p) {
            const auto s = p.string();
            hasher.appendBytes(s.data(), s.size());
        };
        appendPath(mapPaths_.albedo);
        appendPath(mapPaths_.normalGl);
        appendPath(mapPaths_.roughness);
        appendPath(mapPaths_.height);
        appendPath(mapPaths_.ao);
        hasher.appendFloat(physicalSizeMeters_);
        hasher.appendFloat(heightScale_);
    }

private:
    std::string surfaceId_;
    MapPaths mapPaths_;
    float physicalSizeMeters_ = 0.0f;
    float heightScale_ = 1.0f;
};

// ---------------------------------------------------------------------------
// NamedMeshVariantSet - GLB with named-node variants
// ---------------------------------------------------------------------------
inline constexpr ResourceContractDescriptor kNamedMeshVariantSetContract{
    "NamedMeshVariantSet",
    "mesh.variant_set/v1",
};

/// A GLB file imported with named-node variant preservation.
/// Each named mesh-bearing node becomes an independently addressable variant.
/// PlantAssetSet and RockAssetSet extend this with type-specific validation.
class NamedMeshVariantSet : public ResourceBase {
public:
    static constexpr const char* resourceTypeName() noexcept { return "NamedMeshVariantSet"; }

    NamedMeshVariantSet(core::pmr::String resourceId,
        core::pmr::String alias,
        std::string assetSetId,
        std::filesystem::path glbPath,
        std::uint32_t expectedVariantCount,
        std::uint32_t expectedLodCount)
        : ResourceBase(ResourceReference{
              core::pmr::String(std::move(resourceId), &core::defaultCpuMemoryResource()),
              core::pmr::String(resourceTypeName(), &core::defaultCpuMemoryResource()),
              core::pmr::String(std::move(alias), &core::defaultCpuMemoryResource()),
              core::pmr::String("Contract Complete", &core::defaultCpuMemoryResource()),
              {},
          }),
          assetSetId_(std::move(assetSetId)), glbPath_(std::move(glbPath)),
          expectedVariantCount_(expectedVariantCount), expectedLodCount_(expectedLodCount)
    {}

    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override;

    /// Get a variant by name. Returns nullptr if not found.
    const assets::NamedVariant* findVariant(const std::string& name) const;

    /// Get all variant names.
    std::vector<std::string> variantNames() const;

    /// Get the variant count.
    std::size_t variantCount() const { return variants_.size(); }

    /// Get the imported materials.
    const std::pmr::vector<assets::ImportedMaterialAsset>& materials() const { return materials_; }

    const std::string& assetSetId() const { return assetSetId_; }

    const std::filesystem::path& glbPath() const { return glbPath_; }

    std::uint32_t expectedVariantCount() const { return expectedVariantCount_; }

    std::uint32_t expectedLodCount() const { return expectedLodCount_; }

    std::size_t processedByteCount() const noexcept override
    {
        std::size_t total = 0u;
        for (const auto& v : variants_) {
            total += v.mesh.vertices.size() * sizeof(assets::MeshVertex)
                + v.mesh.indices.size() * sizeof(std::uint32_t);
        }
        return total;
    }

    ResourceGraphStage processingStage() const noexcept override
    {
        return ResourceGraphStage::TypedProcessing;
    }

    void appendContentFingerprint(core::StableHash64& hasher) const noexcept override
    {
        hasher.appendBytes(assetSetId_.data(), assetSetId_.size());
        const auto pathStr = glbPath_.string();
        hasher.appendBytes(pathStr.data(), pathStr.size());
        hasher.appendU64(expectedVariantCount_);
        hasher.appendU64(expectedLodCount_);
    }

protected:
    std::string assetSetId_;
    std::filesystem::path glbPath_;
    std::uint32_t expectedVariantCount_ = 0;
    std::uint32_t expectedLodCount_ = 0;
    std::pmr::vector<assets::NamedVariant> variants_{&core::defaultCpuMemoryResource()};
    std::pmr::vector<assets::ImportedMaterialAsset> materials_{&core::defaultCpuMemoryResource()};
    std::pmr::vector<assets::ImportedTextureAsset> textures_{&core::defaultCpuMemoryResource()};
    std::pmr::vector<assets::ImportedImageAsset> images_{&core::defaultCpuMemoryResource()};
};

// ---------------------------------------------------------------------------
// PlantAssetSet - tree model set with wind data
// ---------------------------------------------------------------------------
inline constexpr ResourceContractDescriptor kPlantAssetSetContract{
    "PlantAssetSet",
    "plant.asset_set/v1",
};

/// A tree model set. Extends NamedMeshVariantSet with wind-weight
/// validation (COLOR_0) and material role checking (wood/foliage).
/// Each LOD level is a separate GLB file (lod0.glb, lod1.glb, lod2.glb).
class PlantAssetSet final : public ResourceBase {
public:
    static constexpr const char* resourceTypeName() noexcept { return "PlantAssetSet"; }

    PlantAssetSet(core::pmr::String resourceId,
        core::pmr::String alias,
        std::string assetSetId,
        std::filesystem::path runtimeDir,
        std::uint32_t expectedVariants,
        std::string woodMaterialRole,
        std::string foliageMaterialRole,
        std::uint32_t maxLodCount = 3u)
        : ResourceBase(ResourceReference{
              core::pmr::String(std::move(resourceId), &core::defaultCpuMemoryResource()),
              core::pmr::String(resourceTypeName(), &core::defaultCpuMemoryResource()),
              core::pmr::String(std::move(alias), &core::defaultCpuMemoryResource()),
              core::pmr::String("Contract Complete", &core::defaultCpuMemoryResource()),
              {},
          }),
          assetSetId_(std::move(assetSetId)), runtimeDir_(std::move(runtimeDir)),
          expectedVariants_(expectedVariants), woodMaterialRole_(std::move(woodMaterialRole)),
          foliageMaterialRole_(std::move(foliageMaterialRole)),
          initialLodCount_(maxLodCount == 0u ? 1u : maxLodCount)
    {}

    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override;

    /// Incrementally import one additional distance LOD after admission.
    ResourceLoadResult loadLod(std::uint32_t lodIndex);

    /// Get a variant mesh by (variantName, lodIndex).
    /// Returns nullptr if not found.
    const assets::NamedVariant* findVariant(
        const std::string& variantName, std::uint32_t lodIndex) const;

    /// Get all variant names (from LOD0).
    std::vector<std::string> variantNames() const;

    std::size_t variantCount() const;

    std::uint32_t lodCount() const { return static_cast<std::uint32_t>(lods_.size()); }

    const std::string& assetSetId() const { return assetSetId_; }

    const std::string& woodMaterialRole() const { return woodMaterialRole_; }

    const std::string& foliageMaterialRole() const { return foliageMaterialRole_; }

    const std::pmr::vector<assets::ImportedMaterialAsset>* materialsForLod(
        std::uint32_t lodIndex) const
    {
        const auto* lod = resolvedLod(lodIndex);
        return lod != nullptr ? &lod->materials : nullptr;
    }

    const std::pmr::vector<assets::ImportedTextureAsset>* texturesForLod(
        std::uint32_t lodIndex) const
    {
        const auto* lod = resolvedLod(lodIndex);
        return lod != nullptr ? &lod->textures : nullptr;
    }

    const std::pmr::vector<assets::ImportedImageAsset>* imagesForLod(std::uint32_t lodIndex) const
    {
        const auto* lod = resolvedLod(lodIndex);
        return lod != nullptr ? &lod->images : nullptr;
    }

    std::size_t processedByteCount() const noexcept override
    {
        std::size_t total = 0u;
        for (const auto& lod : lods_) {
            for (const auto& v : lod.variants) {
                total += v.mesh.vertices.size() * sizeof(assets::MeshVertex)
                    + v.mesh.indices.size() * sizeof(std::uint32_t);
            }
        }
        return total;
    }

    ResourceGraphStage processingStage() const noexcept override
    {
        return ResourceGraphStage::TypedProcessing;
    }

    void appendContentFingerprint(core::StableHash64& hasher) const noexcept override
    {
        hasher.appendBytes(assetSetId_.data(), assetSetId_.size());
        const auto dirStr = runtimeDir_.string();
        hasher.appendBytes(dirStr.data(), dirStr.size());
        hasher.appendU64(expectedVariants_);
        hasher.appendU64(maxLodCount_);
    }

private:
    struct LodData {
        std::uint32_t lodIndex = 0;
        // Equal GLB payloads share the imported CPU representation. This is
        // important for authored families whose LOD files are intentionally
        // mirrored until a lower-detail source is available.
        std::uint32_t sourceLod = 0;
        std::pmr::vector<assets::NamedVariant> variants{&core::defaultCpuMemoryResource()};
        std::pmr::vector<assets::ImportedMaterialAsset> materials{
            &core::defaultCpuMemoryResource()};
        std::pmr::vector<assets::ImportedTextureAsset> textures{&core::defaultCpuMemoryResource()};
        std::pmr::vector<assets::ImportedImageAsset> images{&core::defaultCpuMemoryResource()};
    };

    std::string assetSetId_;
    std::filesystem::path runtimeDir_;
    std::uint32_t expectedVariants_ = 0;
    std::string woodMaterialRole_;
    std::string foliageMaterialRole_;
    // The constructor argument controls the eager startup prefix. The
    // contract still permits the complete three-LOD set to be appended later.
    std::uint32_t initialLodCount_ = 3u;
    std::uint32_t maxLodCount_ = 3u;
    std::vector<LodData> lods_;

    ResourceLoadResult loadOneLod(std::uint32_t lodIndex);

    const LodData* resolvedLod(std::uint32_t lodIndex) const
    {
        if (lodIndex >= lods_.size())
            return nullptr;
        const LodData* lod = &lods_[lodIndex];
        std::size_t guard = 0;
        while (lod->sourceLod != lod->lodIndex && guard++ < lods_.size()) {
            if (lod->sourceLod >= lods_.size())
                return nullptr;
            lod = &lods_[lod->sourceLod];
        }
        return lod;
    }
};

// ---------------------------------------------------------------------------
// RockAssetSet - rock model set
// ---------------------------------------------------------------------------
inline constexpr ResourceContractDescriptor kRockAssetSetContract{
    "RockAssetSet",
    "rock.asset_set/v1",
};

/// A rock model set. Like PlantAssetSet but without wind data.
/// Each LOD level is a separate GLB file.
class RockAssetSet final : public ResourceBase {
public:
    static constexpr const char* resourceTypeName() noexcept { return "RockAssetSet"; }

    RockAssetSet(core::pmr::String resourceId,
        core::pmr::String alias,
        std::string assetSetId,
        std::filesystem::path runtimeDir,
        std::uint32_t expectedVariants,
        std::uint32_t maxLodCount = 3u)
        : ResourceBase(ResourceReference{
              core::pmr::String(std::move(resourceId), &core::defaultCpuMemoryResource()),
              core::pmr::String(resourceTypeName(), &core::defaultCpuMemoryResource()),
              core::pmr::String(std::move(alias), &core::defaultCpuMemoryResource()),
              core::pmr::String("Contract Complete", &core::defaultCpuMemoryResource()),
              {},
          }),
          assetSetId_(std::move(assetSetId)), runtimeDir_(std::move(runtimeDir)),
          expectedVariants_(expectedVariants),
          initialLodCount_(maxLodCount == 0u ? 1u : maxLodCount)
    {}

    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override;

    /// Incrementally import one additional distance LOD after admission.
    ResourceLoadResult loadLod(std::uint32_t lodIndex);

    const assets::NamedVariant* findVariant(
        const std::string& variantName, std::uint32_t lodIndex) const;

    std::vector<std::string> variantNames() const;
    std::size_t variantCount() const;

    std::uint32_t lodCount() const { return static_cast<std::uint32_t>(lods_.size()); }

    const std::string& assetSetId() const { return assetSetId_; }

    std::size_t processedByteCount() const noexcept override
    {
        std::size_t total = 0u;
        for (const auto& lod : lods_) {
            for (const auto& v : lod.variants) {
                total += v.mesh.vertices.size() * sizeof(assets::MeshVertex)
                    + v.mesh.indices.size() * sizeof(std::uint32_t);
            }
        }
        return total;
    }

    ResourceGraphStage processingStage() const noexcept override
    {
        return ResourceGraphStage::TypedProcessing;
    }

    void appendContentFingerprint(core::StableHash64& hasher) const noexcept override
    {
        hasher.appendBytes(assetSetId_.data(), assetSetId_.size());
        const auto dirStr = runtimeDir_.string();
        hasher.appendBytes(dirStr.data(), dirStr.size());
        hasher.appendU64(expectedVariants_);
        hasher.appendU64(maxLodCount_);
    }

private:
    struct LodData {
        std::uint32_t lodIndex = 0;
        std::pmr::vector<assets::NamedVariant> variants{&core::defaultCpuMemoryResource()};
        std::pmr::vector<assets::ImportedMaterialAsset> materials{
            &core::defaultCpuMemoryResource()};
    };

    std::string assetSetId_;
    std::filesystem::path runtimeDir_;
    std::uint32_t expectedVariants_ = 0;
    std::uint32_t initialLodCount_ = 3u;
    std::uint32_t maxLodCount_ = 3u;
    std::vector<LodData> lods_;

    ResourceLoadResult loadOneLod(std::uint32_t lodIndex);
};

} // namespace arp::resource
