#include "rhi/backends/metal/metal_backend_queries.h"

#include "core/cpu_allocator.h"

#include <limits>

namespace arp::rhi_metal {

arp::core::UniquePtr<rhi::QueryPool> createMetalRhiQueryPool(const rhi::QueryPoolDesc& desc,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract)
{
    const auto descriptorValidation = rhi::validateQueryPoolDesc(desc);
    if (!descriptorValidation.success) {
        return {};
    }
    const bool timestamp = desc.type == rhi::QueryType::Timestamp;
    const bool pipelineStatistics = desc.type == rhi::QueryType::PipelineStatistics;
    if (timestamp
        && (!rhi::semantics::hasExecutableOptionalNativeFeature(
                capabilityContract, rhi::semantics::NativeBackendFeature::TimestampQueries)
            || !metalRhiNativeTimestampQueriesAvailable())) {
        return {};
    }
    if (pipelineStatistics
        && (!rhi::semantics::hasExecutableOptionalNativeFeature(
                capabilityContract, rhi::semantics::NativeBackendFeature::PipelineStatisticsQueries)
            || !metalRhiNativePipelineStatisticsQueriesAvailable())) {
        return {};
    }
    auto payload = createMetalRhiNativeQueryPoolPayload(desc);
    if (!payload || !payload->nativeRuntimeBacked) {
        return {};
    }
    return arp::core::makeUnique<MetalRhiQueryPool>(std::move(payload), desc);
}

const MetalRhiQueryPool* asMetalRhiQueryPool(const rhi::QueryPool& pool)
{
    return dynamic_cast<const MetalRhiQueryPool*>(&pool);
}

} // namespace arp::rhi_metal
