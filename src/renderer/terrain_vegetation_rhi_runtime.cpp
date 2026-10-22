#include "renderer/terrain_vegetation_rhi_runtime.h"

#include <algorithm>

namespace arp::renderer {
namespace {

auto findPage(core::pmr::Vector<TerrainVegetationGpuPage>& pages, terrain::TerrainTileCoord tile)
{
    return std::find_if(pages.begin(), pages.end(), [tile](const auto& page) {
        return page.tile == tile;
    });
}

} // namespace

TerrainVegetationRhiRuntime::Result TerrainVegetationRhiRuntime::activate(
    rhi::GpuDevice& device, const TerrainVegetationPage& source, std::uint64_t submissionSerial)
{
    Result result;
    if (submissionSerial == 0u || source.vegetationCatalogResourceId.empty()) {
        result.diagnostics = "TERRAIN-VEGETATION-RHI-PAGE-INVALID";
        return result;
    }
    if (device.backend() != rhi::Backend::Null && device.backend() != rhi::Backend::Vulkan) {
        result.diagnostics =
            "TERRAIN-VEGETATION-RHI-DEGRADED: backend has no validated static-instance payload";
        return result;
    }
    auto current = findPage(active_, source.tile);
    if (current != active_.end() && current->terrainGeneration == source.terrainGeneration) {
        result.success = true;
        result.unchanged = true;
        return result;
    }
    if (current != active_.end() && !retired_.canRetire()) {
        result.diagnostics = "TERRAIN-VEGETATION-RHI-RETIREMENT-CAPACITY-EXHAUSTED";
        return result;
    }
    TerrainVegetationGpuPage candidate;
    candidate.tile = source.tile;
    candidate.terrainGeneration = source.terrainGeneration;
    candidate.prototypes.reserve(source.prototypes.size());
    for (const auto& prototype : source.prototypes) {
        core::pmr::Vector<vegetation::InstanceDataGpu> instances{&core::defaultCpuMemoryResource()};
        if (!lowerTerrainVegetationInstancesForCull(prototype, instances, result.diagnostics))
            return result;
        if (instances.empty())
            continue;
        TerrainVegetationGpuPrototypePage gpuPrototype;
        gpuPrototype.prototypeId = prototype.prototypeId;
        gpuPrototype.instanceCount = static_cast<std::uint32_t>(instances.size());
        gpuPrototype.instanceBuffer = device.createBuffer({"TerrainVegetationCullInstances",
            instances.size() * sizeof(vegetation::InstanceDataGpu),
            rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::ShaderRead});
        if (!gpuPrototype.instanceBuffer) {
            result.diagnostics = "TERRAIN-VEGETATION-RHI-BUFFER-CREATE-FAILED";
            return result;
        }
        const auto written = device.writeBuffer(*gpuPrototype.instanceBuffer,
            instances.data(),
            instances.size() * sizeof(vegetation::InstanceDataGpu));
        if (!written.success) {
            result.diagnostics = "TERRAIN-VEGETATION-RHI-BUFFER-UPLOAD-FAILED: ";
            result.diagnostics += written.diagnostics;
            return result;
        }
        candidate.prototypes.push_back(std::move(gpuPrototype));
    }
    if (current == active_.end()) {
        active_.push_back(std::move(candidate));
    } else {
        TerrainVegetationGpuPage retired = std::move(*current);
        *current = std::move(candidate);
        if (!retired_.retire(submissionSerial, std::move(retired))) {
            result.diagnostics = "TERRAIN-VEGETATION-RHI-RETIREMENT-CAPACITY-EXHAUSTED";
            return result;
        }
    }
    result.success = true;
    return result;
}

bool TerrainVegetationRhiRuntime::deactivate(
    terrain::TerrainTileCoord tile, std::uint64_t submissionSerial)
{
    if (submissionSerial == 0u)
        return false;
    const auto current = findPage(active_, tile);
    if (current == active_.end())
        return false;
    if (!retired_.canRetire())
        return false;
    TerrainVegetationGpuPage retired = std::move(*current);
    active_.erase(current);
    return retired_.retire(submissionSerial, std::move(retired));
}

const TerrainVegetationGpuPage* TerrainVegetationRhiRuntime::page(
    terrain::TerrainTileCoord tile) const noexcept
{
    const auto found = std::find_if(active_.begin(), active_.end(), [tile](const auto& page) {
        return page.tile == tile;
    });
    return found == active_.end() ? nullptr : &*found;
}

} // namespace arp::renderer
