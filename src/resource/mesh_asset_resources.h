#pragma once

#include "assets/asset_pipeline.h"
#include "core/cpu_allocator.h"
#include "core/stable_hash.h"
#include "pcg/terrain_heightfield.h"
#include "resource/resource_contract.h"
#include "resource/resource_manager.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace arp::resource {

// ---------------------------------------------------------------------------
// Contract descriptor for mesh asset resources.
// ---------------------------------------------------------------------------
inline constexpr ResourceContractDescriptor kMeshAssetContract{
    "MeshAssetResource",
    "mesh.asset/v1",
};

enum class MeshSourceKind {
    GlbFile,
    ProceduralGrid,
    ProceduralCube,
    CustomMesh,
};

/// MeshAssetResource — a ResourceManager-owned mesh asset.
///
/// Wraps assets::MeshAsset and supports loading from:
///   - GLB files (via assets::importStaticMeshFromGlbFile)
///   - Procedural generation (grid heightfield, cube)
///
/// All mesh data ownership is transferred to ResourceManager. Components
/// reference meshes by resourceId string, not raw pointer.
class MeshAssetResource final : public ResourceBase {
public:
    static constexpr const char* resourceTypeName() noexcept { return "MeshAssetResource"; }

    MeshAssetResource() = default;

    /// Construct for GLB file loading.
    MeshAssetResource(core::pmr::String resourceId, std::filesystem::path sourcePath)
        : ResourceBase(ResourceReference{
              core::pmr::String(std::move(resourceId), &core::defaultCpuMemoryResource()),
              core::pmr::String(resourceTypeName(), &core::defaultCpuMemoryResource()),
              {},
              core::pmr::String("Contract Complete", &core::defaultCpuMemoryResource()),
              {},
          }),
          sourceKind_(MeshSourceKind::GlbFile), sourcePath_(std::move(sourcePath))
    {}

    /// Construct for procedural grid mesh.
    MeshAssetResource(core::pmr::String resourceId,
        std::size_t gridX,
        std::size_t gridZ,
        float size,
        float baseHeight)
        : ResourceBase(ResourceReference{
              core::pmr::String(std::move(resourceId), &core::defaultCpuMemoryResource()),
              core::pmr::String(resourceTypeName(), &core::defaultCpuMemoryResource()),
              {},
              core::pmr::String("Contract Complete", &core::defaultCpuMemoryResource()),
              {},
          }),
          sourceKind_(MeshSourceKind::ProceduralGrid), gridX_(gridX), gridZ_(gridZ),
          gridSize_(size), gridBaseHeight_(baseHeight)
    {}

    /// Construct for a procedural grid mesh driven by an explicit heightfield.
    ///
    /// When `heightField` carries any wave or noise layers,
    /// generateProceduralGrid() samples it directly so the rendered mesh is
    /// bit-identical to the gameplay-sampled elevation (movement system reads
    /// the same TerrainHeightField instance). This is the single sanctioned
    /// path for noise/biome-driven terrain: the heightfield is data, the
    /// MeshAssetResource is the engine-owned runtime asset — no separate
    /// runtime resource manager is introduced.
    MeshAssetResource(core::pmr::String resourceId,
        std::size_t gridX,
        std::size_t gridZ,
        float size,
        float baseHeight,
        core::TerrainHeightField heightField)
        : ResourceBase(ResourceReference{
              core::pmr::String(std::move(resourceId), &core::defaultCpuMemoryResource()),
              core::pmr::String(resourceTypeName(), &core::defaultCpuMemoryResource()),
              {},
              core::pmr::String("Contract Complete", &core::defaultCpuMemoryResource()),
              {},
          }),
          sourceKind_(MeshSourceKind::ProceduralGrid), gridX_(gridX), gridZ_(gridZ),
          gridSize_(size), gridBaseHeight_(baseHeight),
          proceduralHeightField_(std::move(heightField))
    {}

    /// Construct for procedural cube mesh.
    MeshAssetResource(core::pmr::String resourceId, float cubeSize)
        : ResourceBase(ResourceReference{
              core::pmr::String(std::move(resourceId), &core::defaultCpuMemoryResource()),
              core::pmr::String(resourceTypeName(), &core::defaultCpuMemoryResource()),
              {},
              core::pmr::String("Contract Complete", &core::defaultCpuMemoryResource()),
              {},
          }),
          sourceKind_(MeshSourceKind::ProceduralCube), cubeSize_(cubeSize)
    {}

    /// Construct for a custom pre-built mesh (e.g. merged vegetation).
    /// The mesh is moved into the resource and owned by ResourceManager.
    /// The caller must ensure the mesh is validForIndexedDraw() before loading.
    MeshAssetResource(core::pmr::String resourceId, assets::MeshAsset customMesh)
        : ResourceBase(ResourceReference{
              core::pmr::String(std::move(resourceId), &core::defaultCpuMemoryResource()),
              core::pmr::String(resourceTypeName(), &core::defaultCpuMemoryResource()),
              {},
              core::pmr::String("Contract Complete", &core::defaultCpuMemoryResource()),
              {},
          }),
          sourceKind_(MeshSourceKind::CustomMesh),
          customMesh_(std::make_unique<assets::MeshAsset>(std::move(customMesh)))
    {}

    /// Load mesh data from source (GLB file or procedural generation).
    /// Called by ResourceManager::loadResource() before admitting metadata.
    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override;

    /// Access the loaded mesh data. Returns nullptr if not yet loaded.
    const assets::MeshAsset* mesh() const { return mesh_.get(); }

    /// Imported GLB material bindings and encoded image payloads. Empty for
    /// procedural/custom meshes that do not carry imported PBR metadata.
    const std::pmr::vector<assets::ImportedMaterialAsset>& materials() const { return materials_; }

    const std::pmr::vector<assets::ImportedTextureAsset>& textures() const { return textures_; }

    const std::pmr::vector<assets::ImportedImageAsset>& images() const { return images_; }

    const core::pmr::Vector<core::pmr::String>& textureResourceIds() const
    {
        return textureResourceIds_;
    }

    /// Mutable access for per-frame updates (e.g. CPU skinning).
    assets::MeshAsset* mutableMesh()
    {
        mutableMeshExposed_ = true;
        return mesh_.get();
    }

    // A retained mutable pointer can change without another accessor call.
    // Once exposed, geometry must remain outside immutable extraction caches.
    bool immutableMeshGeometry() const { return mesh_ && !mutableMeshExposed_; }

    // Only opt in when material draws are order-independent (opaque/masked).
    void setGroupSectionsByMaterial(bool enabled) { groupSectionsByMaterial_ = enabled; }

    MeshSourceKind sourceKind() const { return sourceKind_; }

    void setUvScale(float s) { uvScale_ = s; }

    float uvScale() const { return uvScale_; }

    const std::filesystem::path& sourcePath() const { return sourcePath_; }

    std::size_t processedByteCount() const noexcept override
    {
        if (!mesh_)
            return 0u;
        return mesh_->vertices.size() * sizeof(assets::MeshVertex)
            + mesh_->indices.size() * sizeof(std::uint32_t);
    }

    ResourceGraphStage processingStage() const noexcept override
    {
        return ResourceGraphStage::TypedProcessing;
    }

    void appendContentFingerprint(core::StableHash64& hasher) const noexcept override
    {
        hasher.appendU64(static_cast<std::uint64_t>(sourceKind_));
        const auto pathStr = sourcePath_.string();
        hasher.appendBytes(pathStr.data(), pathStr.size());
        hasher.appendU64(static_cast<std::uint64_t>(gridX_));
        hasher.appendU64(static_cast<std::uint64_t>(gridZ_));
        hasher.appendFloat(gridSize_);
        hasher.appendFloat(gridBaseHeight_);
        hasher.appendFloat(uvScale_);
        hasher.appendFloat(cubeSize_);
        // Heightfield fingerprint: base height, wave layers, and noise layers
        // (scale/amplitude/seed/octaves). This makes a noise-driven terrain
        // reload when the world seed changes.
        hasher.appendFloat(proceduralHeightField_.baseHeight);
        hasher.appendU64(static_cast<std::uint64_t>(proceduralHeightField_.layerCount));
        hasher.appendU64(static_cast<std::uint64_t>(proceduralHeightField_.noiseLayerCount));
        for (std::uint32_t i = 0u; i < proceduralHeightField_.noiseLayerCount; ++i) {
            const auto& nl = proceduralHeightField_.noiseLayers[i];
            hasher.appendFloat(nl.scale);
            hasher.appendFloat(nl.amplitude);
            hasher.appendU64(static_cast<std::uint64_t>(nl.seed));
            hasher.appendU64(static_cast<std::uint64_t>(nl.octaves));
        }
        // Custom mesh fingerprint: vertex/index counts (not full data — that
        // would be expensive and the counts are enough to distinguish assets).
        if (customMesh_) {
            hasher.appendU64(customMesh_->vertices.size());
            hasher.appendU64(customMesh_->indices.size());
        }
        hasher.appendU64(materials_.size());
        for (const auto& material : materials_) {
            hasher.appendString(material.name);
            hasher.appendString(material.sourceMaterialDocument);
            for (const auto value : material.baseColorFactor)
                hasher.appendFloat(value);
            for (const auto value : material.emissiveFactor)
                hasher.appendFloat(value);
            hasher.appendFloat(material.metallicFactor);
            hasher.appendFloat(material.roughnessFactor);
            hasher.appendU64(material.baseColorTexture);
            hasher.appendU64(material.normalTexture);
            hasher.appendU64(material.metallicRoughnessTexture);
            hasher.appendU64(material.occlusionTexture);
            hasher.appendU64(material.emissiveTexture);
            hasher.appendU64(static_cast<std::uint64_t>(material.alphaMode));
            hasher.appendFloat(material.alphaCutoff);
            hasher.appendByte(material.doubleSided ? 1u : 0u);
        }
        hasher.appendU64(textures_.size());
        for (std::size_t index = 0u; index < textures_.size(); ++index) {
            const auto& texture = textures_[index];
            hasher.appendString(texture.name);
            hasher.appendU64(texture.sourceImage);
            hasher.appendU64(texture.sampler);
            if (index < textureResourceIds_.size()) {
                hasher.appendString(textureResourceIds_[index]);
            }
        }
        hasher.appendU64(images_.size());
        for (const auto& image : images_) {
            hasher.appendString(image.name);
            hasher.appendString(image.uri);
            hasher.appendString(image.mimeType);
            hasher.appendU64(image.bufferView);
            hasher.appendByte(image.alphaIsTransparency ? 1u : 0u);
            hasher.appendFloat(image.alphaTransparencyReference);
            hasher.appendBytes(image.encodedBytes.data(), image.encodedBytes.size());
        }
        if (groupSectionsByMaterial_) {
            constexpr char policy[] = "MeshSectionGroupingByMaterial/1";
            hasher.appendBytes(policy, sizeof(policy) - 1u);
        }
    }

private:
    MeshSourceKind sourceKind_ = MeshSourceKind::ProceduralCube;
    bool mutableMeshExposed_ = false;
    bool groupSectionsByMaterial_ = false;
    std::filesystem::path sourcePath_;

    // Procedural grid parameters
    std::size_t gridX_ = 0;
    std::size_t gridZ_ = 0;
    float gridSize_ = 0.0f;
    float gridBaseHeight_ = 0.0f;

    /// Optional heightfield driving generateProceduralGrid(). When it carries
    /// any wave/noise layers, the grid samples it (rendered mesh == gameplay
    /// elevation). Empty by default (fallback legacy sine formula).
    core::TerrainHeightField proceduralHeightField_;

    // Procedural cube parameters
    float cubeSize_ = 1.0f;

    /// UV scaling factor baked into vertex UVs at generation time.
    /// For ProceduralGrid, generateProceduralGrid() multiplies the normalized
    /// (0..1) UV by this value. Sampler address mode must be Repeat for tiling.
    /// CustomMesh / ProceduralCube / GlbFile sources ignore this field.
    float uvScale_ = 1.0f;

    // Custom pre-built mesh (moved in at construction, swapped to mesh_ in load)
    std::unique_ptr<assets::MeshAsset> customMesh_;

    // Loaded mesh data (owned by this resource, managed through ResourceManager)
    std::unique_ptr<assets::MeshAsset> mesh_;
    std::pmr::vector<assets::ImportedMaterialAsset> materials_{&core::defaultCpuMemoryResource()};
    std::pmr::vector<assets::ImportedTextureAsset> textures_{&core::defaultCpuMemoryResource()};
    std::pmr::vector<assets::ImportedImageAsset> images_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> textureResourceIds_{&core::defaultCpuMemoryResource()};

    void loadFromGlb();
    void generateProceduralGrid();
    void generateProceduralCube();
};

} // namespace arp::resource
