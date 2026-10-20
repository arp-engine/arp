#pragma once

#include "core/cpu_allocator.h"
#include "rhi/gpu_device.h"

#include <cstdint>
#include <memory>
#include <mutex>

namespace arp::rhi_metal {

enum class MetalRhiNativeQueryState : std::uint8_t {
    NotRecorded,
    Recorded,
    Pending,
    Available,
    DiagnosticFailure,
};

struct MetalRhiNativeQueryPoolPayload {
    std::shared_ptr<void> nativeDevice;
    std::shared_ptr<void> nativeCounterSampleBuffer;
    core::pmr::Vector<std::uint64_t> scopeIds{
        &core::defaultCpuMemoryResource(),
    };
    std::uint32_t count = 0;
    rhi::PipelineStatisticsMask pipelineStatisticsMask = 0;
    core::pmr::Vector<MetalRhiNativeQueryState> timestampStates{
        &core::defaultCpuMemoryResource(),
    };
    core::pmr::Vector<MetalRhiNativeQueryState> pipelineStatisticsStates{
        &core::defaultCpuMemoryResource(),
    };
    core::pmr::Vector<rhi::GpuPipelineStatisticsSample> pipelineStatisticsResults{
        &core::defaultCpuMemoryResource(),
    };
    mutable std::mutex pipelineStatisticsMutex;
    bool pipelineStatistics = false;
    bool nativeRuntimeBacked = false;
    bool nativeRuntimeDiagnosticOnly = true;
};

struct MetalRhiRecordedTimestampQuery {
    std::shared_ptr<MetalRhiNativeQueryPoolPayload> pool;
    std::uint32_t queryIndex = 0;
};

struct MetalRhiRecordedPipelineStatisticsQuery {
    std::shared_ptr<MetalRhiNativeQueryPoolPayload> pool;
    std::uint32_t queryIndex = 0;
};

// Backend-owned metadata; taking these vectors proves ownership transfer only,
// never native query support. Prepared submission state embeds this object so
// the native block captures an existing owner rather than copying its vectors.
class MetalRhiSubmittedQueryRetention final {
public:
    MetalRhiSubmittedQueryRetention() = default;
    MetalRhiSubmittedQueryRetention(const MetalRhiSubmittedQueryRetention&) = delete;
    MetalRhiSubmittedQueryRetention& operator=(const MetalRhiSubmittedQueryRetention&) = delete;

    bool canTakeRecorded(
        const core::pmr::Vector<std::shared_ptr<MetalRhiNativeQueryPoolPayload>>& pools,
        const core::pmr::Vector<MetalRhiRecordedTimestampQuery>& timestamps,
        const core::pmr::Vector<MetalRhiRecordedPipelineStatisticsQuery>& statistics) const noexcept
    {
        const auto* resource = &core::defaultCpuMemoryResource();
        return !taken_ && pools.get_allocator().resource() == resource
            && timestamps.get_allocator().resource() == resource
            && statistics.get_allocator().resource() == resource;
    }

    bool takeRecorded(core::pmr::Vector<std::shared_ptr<MetalRhiNativeQueryPoolPayload>>& pools,
        core::pmr::Vector<MetalRhiRecordedTimestampQuery>& timestamps,
        core::pmr::Vector<MetalRhiRecordedPipelineStatisticsQuery>& statistics) noexcept
    {
        if (!canTakeRecorded(pools, timestamps, statistics))
            return false;
        pools_ = std::move(pools);
        timestamps_ = std::move(timestamps);
        statistics_ = std::move(statistics);
        taken_ = true;
        return true;
    }

    const auto& pools() const noexcept { return pools_; }

    const auto& timestamps() const noexcept { return timestamps_; }

    const auto& statistics() const noexcept { return statistics_; }

    void releaseAfterResolution() noexcept
    {
        timestamps_.clear();
        statistics_.clear();
        pools_.clear();
    }

private:
    bool taken_ = false;
    core::pmr::Vector<std::shared_ptr<MetalRhiNativeQueryPoolPayload>> pools_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<MetalRhiRecordedTimestampQuery> timestamps_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<MetalRhiRecordedPipelineStatisticsQuery> statistics_{
        &core::defaultCpuMemoryResource()};
};

std::shared_ptr<MetalRhiNativeQueryPoolPayload> createMetalRhiNativeQueryPoolPayload(
    const rhi::QueryPoolDesc& desc);

bool recordMetalRhiNativeTimestamp(const std::shared_ptr<void>& nativeEncoder,
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload,
    std::uint32_t queryIndex,
    std::pmr::string& diagnostics);

bool markMetalRhiNativeTimestampQuerySubmitted(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload,
    std::uint32_t queryIndex,
    std::pmr::string& diagnostics);

/// Undo a submission mark when command-buffer submission fails before commit.
/// Query state is shared with the QueryPool object, so the submit path must be
/// transactional rather than leaving an earlier query permanently Pending.
bool rollbackMetalRhiNativeTimestampQuerySubmitted(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload,
    std::uint32_t queryIndex,
    std::pmr::string& diagnostics);

bool recordMetalRhiNativePipelineStatistics(const std::shared_ptr<void>& nativeEncoder,
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload,
    std::uint32_t queryIndex,
    std::pmr::string& diagnostics);

bool markMetalRhiNativeQueryPoolSubmitted(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload,
    std::uint32_t queryIndex,
    std::pmr::string& diagnostics);

/// Undo a pipeline-statistics submission mark when command-buffer submission
/// fails before commit. This also restores the pending-query accounting.
bool rollbackMetalRhiNativePipelineStatisticsQuerySubmitted(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload,
    std::uint32_t queryIndex,
    std::pmr::string& diagnostics);

rhi::CommandValidationResult resetMetalRhiNativeQueryPool(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload);

rhi::QueryExecutionStatus metalRhiNativeQueryPoolExecutionStatus(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload);

rhi::GpuPipelineStatisticsSample readMetalRhiNativePipelineStatistics(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload, std::uint32_t queryIndex);

void resolveMetalRhiNativeQueryPool(const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload,
    std::uint64_t frameSerial,
    const core::pmr::Vector<MetalRhiRecordedTimestampQuery>& queries,
    bool commandBufferSucceeded);

rhi::GpuTimingSample latestMetalRhiNativeTimingSample();
void resolveMetalRhiNativePipelineStatistics(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload,
    std::uint64_t frameSerial,
    std::uint32_t queryIndex,
    bool commandBufferSucceeded);
rhi::GpuPipelineStatisticsSample latestMetalRhiNativePipelineStatistics();
std::uint64_t allocateMetalRhiNativeTimingFrameSerial();
/// EncoderCounterSampling/1: the current write-query lowering samples at
/// draw/dispatch boundaries. Counter-buffer creation alone is insufficient.
/// A device offering only stage-boundary samples cannot execute this lowering.
bool metalRhiNativeTimestampQueriesAvailable();
bool metalRhiNativePipelineStatisticsQueriesAvailable();

} // namespace arp::rhi_metal
