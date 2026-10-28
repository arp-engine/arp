#pragma once

#include "rhi/frontend/resource_barrier_values.h"

namespace arp::rhi::semantics {

inline constexpr std::uint32_t kResourceMemoryBarrierVersion = 1u;

inline const char* validateResourceMemoryBarrierDesc(
    const rhi::ResourceMemoryBarrierDesc& desc, rhi::GpuResourceId resource) noexcept
{
    if (!resource.isValid() || !desc.resource.isValid() || desc.resource != resource) {
        return "RHI-RESOURCE-MEMORY-BARRIER-RESOURCE-MISMATCH";
    }
    if (desc.sourceStages == rhi::PipelineStage::None) {
        return "RHI-RESOURCE-MEMORY-BARRIER-SOURCE-STAGE-MISSING";
    }
    if (desc.destinationStages == rhi::PipelineStage::None) {
        return "RHI-RESOURCE-MEMORY-BARRIER-DESTINATION-STAGE-MISSING";
    }
    if (desc.sourceAccess == rhi::ResourceAccess::None) {
        return "RHI-RESOURCE-MEMORY-BARRIER-SOURCE-ACCESS-MISSING";
    }
    if (desc.destinationAccess == rhi::ResourceAccess::None) {
        return "RHI-RESOURCE-MEMORY-BARRIER-DESTINATION-ACCESS-MISSING";
    }
    return nullptr;
}

} // namespace arp::rhi::semantics
