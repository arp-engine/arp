#pragma once

#include "renderer/volumetric_cloud/cloud_prepared_resources.h"
#include "rhi/frontend/descriptor_general_write_validation.h"
#include "rhi/gpu_command_sources.h"

namespace arp::renderer::cloud {

// Positions in the caller's one canonical prefix. Reset-frame previous/raw may
// use the same position. Color/depth name original full sampled-view sources.
struct CloudPreparedDescriptorLocations {
    std::size_t constants = SIZE_MAX, density = SIZE_MAX, raw = SIZE_MAX, previous = SIZE_MAX,
                resolved = SIZE_MAX, reconstructionConstants = SIZE_MAX, color = SIZE_MAX,
                depth = SIZE_MAX;
};

struct CloudPreparedDescriptorInput {
    rhi::GpuDevice& device;
    const CloudPreparedResources& resources;
    const std::shared_ptr<rhi::OriginalCommandResourceSources>& sources;
    CloudPreparedDescriptorLocations locations;
};

struct CloudPreparedDescriptorDeclarations {
    struct Pass {
        std::array<rhi::OriginalCommandDescriptorSetElement, 7u> elements{};
        std::uint32_t count = 0u;
    };

    std::array<Pass, kCloudPipelineCount> passes{};
    std::uint64_t historyGeneration = 0u;
};

struct CloudPreparedDescriptorPublishContext {
    rhi::GpuDevice& device;
    const CloudPreparedResources& resources;
    const CloudPreparedPipelines& pipelines;
    // The candidate inventory must have one external owner and enough existing
    // Assembly8 capacity for all Sources. Pipelines already belong to it.
    std::shared_ptr<rhi::OriginalCommandResourceSources>& inventory;
    // A fresh registered arena with at least three sets and 12/15 elements;
    // additional authored capacity can serve the rest of the same scene graph.
    core::UniquePtr<rhi::DescriptorArena>& arena;
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    CloudPreparedDescriptorLocations locations;
    std::array<std::size_t, kCloudPipelineCount> pipelineSources{SIZE_MAX, SIZE_MAX, SIZE_MAX};
    std::array<rhi::OriginalGeneralDescriptorWriteRequest, kCloudPipelineCount> requests;
    std::array<std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>, kCloudPipelineCount> quotas;
    std::uint64_t maxCpuOwnerBytes = 0u;
    std::shared_ptr<core::CpuMemoryQuota> ownerQuota;
};

struct CloudPreparedDescriptorOutput {
    std::array<std::size_t, kCloudPipelineCount> descriptors{SIZE_MAX, SIZE_MAX, SIZE_MAX};
    std::uint64_t historyGeneration = 0u;
    std::shared_ptr<rhi::DescriptorArena> arena;
};

class ARP_API CloudPreparedDescriptors final {
public:
    // No publication/command authority. Exact current pending owner, finite
    // ranges, view shape/parent and canonical Source quota are checked here.
    static CloudResult describe(
        const CloudPreparedDescriptorInput&, CloudPreparedDescriptorDeclarations&) noexcept;
    // Preflight failures preserve the caller's candidates. Once native writes
    // begin, both candidates are consumed: success publishes the same inventory,
    // failure destroys it and the arena. No partial inventory escapes. Sources
    // retain their original leases/arena independently of this cold stack call.
    static CloudResult publish(
        const CloudPreparedDescriptorPublishContext&, CloudPreparedDescriptorOutput&);
};
} // namespace arp::renderer::cloud
