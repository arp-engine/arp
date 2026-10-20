#pragma once

#include "core/cpu_allocator.h"
#include "core/runtime/gpu_resource_retire_queue.h"
#include "renderer/terrain_vegetation_runtime.h"
#include "rhi/gpu_device.h"

#include <memory>

namespace arp::renderer {

struct TerrainVegetationGpuPrototypePage final {
    std::uint32_t prototypeId = 0u;
    std::uint32_t instanceCount = 0u;
    core::UniquePtr<rhi::GpuBuffer> instanceBuffer;
};

struct TerrainVegetationGpuPage final {
    terrain::TerrainTileCoord tile;
    std::uint64_t terrainGeneration = 0u;
    core::pmr::Vector<TerrainVegetationGpuPrototypePage> prototypes{
        &core::defaultCpuMemoryResource()};
};

/// Renderer-owned static instance pages. The class never resolves catalog
/// models or creates collision objects; it only materializes validated page
/// records into GPU buffers and defers replaced buffers by submission serial.
class TerrainVegetationRhiRuntime final {
public:
    struct Result final {
        bool success = false;
        bool unchanged = false;
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    };

    Result activate(
        rhi::GpuDevice& device, const TerrainVegetationPage& page, std::uint64_t submissionSerial);
    bool deactivate(terrain::TerrainTileCoord tile, std::uint64_t submissionSerial);

    bool setRetirementCapacityBudget(std::shared_ptr<core::AtomicCapacityBudget> budget) noexcept
    {
        return retired_.setCapacityBudget(std::move(budget));
    }

    const TerrainVegetationGpuPage* page(terrain::TerrainTileCoord tile) const noexcept;

    std::size_t reapCompleted(std::uint64_t completedSubmissionSerial)
    {
        return retired_.reapCompleted(completedSubmissionSerial);
    }

    std::size_t retiredPageCount() const noexcept { return retired_.size(); }

    core::runtime::GpuResourceRetireQueueStats retirementStats() const noexcept
    {
        return retired_.stats();
    }

    void clear()
    {
        active_.clear();
        (void)retired_.reapAll();
    }

private:
    core::pmr::Vector<TerrainVegetationGpuPage> active_{&core::defaultCpuMemoryResource()};
    core::runtime::GpuResourceRetireQueue<TerrainVegetationGpuPage> retired_;
};

} // namespace arp::renderer
