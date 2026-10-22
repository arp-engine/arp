#pragma once

#include "3d/terrain/terrain_mesh_samples.h"
#include "assets/asset_pipeline.h"
#include "assets/glb_static_mesh_importer.h"
#include "assets/mesh_section_grouping.h"
#include "core/hash/stable_hash.h"
#include "core/memory/cpu_allocator.h"
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

    /// Explicit Legacy procedural-grid API. Selecting this overload opts into
    /// the historical sine demo surface; production terrain uses baked assets.
    MeshAssetResource(core::pmr::String resourceId,
        std::size_t gridX,
        std::size_t gridZ,
        float size,
        float baseHeight)
        : ResourceBase(ResourceReference{
              core::pmr::String(std::move(resourceId), &core::defaultCpuMemoryResource()),
              core::pmr::String(resourceTypeName(), &core::defaultCpuMemoryResource()),
              {},
              core::pmr::String("Prototype Complete", &core::defaultCpuMemoryResource()),
              {},
          }),
          sourceKind_(MeshSourceKind::ProceduralGrid), gridX_(gridX), gridZ_(gridZ),
          gridSize_(size), gridBaseHeight_(baseHeight)
    {}

    /// Explicit source compatibility boundary: materialize height and normal
    /// samples now. Runtime resource loading owns data, never a PCG generator.
    template <class HeightSource,
        class = decltype(std::declval<const HeightSource&>().sample(0.0f, 0.0f))>
    MeshAssetResource(core::pmr::String resourceId,
        std::size_t gridX,
        std::size_t gridZ,
        float size,
        float baseHeight,
        const HeightSource& heightField)
        : ResourceBase(ResourceReference{
              core::pmr::String(std::move(resourceId), &core::defaultCpuMemoryResource()),
              core::pmr::String(resourceTypeName(), &core::defaultCpuMemoryResource()),
              {},
              core::pmr::String("Contract Complete", &core::defaultCpuMemoryResource()),
              {},
          }),
          sourceKind_(MeshSourceKind::ProceduralGrid), gridX_(gridX), gridZ_(gridZ),
          gridSize_(size), gridBaseHeight_(baseHeight), explicitGridSamples_(true),
          gridSamples_(terrain::TerrainMeshSamples::build(heightField,
              gridX,
              gridZ,
              size,
              terrain::TerrainMeshSamples::legacyNormalSampleSpacing))
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
          customMesh_(core::makeUnique<assets::MeshAsset>(std::move(customMesh)))
    {}

    /// Load mesh data from source (GLB file or procedural generation).
    /// Called by ResourceManager::loadResource() before admitting metadata.
    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override;

    /// Access the loaded mesh data. Returns nullptr if not yet loaded.
    const assets::MeshAsset* mesh() const { return mesh_.get(); }

    /// Imported GLB material bindings and encoded image payloads. Empty for
    /// procedural/custom meshes that do not carry imported PBR metadata.
    const core::pmr::Vector<assets::ImportedMaterialAsset>& materials() const { return materials_; }

    const core::pmr::Vector<assets::ImportedTextureAsset>& textures() const { return textures_; }

    const core::pmr::Vector<assets::ImportedImageAsset>& images() const { return images_; }

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

    void setRetainSourceRanges(bool enabled) { retainSourceRanges_ = enabled; }

    void setRetainSourceIdentity(bool enabled) { retainSourceIdentity_ = enabled; }

    const core::pmr::Vector<assets::StaticMeshSourceIdentity>& sourceIdentities() const
    {
        return sourceIdentities_;
    }

    std::uint32_t sourceIdentityVersion() const { return sourceIdentityVersion_; }

    const std::array<std::uint8_t, 32>& sourceContentDigest() const { return sourceContentDigest_; }

    void setSectionSpatialPartition(assets::MeshSectionSpatialPartition policy)
    {
        sectionSpatialPartition_ = policy;
    }

    MeshSourceKind sourceKind() const { return sourceKind_; }

    void setUvScale(float s) { uvScale_ = s; }

    float uvScale() const { return uvScale_; }

    const std::filesystem::path& sourcePath() const { return sourcePath_; }

    std::size_t processedByteCount() const noexcept override
    {
        if (!mesh_)
            return 0u;
        return mesh_->vertices.size() * sizeof(assets::MeshVertex)
            + mesh_->indices.size() * sizeof(std::uint32_t)
            + mesh_->sourceRanges.size() * sizeof(assets::MeshSourceRange)
            + sourceIdentities_.size() * sizeof(assets::StaticMeshSourceIdentity);
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
        hasher.appendByte(explicitGridSamples_ ? 1u : 0u);
        for (const auto& sample : gridSamples_.samples()) {
            hasher.appendFloat(sample.height);
            hasher.appendFloat(sample.offsetHeightX);
            hasher.appendFloat(sample.offsetHeightZ);
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
        if (retainSourceRanges_) {
            constexpr char policy[] = "MeshSourceRanges/1";
            hasher.appendBytes(policy, sizeof(policy) - 1u);
        }
        if (retainSourceIdentity_) {
            constexpr char policy[] = "StaticMeshSourceIdentity/1";
            hasher.appendBytes(policy, sizeof(policy) - 1u);
            hasher.appendU64(sourceIdentityVersion_);
            hasher.appendBytes(sourceContentDigest_.data(), sourceContentDigest_.size());
            for (const auto& identity : sourceIdentities_) {
                hasher.appendU64(identity.node);
                hasher.appendU64(identity.mesh);
                hasher.appendU64(identity.primitive);
                hasher.appendU64(identity.sourceIndices.firstIndex);
                hasher.appendU64(identity.sourceIndices.indexCount);
                hasher.appendU64(identity.sourceIndices.materialSlot);
                for (const auto value : identity.worldAffine)
                    hasher.appendFloat(value);
                for (const auto value : identity.localAbsolutePosition)
                    hasher.appendBytes(&value, sizeof(value));
                hasher.appendBytes(
                    identity.localSurfaceDigest.data(), identity.localSurfaceDigest.size());
            }
        }
        if (sectionSpatialPartition_.enabled()) {
            constexpr char policy[] = "MeshSectionSpatialPartition/1";
            hasher.appendBytes(policy, sizeof(policy) - 1u);
            hasher.appendU64(sectionSpatialPartition_.maxTrianglesPerSection);
            hasher.appendU64(sectionSpatialPartition_.maxSections);
        }
    }

private:
    MeshSourceKind sourceKind_ = MeshSourceKind::ProceduralCube;
    bool mutableMeshExposed_ = false;
    bool groupSectionsByMaterial_ = false;
    bool retainSourceRanges_ = false;
    bool retainSourceIdentity_ = false;
    std::uint32_t sourceIdentityVersion_ = 0;
    std::array<std::uint8_t, 32> sourceContentDigest_{};
    core::pmr::Vector<assets::StaticMeshSourceIdentity> sourceIdentities_{
        &core::defaultCpuMemoryResource()};
    assets::MeshSectionSpatialPartition sectionSpatialPartition_;
    std::filesystem::path sourcePath_;

    // Procedural grid parameters
    std::size_t gridX_ = 0;
    std::size_t gridZ_ = 0;
    float gridSize_ = 0.0f;
    float gridBaseHeight_ = 0.0f;

    bool explicitGridSamples_ = false;
    terrain::TerrainMeshSamples gridSamples_;

    // Procedural cube parameters
    float cubeSize_ = 1.0f;

    /// UV scaling factor baked into vertex UVs at generation time.
    /// For ProceduralGrid, generateProceduralGrid() multiplies the normalized
    /// (0..1) UV by this value. Sampler address mode must be Repeat for tiling.
    /// CustomMesh / ProceduralCube / GlbFile sources ignore this field.
    float uvScale_ = 1.0f;

    // Custom pre-built mesh (moved in at construction, swapped to mesh_ in load)
    core::UniquePtr<assets::MeshAsset> customMesh_;

    // Loaded mesh data (owned by this resource, managed through ResourceManager)
    core::UniquePtr<assets::MeshAsset> mesh_;
    core::pmr::Vector<assets::ImportedMaterialAsset> materials_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<assets::ImportedTextureAsset> textures_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<assets::ImportedImageAsset> images_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> textureResourceIds_{&core::defaultCpuMemoryResource()};

    void loadFromGlb();
    void generateProceduralGrid();
    void generateProceduralCube();
};

} // namespace arp::resource
