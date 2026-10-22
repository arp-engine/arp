#pragma once

#include "3d/vegetation/instance_layout.h"
#include "core/container/array_view.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/runtime/gpu_resource_retire_queue.h"
#include "resource/terrain_asset_resource.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace arp::resource {
class VegetationCatalogResource;
}

namespace arp::renderer {

/// One prototype's immutable instance records for a resident terrain tile.
/// Prototype resources, material binding, LOD selection and GPU buffers remain
/// backend-owned; this type only preserves the asset's declared layout.
struct TerrainVegetationPrototypePage final {
    std::uint32_t prototypeId = 0u;
    core::pmr::Vector<resource::TerrainStaticVegetationInstance> instances{
        &core::defaultCpuMemoryResource()};
};

// Preserve record order within each prototype; return prototypes sorted by ID.
core::pmr::Vector<TerrainVegetationPrototypePage> groupTerrainVegetationInstances(
    core::ArrayView<vegetation::StaticVegetationInstance> instances);

struct TerrainVegetationPage final {
    terrain::TerrainTileCoord tile;
    std::uint64_t terrainGeneration = 0u;
    core::pmr::String terrainResourceId{&core::defaultCpuMemoryResource()};
    core::pmr::String vegetationCatalogResourceId{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<TerrainVegetationPrototypePage> prototypes{&core::defaultCpuMemoryResource()};
};

/// Shader/instance-buffer representation of terrain.asset/v1 vegetation.
/// Its field order is identical to the fixed 48-byte binary record, making
/// the CPU asset contract and GPU instance upload independently auditable.
struct TerrainVegetationGpuInstance final {
    std::uint32_t prototypeId = 0u;
    float position[3]{};
    float rotation[4]{};
    float scale[3]{};
    std::uint32_t variationSeed = 0u;
};

static_assert(sizeof(TerrainVegetationGpuInstance) == 48u,
    "terrain.asset/v1 vegetation GPU record must remain 48 bytes");

bool buildTerrainVegetationGpuInstances(const TerrainVegetationPrototypePage& page,
    core::pmr::Vector<TerrainVegetationGpuInstance>& outInstances,
    core::pmr::String& diagnostics);

/// Lower the fixed terrain.asset/v1 record into the established 64-byte
/// vegetation cull and instance-draw ABI. The lowerer fails when the legacy
/// ABI cannot preserve the authored transform.
bool lowerTerrainVegetationInstancesForCull(const TerrainVegetationPrototypePage& page,
    core::pmr::Vector<vegetation::InstanceDataGpu>& outInstances,
    core::pmr::String& diagnostics);

// Shared decoder for the established 48-byte little-endian placement layout.
bool decodeTerrainVegetationRecords(const std::uint8_t* bytes,
    std::size_t byteCount,
    core::pmr::Vector<resource::TerrainStaticVegetationInstance>& out,
    core::pmr::String& diagnostics);

/// Backend-neutral static vegetation page lifecycle. A backend receives pages
/// only through the explicit upload callback; no empty page or default model
/// is ever reported as uploaded. Replacement and removal defer the old page
/// by submission serial, matching terrain mesh retirement semantics.
class TerrainVegetationRuntime final {
public:
    using UploadPageFn = bool (*)(
        void* context, const TerrainVegetationPage& page, core::pmr::String& diagnostics);

    struct Result final {
        bool success = false;
        bool unchanged = false;
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    };

    Result activate(const resource::TerrainAssetResource& terrain,
        const resource::VegetationCatalogResource& catalog,
        terrain::TerrainTileCoord tile,
        std::uint64_t submissionSerial,
        UploadPageFn upload,
        void* context);

    bool deactivate(terrain::TerrainTileCoord tile, std::uint64_t submissionSerial);

    bool setRetirementCapacityBudget(std::shared_ptr<core::AtomicCapacityBudget> budget) noexcept
    {
        return retiredPages_.setCapacityBudget(std::move(budget));
    }

    const TerrainVegetationPage* page(terrain::TerrainTileCoord tile) const noexcept;

    std::size_t activePageCount() const noexcept { return activePages.size(); }

    std::size_t retiredPageCount() const noexcept { return retiredPages_.size(); }

    core::runtime::GpuResourceRetireQueueStats retirementStats() const noexcept
    {
        return retiredPages_.stats();
    }

    std::size_t reapCompleted(std::uint64_t completedSubmissionSerial)
    {
        return retiredPages_.reapCompleted(completedSubmissionSerial);
    }

    std::size_t reapAll() { return retiredPages_.reapAll(); }

    void clear()
    {
        activePages.clear();
        (void)retiredPages_.reapAll();
    }

private:
    static TerrainVegetationPage buildPage(
        const resource::TerrainAssetResource& terrain, terrain::TerrainTileCoord tile);
    core::pmr::Vector<TerrainVegetationPage> activePages{&core::defaultCpuMemoryResource()};
    core::runtime::GpuResourceRetireQueue<TerrainVegetationPage> retiredPages_;
};

} // namespace arp::renderer
