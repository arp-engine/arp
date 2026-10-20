#pragma once

#include "core/cpu_allocator.h"
#include "core/stable_hash.h"
#include "resource/resource_contract.h"
#include "resource/resource_manager.h"
#include "terrain/terrain_surface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>

namespace arp::resource {

inline constexpr ResourceContractDescriptor kTerrainAssetResourceContract{
    "TerrainAssetResource",
    "terrain.asset/v1",
};

enum class TerrainAssetBinaryFormat : std::uint8_t {
    R32SFloat,
    Rgba8Unorm,
    StaticVegetationV1,
};

struct TerrainAssetLayerDesc final {
    core::pmr::String layerId{&core::defaultCpuMemoryResource()};
    core::pmr::String semantic{&core::defaultCpuMemoryResource()};
    core::pmr::String materialResourceId{&core::defaultCpuMemoryResource()};
    std::uint32_t page = 0u;
    std::uint32_t channel = 0u;
};

struct TerrainAssetBlockDesc final {
    core::pmr::String path{&core::defaultCpuMemoryResource()};
    std::uint64_t byteCount = 0u;
    std::uint64_t contentHash = 0u;
    TerrainAssetBinaryFormat format = TerrainAssetBinaryFormat::R32SFloat;
};

struct TerrainAssetTileDesc final {
    terrain::TerrainTileCoord coord;
    TerrainAssetBlockDesc height;
    TerrainAssetBlockDesc weightPageA;
    TerrainAssetBlockDesc weightPageB;
    TerrainAssetBlockDesc vegetation;
};

struct TerrainStaticVegetationInstance final {
    std::uint32_t prototypeId = 0u;
    core::Vec3 position{};
    std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
    core::Vec3 scale{1.0f, 1.0f, 1.0f};
    std::uint32_t variationSeed = 0u;
};

struct TerrainAssetBuildTile final {
    terrain::TerrainTileCoord coord;
    core::pmr::Vector<float> heights{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint8_t> weightPageA{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint8_t> weightPageB{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TerrainStaticVegetationInstance> vegetation{
        &core::defaultCpuMemoryResource()};
};

struct TerrainAssetBuildDesc final {
    core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
    core::pmr::String alias{&core::defaultCpuMemoryResource()};
    core::pmr::String status{"Contract Complete", &core::defaultCpuMemoryResource()};
    terrain::TerrainBounds bounds;
    std::uint32_t tileCountX = 0u;
    std::uint32_t tileCountZ = 0u;
    std::uint32_t samplesPerEdge = 0u;
    float tileWorldSize = 0.0f;
    core::pmr::String vegetationCatalogResourceId{&core::defaultCpuMemoryResource()};
    std::array<TerrainAssetLayerDesc, terrain::kTerrainBaseLayerCount + 2u> layers{};
    core::pmr::Vector<TerrainAssetBuildTile> tiles{&core::defaultCpuMemoryResource()};
};

struct TerrainAssetIoResult final {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

struct TerrainAssetHeightRange final {
    float minimum = 0.0f;
    float maximum = 0.0f;

    bool valid() const noexcept
    {
        return std::isfinite(minimum) && std::isfinite(maximum) && minimum <= maximum;
    }
};

/// Read-only view of one fully validated TerrainAsset tile. The memory is
/// owned by the resource generation; consumers must not retain this view over
/// a reload. Runtime users that need a stable query use snapshotSurface(),
/// while GPU upload paths consume this view synchronously.
struct TerrainAssetTileReadView final {
    terrain::TerrainTileCoord coord;
    std::uint32_t samplesPerEdge = 0u;
    float tileWorldSize = 0.0f;
    std::uint64_t generation = 0u;
    const float* heights = nullptr;
    std::size_t heightCount = 0u;
    const std::uint8_t* weightPageA = nullptr;
    std::size_t weightPageAByteCount = 0u;
    const std::uint8_t* weightPageB = nullptr;
    std::size_t weightPageBByteCount = 0u;

    bool valid() const noexcept
    {
        const auto expected = static_cast<std::size_t>(samplesPerEdge) * samplesPerEdge;
        return samplesPerEdge >= 2u && heights != nullptr && heightCount == expected
            && weightPageA != nullptr && weightPageAByteCount == expected * 4u
            && weightPageB != nullptr && weightPageBByteCount == expected * 4u;
    }
};

/// Writes a terrain asset manifest plus immutable binary blocks. The manifest is
/// YAML; every binary block has an explicit byte count and content hash.
class ARP_API TerrainAssetWriter final {
public:
    TerrainAssetIoResult write(
        const TerrainAssetBuildDesc& desc, const std::filesystem::path& outputDirectory) const;
};

class ARP_API TerrainAssetResource final : public ResourceBase, public terrain::TerrainSurface {
public:
    static constexpr const char* resourceTypeName() noexcept { return "TerrainAssetResource"; }

    TerrainAssetResource() = default;
    TerrainAssetResource(ResourceReference reference, std::filesystem::path manifestPath);
    TerrainAssetResource(const TerrainAssetResource& other);

    ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context) override;
    std::size_t processedByteCount() const noexcept override;
    void appendContentFingerprint(core::StableHash64& hasher) const noexcept override;

    terrain::TerrainBounds bounds() const noexcept override { return bounds_; }

    std::uint64_t generation() const noexcept override { return generation_; }

    terrain::TerrainSurfaceQuery sample(float worldX, float worldZ) const override;

    /// Retains an immutable query generation for consumers while a resource
    /// reload publishes the next TerrainAsset generation.
    std::shared_ptr<const terrain::TerrainSurface> snapshotSurface() const;

    TerrainAssetHeightRange heightRange() const noexcept { return heightRange_; }

    const core::pmr::Vector<TerrainStaticVegetationInstance>* vegetationFor(
        terrain::TerrainTileCoord coord) const;
    bool tileReadView(
        terrain::TerrainTileCoord coord, TerrainAssetTileReadView& outView) const noexcept;

    std::uint32_t tileCountX() const noexcept { return tileCountX_; }

    std::uint32_t tileCountZ() const noexcept { return tileCountZ_; }

    std::uint32_t samplesPerEdge() const noexcept { return samplesPerEdge_; }

    /// PhysicsWorld currently accepts a square heightfield. Use the longer
    /// TerrainAsset lattice axis so neither axis is undersampled; zero marks
    /// invalid or overflowing metadata and callers must reject the build.
    std::uint32_t physicsGridResolution() const noexcept
    {
        if (tileCountX_ == 0u || tileCountZ_ == 0u || samplesPerEdge_ < 2u) {
            return 0u;
        }
        const auto tileCount = std::max(tileCountX_, tileCountZ_);
        const auto intervalsPerTile = samplesPerEdge_ - 1u;
        if (tileCount > (std::numeric_limits<std::uint32_t>::max() - 1u) / intervalsPerTile) {
            return 0u;
        }
        return tileCount * intervalsPerTile + 1u;
    }

    float tileWorldSize() const noexcept { return tileWorldSize_; }

    const core::pmr::Vector<TerrainAssetLayerDesc>& layers() const noexcept { return layers_; }

    const core::pmr::String& vegetationCatalogResourceId() const noexcept
    {
        return vegetationCatalogResourceId_;
    }

private:
    struct LoadedTile final {
        terrain::TerrainTileCoord coord;
        core::pmr::Vector<float> heights{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<std::uint8_t> weightPageA{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<std::uint8_t> weightPageB{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<TerrainStaticVegetationInstance> vegetation{
            &core::defaultCpuMemoryResource()};
    };

    std::filesystem::path manifestPath_;
    terrain::TerrainBounds bounds_;
    std::uint32_t tileCountX_ = 0u;
    std::uint32_t tileCountZ_ = 0u;
    std::uint32_t samplesPerEdge_ = 0u;
    float tileWorldSize_ = 0.0f;
    std::uint64_t generation_ = 0u;
    TerrainAssetHeightRange heightRange_{};
    core::pmr::String vegetationCatalogResourceId_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TerrainAssetLayerDesc> layers_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<LoadedTile> tiles_{&core::defaultCpuMemoryResource()};
    // One immutable copy belongs to each loaded generation. Reusing it keeps
    // high-frequency TerrainSurface queries bounded while preserving the
    // snapshot lifetime across a subsequent resource reload.
    mutable std::mutex snapshotMutex_;
    mutable std::shared_ptr<const terrain::TerrainSurface> surfaceSnapshot_;
};

TerrainAssetIoResult writeTerrainAsset(
    const TerrainAssetBuildDesc& desc, const std::filesystem::path& outputDirectory);

ResourceLoadResult loadTerrainAssetResource(ResourceManager& manager,
    ResourceReference reference,
    const std::filesystem::path& manifestPath,
    ResourceLoadPolicy policy = {});

} // namespace arp::resource
