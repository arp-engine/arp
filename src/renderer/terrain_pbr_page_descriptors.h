#pragma once

#include "core/memory/cpu_allocator.h"
#include "renderer/terrain_gpu_rhi_uploader.h"
#include "rhi/gpu_descriptors.h"

namespace arp::renderer {

/// Stable set-0 bindings declared by wocc/pbr_scene_terrain.slang. The
/// terrain material owns bindings 0-28 other than the asset page payload;
/// this adapter owns exactly the validated TerrainAsset page resources.
struct TerrainPbrPageBindings final {
    static constexpr std::uint32_t weightPageA = 24u;
    static constexpr std::uint32_t weightPageB = 25u;
    static constexpr std::uint32_t pageTable = 29u;
    static constexpr std::uint32_t pageTableParams = 30u;
};

struct TerrainPbrPageDescriptorResult final {
    bool success = false;
    std::uint64_t pageTableGeneration = 0u;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

/// Binds the renderer-owned TerrainAsset array-page payload to a material set.
/// The caller is responsible for the rest of the terrain material contract.
/// This function never supplies placeholder pages: unavailable page payloads
/// or an incompatible descriptor layout produce a diagnostic failure.
TerrainPbrPageDescriptorResult writeTerrainPbrPageDescriptors(rhi::DescriptorSet& descriptorSet,
    const TerrainGpuRhiUploader& uploader,
    const TerrainGpuRhiUploader::PageTableSnapshot& pageTableSnapshot);

/// Captures the uploader's current immutable page-table generation before
/// binding it. Render submissions that span an asset reload must instead pass
/// their retained snapshot explicitly through the overload above.
TerrainPbrPageDescriptorResult writeTerrainPbrPageDescriptors(
    rhi::DescriptorSet& descriptorSet, const TerrainGpuRhiUploader& uploader);

} // namespace arp::renderer
