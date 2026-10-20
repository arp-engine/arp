#include "rhi/backends/metal/metal_native_queries.h"

namespace arp::rhi_metal {

std::shared_ptr<MetalRhiNativeQueryPoolPayload> createMetalRhiNativeQueryPoolPayload(
    const rhi::QueryPoolDesc&)
{
    return {};
}

bool recordMetalRhiNativeTimestamp(const std::shared_ptr<void>&,
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>&,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-TIMESTAMP-QUERY-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool markMetalRhiNativeTimestampQuerySubmitted(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>&,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-TIMESTAMP-QUERY-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool rollbackMetalRhiNativeTimestampQuerySubmitted(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>&,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-TIMESTAMP-QUERY-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool recordMetalRhiNativePipelineStatistics(const std::shared_ptr<void>&,
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>&,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-PIPELINE-STATISTICS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool markMetalRhiNativeQueryPoolSubmitted(const std::shared_ptr<MetalRhiNativeQueryPoolPayload>&,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-PIPELINE-STATISTICS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

bool rollbackMetalRhiNativePipelineStatisticsQuerySubmitted(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>&,
    std::uint32_t,
    std::pmr::string& diagnostics)
{
    diagnostics = "RHI-METAL-PIPELINE-STATISTICS-NATIVE-RUNTIME-UNAVAILABLE";
    return false;
}

rhi::CommandValidationResult resetMetalRhiNativeQueryPool(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>&)
{
    return {
        false,
        "RHI-METAL-QUERY-POOL-RESET-NATIVE-RUNTIME-UNAVAILABLE",
        0,
    };
}

rhi::QueryExecutionStatus metalRhiNativeQueryPoolExecutionStatus(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload)
{
    rhi::QueryExecutionStatus status;
    status.type = payload && payload->pipelineStatistics ? rhi::QueryType::PipelineStatistics
                                                         : rhi::QueryType::Timestamp;
    status.count = payload ? payload->count : 0u;
    status.diagnostics = "RHI-METAL-QUERY-POOL-NATIVE-RUNTIME-UNAVAILABLE";
    return status;
}

rhi::GpuPipelineStatisticsSample readMetalRhiNativePipelineStatistics(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>&, std::uint32_t queryIndex)
{
    rhi::GpuPipelineStatisticsSample sample;
    sample.queryIndex = queryIndex;
    sample.availability = rhi::PipelineStatisticsAvailability::Unsupported;
    sample.diagnostic =
        "Metal pipeline statistics are unavailable outside an Apple Objective-C++ runtime";
    return sample;
}

void resolveMetalRhiNativeQueryPool(const std::shared_ptr<MetalRhiNativeQueryPoolPayload>&,
    std::uint64_t,
    const core::pmr::Vector<MetalRhiRecordedTimestampQuery>&,
    bool)
{}

rhi::GpuTimingSample latestMetalRhiNativeTimingSample()
{
    rhi::GpuTimingSample sample;
    sample.availability = rhi::GpuTimingAvailability::Unsupported;
    sample.diagnostic = "Metal GPU timing is unavailable outside an Apple Objective-C++ runtime";
    return sample;
}

void resolveMetalRhiNativePipelineStatistics(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>&, std::uint64_t, std::uint32_t, bool)
{}

rhi::GpuPipelineStatisticsSample latestMetalRhiNativePipelineStatistics()
{
    rhi::GpuPipelineStatisticsSample sample;
    sample.availability = rhi::PipelineStatisticsAvailability::Unsupported;
    sample.diagnostic =
        "Metal pipeline statistics are unavailable outside an Apple Objective-C++ runtime";
    return sample;
}

std::uint64_t allocateMetalRhiNativeTimingFrameSerial()
{
    return 0u;
}

bool metalRhiNativeTimestampQueriesAvailable()
{
    return false;
}

bool metalRhiNativePipelineStatisticsQueriesAvailable()
{
    return false;
}

} // namespace arp::rhi_metal
