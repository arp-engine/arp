#include "rhi/backends/metal/metal_native_queries.h"

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_native_runtime.h"

#include <Metal/Metal.h>
#include <algorithm>
#include <atomic>
#include <mutex>

namespace arp::rhi_metal {

namespace {

id<MTLDevice> defaultMetalDevice()
{
    return reinterpret_cast<id<MTLDevice>>(metalRhiNativeDeviceHandle());
}

bool supportsEncoderCounterSampling(id<MTLDevice> device)
{
    if (@available(macOS 11.0, iOS 14.0, *)) {
        return device != nil &&
            [device supportsCounterSampling:MTLCounterSamplingPointAtDrawBoundary] &&
            [device supportsCounterSampling:MTLCounterSamplingPointAtDispatchBoundary];
    }
    return false;
}

bool supportsEncoderCounterSampling(id<MTLCommandEncoder> encoder)
{
    if (@available(macOS 11.0, iOS 14.0, *)) {
        if ([encoder conformsToProtocol:@protocol(MTLRenderCommandEncoder)]) {
            return [encoder.device supportsCounterSampling:MTLCounterSamplingPointAtDrawBoundary];
        }
        if ([encoder conformsToProtocol:@protocol(MTLComputeCommandEncoder)]) {
            return
                [encoder.device supportsCounterSampling:MTLCounterSamplingPointAtDispatchBoundary];
        }
    }
    return false;
}

void sampleEncoderCounters(
    id<MTLCommandEncoder> encoder, id<MTLCounterSampleBuffer> buffer, std::uint32_t index)
{
    if ([encoder conformsToProtocol:@protocol(MTLRenderCommandEncoder)]) {
        [(id<MTLRenderCommandEncoder>)encoder sampleCountersInBuffer:buffer
                                                       atSampleIndex:index
                                                         withBarrier:YES];
    } else {
        [(id<MTLComputeCommandEncoder>)encoder sampleCountersInBuffer:buffer
                                                        atSampleIndex:index
                                                          withBarrier:YES];
    }
}

std::shared_ptr<void> retainNativeObject(id object)
{
    if (object == nil)
        return {};
    return std::shared_ptr<void>(reinterpret_cast<void*>(object), [](void* nativeObject) {
        if (nativeObject)
            [reinterpret_cast<id>(nativeObject) release];
    });
}

id<MTLCounterSet> findTimestampCounterSet(id<MTLDevice> device)
{
    if (device == nil)
        return nil;
    for (id<MTLCounterSet> counterSet in device.counterSets) {
        NSString* name = counterSet.name.lowercaseString;
        if ([name containsString:@"timestamp"])
            return counterSet;
        for (id<MTLCounter> counter in counterSet.counters) {
            if ([counter.name.lowercaseString containsString:@"timestamp"]) {
                return counterSet;
            }
        }
    }
    return nil;
}

id<MTLCounterSet> findPipelineStatisticsCounterSet(id<MTLDevice> device)
{
    if (device == nil)
        return nil;
    for (id<MTLCounterSet> counterSet in device.counterSets) {
        NSString* name = counterSet.name.lowercaseString;
        if ([name containsString:@"statistic"] || [name containsString:@"counter"]) {
            for (id<MTLCounter> counter in counterSet.counters) {
                NSString* counterName = counter.name.lowercaseString;
                if ([counterName containsString:@"vertex"] ||
                    [counterName containsString:@"fragment"] ||
                    [counterName containsString:@"compute"]) {
                    return counterSet;
                }
            }
        }
    }
    return nil;
}

std::mutex& timingMutex()
{
    static std::mutex mutex;
    return mutex;
}

rhi::GpuTimingSample& completedTiming()
{
    static rhi::GpuTimingSample sample;
    return sample;
}

std::atomic<std::uint64_t>& timingSerial()
{
    static std::atomic<std::uint64_t> serial = 0;
    return serial;
}

std::atomic<bool>& hasCompletedTiming()
{
    static std::atomic<bool> completed = false;
    return completed;
}

std::mutex& pipelineStatisticsMutex()
{
    static std::mutex mutex;
    return mutex;
}

rhi::GpuPipelineStatisticsSample& completedPipelineStatistics()
{
    static rhi::GpuPipelineStatisticsSample sample;
    return sample;
}

std::atomic<bool>& hasCompletedPipelineStatistics()
{
    static std::atomic<bool> completed = false;
    return completed;
}

std::atomic<std::uint32_t>& pendingPipelineStatisticsQueries()
{
    static std::atomic<std::uint32_t> pending = 0;
    return pending;
}

} // namespace

std::shared_ptr<MetalRhiNativeQueryPoolPayload> createMetalRhiNativeQueryPoolPayload(
    const rhi::QueryPoolDesc& desc)
{
    if (!rhi::validateQueryPoolDesc(desc).success) {
        return {};
    }
    auto payload = arp::core::makeShared<MetalRhiNativeQueryPoolPayload>();
    payload->nativeDevice = metalRhiNativeDeviceIdentity();
    payload->count = desc.count;
    payload->pipelineStatistics = desc.type == rhi::QueryType::PipelineStatistics;
    payload->pipelineStatisticsMask = desc.pipelineStatisticsMask;
    if (!payload->pipelineStatistics) {
        payload->timestampStates.resize(desc.count, MetalRhiNativeQueryState::NotRecorded);
    }
    if (payload->pipelineStatistics) {
        payload->pipelineStatisticsStates.resize(desc.count, MetalRhiNativeQueryState::NotRecorded);
        payload->pipelineStatisticsResults.resize(desc.count);
        for (std::uint32_t index = 0; index < payload->pipelineStatisticsResults.size(); ++index) {
            auto& result = payload->pipelineStatisticsResults[index];
            result.availability = rhi::PipelineStatisticsAvailability::NotRecorded;
            result.queryIndex = index;
            result.statisticMask = desc.pipelineStatisticsMask;
            result.diagnostic = "Metal pipeline-statistics query has not been recorded";
        }
    }
    if (desc.timingScopeIds && desc.timingScopeIdCount) {
        payload->scopeIds.assign(
            desc.timingScopeIds, desc.timingScopeIds + desc.timingScopeIdCount);
    }
    if (!@available(macOS 10.15, iOS 14.0, *)) {
        return payload;
    }
    id<MTLDevice> device = defaultMetalDevice();
    id<MTLCounterSet> counterSet = payload->pipelineStatistics
        ? findPipelineStatisticsCounterSet(device)
        : findTimestampCounterSet(device);
    if (device == nil || counterSet == nil || !supportsEncoderCounterSampling(device))
        return payload;

    MTLCounterSampleBufferDescriptor* descriptor = [[MTLCounterSampleBufferDescriptor alloc] init];
    descriptor.counterSet = counterSet;
    descriptor.sampleCount = desc.count;
    descriptor.storageMode = MTLStorageModeShared;
    NSError* error = nil;
    id<MTLCounterSampleBuffer> sampleBuffer =
        [device newCounterSampleBufferWithDescriptor:descriptor error:&error];
    [descriptor release];
    payload->nativeCounterSampleBuffer = retainNativeObject(sampleBuffer);
    payload->nativeRuntimeBacked = sampleBuffer != nil;
    payload->nativeRuntimeDiagnosticOnly = !payload->nativeRuntimeBacked;
    return payload;
}

bool recordMetalRhiNativeTimestamp(const std::shared_ptr<void>& nativeEncoder,
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload,
    std::uint32_t queryIndex,
    std::pmr::string& diagnostics)
{
    if (!@available(macOS 10.15, iOS 14.0, *)) {
        diagnostics = "RHI-METAL-TIMESTAMP-QUERY-API-UNAVAILABLE";
        return false;
    }
    if (!nativeEncoder || !payload || !payload->nativeRuntimeBacked
        || !payload->nativeCounterSampleBuffer || queryIndex >= payload->count
        || queryIndex >= payload->timestampStates.size()) {
        diagnostics = "RHI-METAL-TIMESTAMP-QUERY-PAYLOAD-MISSING";
        return false;
    }
    id<MTLCommandEncoder> encoder = reinterpret_cast<id<MTLCommandEncoder>>(nativeEncoder.get());
    id<MTLCounterSampleBuffer> sampleBuffer =
        reinterpret_cast<id<MTLCounterSampleBuffer>>(payload->nativeCounterSampleBuffer.get());
    if (encoder == nil || sampleBuffer == nil || encoder.device == nil
        || sampleBuffer.device != encoder.device) {
        diagnostics = "RHI-METAL-TIMESTAMP-QUERY-NATIVE-OBJECT-MISSING";
        return false;
    }
    if (!supportsEncoderCounterSampling(encoder)) {
        diagnostics = "RHI-METAL-TIMESTAMP-SAMPLING-POINT-UNSUPPORTED";
        return false;
    }
    {
        std::lock_guard lock(payload->pipelineStatisticsMutex);
        if (payload->pipelineStatistics || queryIndex >= payload->timestampStates.size()
            || payload->timestampStates[queryIndex] != MetalRhiNativeQueryState::NotRecorded) {
            diagnostics = "RHI-METAL-TIMESTAMP-QUERY-MUST-BE-RESET-FIRST";
            return false;
        }
        payload->timestampStates[queryIndex] = MetalRhiNativeQueryState::Recorded;
    }
    sampleEncoderCounters(encoder, sampleBuffer, queryIndex);
    return true;
}

bool markMetalRhiNativeTimestampQuerySubmitted(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload,
    std::uint32_t queryIndex,
    std::pmr::string& diagnostics)
{
    if (!payload || payload->pipelineStatistics || !payload->nativeRuntimeBacked
        || !payload->nativeCounterSampleBuffer) {
        diagnostics = "RHI-METAL-TIMESTAMP-QUERY-SUBMISSION-PAYLOAD-INVALID";
        return false;
    }
    std::lock_guard lock(payload->pipelineStatisticsMutex);
    if (queryIndex >= payload->timestampStates.size()
        || payload->timestampStates[queryIndex] != MetalRhiNativeQueryState::Recorded) {
        diagnostics = "RHI-METAL-TIMESTAMP-QUERY-SUBMISSION-STATE-INVALID";
        return false;
    }
    payload->timestampStates[queryIndex] = MetalRhiNativeQueryState::Pending;
    return true;
}

bool rollbackMetalRhiNativeTimestampQuerySubmitted(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload,
    std::uint32_t queryIndex,
    std::pmr::string& diagnostics)
{
    if (!payload) {
        diagnostics = "RHI-METAL-TIMESTAMP-QUERY-ROLLBACK-PAYLOAD-MISSING";
        return false;
    }
    std::lock_guard lock(payload->pipelineStatisticsMutex);
    if (queryIndex >= payload->timestampStates.size()
        || payload->timestampStates[queryIndex] != MetalRhiNativeQueryState::Pending) {
        diagnostics = "RHI-METAL-TIMESTAMP-QUERY-ROLLBACK-STATE-INVALID";
        return false;
    }
    payload->timestampStates[queryIndex] = MetalRhiNativeQueryState::Recorded;
    return true;
}

bool recordMetalRhiNativePipelineStatistics(const std::shared_ptr<void>& nativeEncoder,
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload,
    std::uint32_t queryIndex,
    std::pmr::string& diagnostics)
{
    if (!@available(macOS 10.15, iOS 14.0, *)) {
        diagnostics = "RHI-METAL-PIPELINE-STATISTICS-QUERY-API-UNAVAILABLE";
        return false;
    }
    if (!nativeEncoder || !payload || !payload->pipelineStatistics || !payload->nativeRuntimeBacked
        || !payload->nativeCounterSampleBuffer || queryIndex >= payload->count
        || queryIndex >= payload->pipelineStatisticsStates.size()
        || queryIndex >= payload->pipelineStatisticsResults.size()) {
        diagnostics = "RHI-METAL-PIPELINE-STATISTICS-QUERY-PAYLOAD-MISSING";
        return false;
    }
    id<MTLCommandEncoder> encoder = reinterpret_cast<id<MTLCommandEncoder>>(nativeEncoder.get());
    id<MTLCounterSampleBuffer> sampleBuffer =
        reinterpret_cast<id<MTLCounterSampleBuffer>>(payload->nativeCounterSampleBuffer.get());
    if (encoder == nil || sampleBuffer == nil || encoder.device == nil
        || sampleBuffer.device != encoder.device) {
        diagnostics = "RHI-METAL-PIPELINE-STATISTICS-QUERY-NATIVE-OBJECT-MISSING";
        return false;
    }
    if (!supportsEncoderCounterSampling(encoder)) {
        diagnostics = "RHI-METAL-STATISTICS-SAMPLING-POINT-UNSUPPORTED";
        return false;
    }
    {
        std::lock_guard lock(payload->pipelineStatisticsMutex);
        if (queryIndex >= payload->pipelineStatisticsStates.size()
            || queryIndex >= payload->pipelineStatisticsResults.size()
            || payload->pipelineStatisticsStates[queryIndex]
                != MetalRhiNativeQueryState::NotRecorded) {
            diagnostics = "RHI-METAL-PIPELINE-STATISTICS-QUERY-MUST-BE-RESET-FIRST";
            return false;
        }
        payload->pipelineStatisticsStates[queryIndex] = MetalRhiNativeQueryState::Recorded;
        payload->pipelineStatisticsResults[queryIndex].availability =
            rhi::PipelineStatisticsAvailability::Pending;
        payload->pipelineStatisticsResults[queryIndex].queryIndex = queryIndex;
        payload->pipelineStatisticsResults[queryIndex].statisticMask =
            payload->pipelineStatisticsMask;
        payload->pipelineStatisticsResults[queryIndex].diagnostic =
            "Metal GPU pipeline-statistics query is recorded and awaiting submission";
    }
    sampleEncoderCounters(encoder, sampleBuffer, queryIndex);
    return true;
}

bool markMetalRhiNativeQueryPoolSubmitted(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload,
    std::uint32_t queryIndex,
    std::pmr::string& diagnostics)
{
    if (!payload || !payload->pipelineStatistics || !payload->nativeRuntimeBacked
        || !payload->nativeCounterSampleBuffer) {
        diagnostics = "RHI-METAL-PIPELINE-STATISTICS-QUERY-SUBMISSION-PAYLOAD-INVALID";
        return false;
    }
    {
        std::lock_guard lock(payload->pipelineStatisticsMutex);
        if (queryIndex >= payload->count || queryIndex >= payload->pipelineStatisticsStates.size()
            || queryIndex >= payload->pipelineStatisticsResults.size()
            || payload->pipelineStatisticsStates[queryIndex]
                != MetalRhiNativeQueryState::Recorded) {
            diagnostics = "RHI-METAL-PIPELINE-STATISTICS-QUERY-SUBMISSION-STATE-INVALID";
            return false;
        }
        payload->pipelineStatisticsStates[queryIndex] = MetalRhiNativeQueryState::Pending;
        payload->pipelineStatisticsResults[queryIndex].availability =
            rhi::PipelineStatisticsAvailability::Pending;
        payload->pipelineStatisticsResults[queryIndex].diagnostic =
            "Metal GPU pipeline-statistics query data is pending completion";
    }
    pendingPipelineStatisticsQueries().fetch_add(1u, std::memory_order_acq_rel);
    return true;
}

bool rollbackMetalRhiNativePipelineStatisticsQuerySubmitted(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload,
    std::uint32_t queryIndex,
    std::pmr::string& diagnostics)
{
    if (!payload || !payload->pipelineStatistics) {
        diagnostics = "RHI-METAL-PIPELINE-STATISTICS-QUERY-ROLLBACK-PAYLOAD-MISSING";
        return false;
    }
    std::lock_guard lock(payload->pipelineStatisticsMutex);
    if (queryIndex >= payload->pipelineStatisticsStates.size()
        || payload->pipelineStatisticsStates[queryIndex] != MetalRhiNativeQueryState::Pending) {
        diagnostics = "RHI-METAL-PIPELINE-STATISTICS-QUERY-ROLLBACK-STATE-INVALID";
        return false;
    }
    payload->pipelineStatisticsStates[queryIndex] = MetalRhiNativeQueryState::Recorded;
    payload->pipelineStatisticsResults[queryIndex].availability =
        rhi::PipelineStatisticsAvailability::Pending;
    payload->pipelineStatisticsResults[queryIndex].diagnostic =
        "Metal GPU pipeline-statistics query submission rolled back before commit";
    pendingPipelineStatisticsQueries().fetch_sub(1u, std::memory_order_acq_rel);
    return true;
}

rhi::CommandValidationResult resetMetalRhiNativeQueryPool(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload)
{
    if (!payload || !payload->nativeRuntimeBacked) {
        return {
            false,
            "RHI-METAL-QUERY-POOL-RESET-NATIVE-PAYLOAD-MISSING",
            0,
        };
    }
    std::lock_guard lock(payload->pipelineStatisticsMutex);
    const auto& states =
        payload->pipelineStatistics ? payload->pipelineStatisticsStates : payload->timestampStates;
    for (const auto state : states) {
        if (state == MetalRhiNativeQueryState::Recorded
            || state == MetalRhiNativeQueryState::Pending) {
            return {
                false,
                "RHI-METAL-QUERY-POOL-RESET-WAITING-FOR-COMPLETION",
                0,
            };
        }
    }
    if (!payload->pipelineStatistics) {
        std::fill(payload->timestampStates.begin(),
            payload->timestampStates.end(),
            MetalRhiNativeQueryState::NotRecorded);
        return {true, "", 0};
    }
    for (std::uint32_t index = 0; index < payload->count; ++index) {
        payload->pipelineStatisticsStates[index] = MetalRhiNativeQueryState::NotRecorded;
        payload->pipelineStatisticsResults[index] = {};
        payload->pipelineStatisticsResults[index].availability =
            rhi::PipelineStatisticsAvailability::NotRecorded;
        payload->pipelineStatisticsResults[index].queryIndex = index;
        payload->pipelineStatisticsResults[index].statisticMask = payload->pipelineStatisticsMask;
        payload->pipelineStatisticsResults[index].diagnostic =
            "Metal pipeline-statistics query has not been recorded";
    }
    return {true, "", 0};
}

rhi::QueryExecutionStatus metalRhiNativeQueryPoolExecutionStatus(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload)
{
    rhi::QueryExecutionStatus status;
    status.type = payload && payload->pipelineStatistics ? rhi::QueryType::PipelineStatistics
                                                         : rhi::QueryType::Timestamp;
    status.count = payload ? payload->count : 0u;
    status.creationSupported =
        payload && payload->nativeRuntimeBacked && payload->nativeCounterSampleBuffer;
    status.resetSupported = status.creationSupported;
    status.asynchronousReadbackSupported = status.creationSupported;
    status.completionGatedLifetimeRequired = status.creationSupported;
    if (!payload) {
        status.state = rhi::QueryExecutionState::DiagnosticFailure;
        status.diagnostics = "RHI-METAL-QUERY-POOL-PAYLOAD-MISSING";
        return status;
    }
    const auto& states =
        payload->pipelineStatistics ? payload->pipelineStatisticsStates : payload->timestampStates;
    std::lock_guard lock(payload->pipelineStatisticsMutex);
    bool hasDiagnosticFailure = false;
    bool hasPending = false;
    bool hasRecorded = false;
    bool hasAvailable = false;
    for (const auto state : states) {
        hasDiagnosticFailure =
            hasDiagnosticFailure || state == MetalRhiNativeQueryState::DiagnosticFailure;
        hasPending = hasPending || state == MetalRhiNativeQueryState::Pending;
        hasRecorded = hasRecorded || state == MetalRhiNativeQueryState::Recorded;
        hasAvailable = hasAvailable || state == MetalRhiNativeQueryState::Available;
    }
    if (!status.creationSupported) {
        status.state = rhi::QueryExecutionState::Unsupported;
        status.diagnostics = "RHI-METAL-QUERY-POOL-NATIVE-PAYLOAD-MISSING";
    } else if (hasDiagnosticFailure) {
        status.state = rhi::QueryExecutionState::DiagnosticFailure;
        status.diagnostics = "RHI-METAL-QUERY-POOL-READBACK-FAILED";
    } else if (hasPending) {
        status.state = rhi::QueryExecutionState::Pending;
        status.diagnostics = "RHI-METAL-QUERY-POOL-WAITING-FOR-COMPLETION";
    } else if (hasRecorded) {
        status.state = rhi::QueryExecutionState::Recorded;
        status.diagnostics = "RHI-METAL-QUERY-POOL-RECORDED-BEFORE-SUBMISSION";
    } else if (hasAvailable) {
        status.state = rhi::QueryExecutionState::Available;
        status.diagnostics = "RHI-METAL-QUERY-POOL-READBACK-AVAILABLE";
    } else {
        status.state = rhi::QueryExecutionState::NotRecorded;
        status.diagnostics = "RHI-METAL-QUERY-POOL-NOT-RECORDED";
    }
    return status;
}

rhi::GpuPipelineStatisticsSample readMetalRhiNativePipelineStatistics(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload, std::uint32_t queryIndex)
{
    rhi::GpuPipelineStatisticsSample sample;
    sample.queryIndex = queryIndex;
    if (!payload || !payload->pipelineStatistics || queryIndex >= payload->count
        || queryIndex >= payload->pipelineStatisticsResults.size()) {
        sample.availability = rhi::PipelineStatisticsAvailability::DiagnosticFailure;
        sample.diagnostic = "RHI-METAL-PIPELINE-STATISTICS-QUERY-IDENTITY-INVALID";
        return sample;
    }
    std::lock_guard lock(payload->pipelineStatisticsMutex);
    return payload->pipelineStatisticsResults[queryIndex];
}

void resolveMetalRhiNativePipelineStatistics(
    const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload,
    std::uint64_t frameSerial,
    std::uint32_t queryIndex,
    bool commandBufferSucceeded)
{
    if (!payload || !payload->pipelineStatistics || !payload->nativeRuntimeBacked
        || !payload->nativeCounterSampleBuffer) {
        return;
    }
    id<MTLCounterSampleBuffer> sampleBuffer =
        reinterpret_cast<id<MTLCounterSampleBuffer>>(payload->nativeCounterSampleBuffer.get());
    if (!commandBufferSucceeded) {
        std::lock_guard lock(payload->pipelineStatisticsMutex);
        if (queryIndex < payload->pipelineStatisticsStates.size()
            && queryIndex < payload->pipelineStatisticsResults.size()
            && payload->pipelineStatisticsStates[queryIndex] == MetalRhiNativeQueryState::Pending) {
            auto& sample = payload->pipelineStatisticsResults[queryIndex];
            sample.queryIndex = queryIndex;
            sample.statisticMask = payload->pipelineStatisticsMask;
            sample.availability = rhi::PipelineStatisticsAvailability::DiagnosticFailure;
            sample.completedFrameSerial = frameSerial;
            sample.diagnostic = "RHI-METAL-PIPELINE-STATISTICS-COMMAND-BUFFER-FAILED";
            payload->pipelineStatisticsStates[queryIndex] =
                MetalRhiNativeQueryState::DiagnosticFailure;
            {
                std::lock_guard completedLock(pipelineStatisticsMutex());
                completedPipelineStatistics() = sample;
                hasCompletedPipelineStatistics().store(true, std::memory_order_release);
            }
            pendingPipelineStatisticsQueries().fetch_sub(1u, std::memory_order_acq_rel);
        }
        return;
    }
    if (!@available(macOS 10.15, iOS 14.0, *)) {
        std::lock_guard lock(payload->pipelineStatisticsMutex);
        if (queryIndex < payload->pipelineStatisticsStates.size()
            && queryIndex < payload->pipelineStatisticsResults.size()
            && payload->pipelineStatisticsStates[queryIndex] == MetalRhiNativeQueryState::Pending) {
            auto& sample = payload->pipelineStatisticsResults[queryIndex];
            sample.queryIndex = queryIndex;
            sample.statisticMask = payload->pipelineStatisticsMask;
            sample.availability = rhi::PipelineStatisticsAvailability::DiagnosticFailure;
            sample.completedFrameSerial = frameSerial;
            sample.diagnostic = "RHI-METAL-PIPELINE-STATISTICS-QUERY-API-UNAVAILABLE";
            payload->pipelineStatisticsStates[queryIndex] =
                MetalRhiNativeQueryState::DiagnosticFailure;
            {
                std::lock_guard completedLock(pipelineStatisticsMutex());
                completedPipelineStatistics() = sample;
                hasCompletedPipelineStatistics().store(true, std::memory_order_release);
            }
            pendingPipelineStatisticsQueries().fetch_sub(1u, std::memory_order_acq_rel);
        }
        return;
    }
    NSData* data = [sampleBuffer resolveCounterRange:NSMakeRange(0, payload->count)];
    const bool validData =
        data != nil && payload->count <= data.length / sizeof(MTLCounterResultStatistic);
    const auto* values =
        validData ? reinterpret_cast<const MTLCounterResultStatistic*>(data.bytes) : nullptr;
    if (queryIndex >= payload->count || queryIndex >= payload->pipelineStatisticsStates.size()
        || queryIndex >= payload->pipelineStatisticsResults.size()) {
        return;
    }
    {
        rhi::GpuPipelineStatisticsSample sample;
        {
            std::lock_guard lock(payload->pipelineStatisticsMutex);
            if (payload->pipelineStatisticsStates[queryIndex]
                != MetalRhiNativeQueryState::Pending) {
                return;
            }
            sample.queryIndex = queryIndex;
            sample.statisticMask = payload->pipelineStatisticsMask;
            sample.completedFrameSerial = frameSerial;
            if (!validData) {
                sample.availability = rhi::PipelineStatisticsAvailability::DiagnosticFailure;
                sample.diagnostic = "RHI-METAL-PIPELINE-STATISTICS-QUERY-READBACK-FAILED";
                payload->pipelineStatisticsStates[queryIndex] =
                    MetalRhiNativeQueryState::DiagnosticFailure;
            } else {
                sample.availability = rhi::PipelineStatisticsAvailability::Available;
                sample.diagnostic = "Metal GPU pipeline-statistics query completed";
                const auto& value = values[queryIndex];
                const auto append = [&](rhi::PipelineStatistic statistic, std::uint64_t count) {
                    if ((payload->pipelineStatisticsMask & rhi::pipelineStatisticBit(statistic))
                            == 0u
                        || sample.valueCount >= rhi::GpuPipelineStatisticsSample::kMaxStatistics) {
                        return;
                    }
                    sample.values[sample.valueCount++] = {statistic, count};
                };
                append(rhi::PipelineStatistic::VertexInvocations, value.vertexInvocations);
                append(rhi::PipelineStatistic::PostTessellationVertexInvocations,
                    value.postTessellationVertexInvocations);
                append(rhi::PipelineStatistic::ClipperInvocations, value.clipperInvocations);
                append(rhi::PipelineStatistic::ClipperPrimitivesOut, value.clipperPrimitivesOut);
                append(rhi::PipelineStatistic::FragmentInvocations, value.fragmentInvocations);
                append(rhi::PipelineStatistic::FragmentsPassed, value.fragmentsPassed);
                append(rhi::PipelineStatistic::ComputeKernelInvocations,
                    value.computeKernelInvocations);
                payload->pipelineStatisticsStates[queryIndex] = MetalRhiNativeQueryState::Available;
            }
            payload->pipelineStatisticsResults[queryIndex] = sample;
        }
        {
            std::lock_guard lock(pipelineStatisticsMutex());
            completedPipelineStatistics() = sample;
            hasCompletedPipelineStatistics().store(true, std::memory_order_release);
        }
        pendingPipelineStatisticsQueries().fetch_sub(1u, std::memory_order_acq_rel);
    }
}

void resolveMetalRhiNativeQueryPool(const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& payload,
    std::uint64_t frameSerial,
    const core::pmr::Vector<MetalRhiRecordedTimestampQuery>& queries,
    bool commandBufferSucceeded)
{
    if (!payload || !payload->nativeRuntimeBacked || !payload->nativeCounterSampleBuffer) {
        return;
    }
    if (payload->pipelineStatistics || queries.empty())
        return;
    if (!commandBufferSucceeded) {
        std::lock_guard lock(payload->pipelineStatisticsMutex);
        for (const auto& query : queries) {
            if (query.pool == payload && query.queryIndex < payload->timestampStates.size()
                && payload->timestampStates[query.queryIndex]
                    == MetalRhiNativeQueryState::Pending) {
                payload->timestampStates[query.queryIndex] =
                    MetalRhiNativeQueryState::DiagnosticFailure;
            }
        }
        std::lock_guard timingLock(timingMutex());
        auto& sample = completedTiming();
        sample.completedFrameSerial = frameSerial;
        sample.availability = rhi::GpuTimingAvailability::DiagnosticFailure;
        sample.diagnostic = "RHI-METAL-TIMESTAMP-COMMAND-BUFFER-FAILED";
        hasCompletedTiming().store(true, std::memory_order_release);
        return;
    }
    if (!@available(macOS 10.15, iOS 14.0, *)) {
        std::lock_guard lock(payload->pipelineStatisticsMutex);
        for (const auto& query : queries) {
            if (query.pool == payload && query.queryIndex < payload->timestampStates.size()
                && payload->timestampStates[query.queryIndex]
                    == MetalRhiNativeQueryState::Pending) {
                payload->timestampStates[query.queryIndex] =
                    MetalRhiNativeQueryState::DiagnosticFailure;
            }
        }
        std::lock_guard timingLock(timingMutex());
        auto& sample = completedTiming();
        sample.completedFrameSerial = frameSerial;
        sample.availability = rhi::GpuTimingAvailability::DiagnosticFailure;
        sample.diagnostic = "RHI-METAL-TIMESTAMP-QUERY-API-UNAVAILABLE";
        hasCompletedTiming().store(true, std::memory_order_release);
        return;
    }
    id<MTLCounterSampleBuffer> sampleBuffer =
        reinterpret_cast<id<MTLCounterSampleBuffer>>(payload->nativeCounterSampleBuffer.get());
    NSData* data = [sampleBuffer resolveCounterRange:NSMakeRange(0, payload->count)];
    if (data == nil || payload->count > data.length / sizeof(MTLCounterResultTimestamp)) {
        std::lock_guard lock(payload->pipelineStatisticsMutex);
        for (const auto& query : queries) {
            if (query.pool == payload && query.queryIndex < payload->timestampStates.size()
                && payload->timestampStates[query.queryIndex]
                    == MetalRhiNativeQueryState::Pending) {
                payload->timestampStates[query.queryIndex] =
                    MetalRhiNativeQueryState::DiagnosticFailure;
            }
        }
        std::lock_guard timingLock(timingMutex());
        auto& sample = completedTiming();
        sample.availability = rhi::GpuTimingAvailability::DiagnosticFailure;
        sample.diagnostic = "RHI-METAL-TIMESTAMP-QUERY-READBACK-FAILED";
        hasCompletedTiming().store(true, std::memory_order_release);
        return;
    }
    const auto* values = reinterpret_cast<const MTLCounterResultTimestamp*>(data.bytes);
    {
        std::lock_guard lock(payload->pipelineStatisticsMutex);
        for (const auto& query : queries) {
            if (query.pool == payload && query.queryIndex < payload->timestampStates.size()
                && payload->timestampStates[query.queryIndex]
                    == MetalRhiNativeQueryState::Pending) {
                payload->timestampStates[query.queryIndex] = MetalRhiNativeQueryState::Available;
            }
        }
    }
    if (payload->scopeIds.empty())
        return;
    {
        std::lock_guard lock(payload->pipelineStatisticsMutex);
        const auto expectedQueryCount = payload->scopeIds.size() * 2u;
        if (expectedQueryCount > payload->timestampStates.size())
            return;
        for (std::size_t index = 0; index < expectedQueryCount; ++index) {
            if (payload->timestampStates[index] != MetalRhiNativeQueryState::Available) {
                return;
            }
        }
    }
    rhi::GpuTimingSample sample;
    sample.availability = rhi::GpuTimingAvailability::Available;
    sample.completedFrameSerial = frameSerial;
    sample.scopeCount = static_cast<std::uint32_t>(
        std::min<std::size_t>(payload->scopeIds.size(), rhi::GpuTimingSample::kMaxScopes));
    sample.diagnostic = "Metal GPU timestamp query completed";
    for (std::uint32_t index = 0; index < sample.scopeCount; ++index) {
        const auto begin = values[index * 2u].timestamp;
        const auto end = values[index * 2u + 1u].timestamp;
        sample.scopes[index].scopeId = payload->scopeIds[index];
        sample.scopes[index].elapsedMs =
            end >= begin ? static_cast<float>(end - begin) / 1000000.0f : 0.0f;
    }
    std::lock_guard lock(timingMutex());
    completedTiming() = sample;
    hasCompletedTiming().store(true, std::memory_order_release);
}

rhi::GpuTimingSample latestMetalRhiNativeTimingSample()
{
    std::lock_guard lock(timingMutex());
    auto sample = completedTiming();
    if (!hasCompletedTiming().load(std::memory_order_acquire)
        && metalRhiNativeTimestampQueriesAvailable()) {
        sample.availability = rhi::GpuTimingAvailability::Pending;
        sample.diagnostic = "Metal GPU timestamp query data is pending completion";
    }
    return sample;
}

rhi::GpuPipelineStatisticsSample latestMetalRhiNativePipelineStatistics()
{
    std::lock_guard lock(pipelineStatisticsMutex());
    auto sample = completedPipelineStatistics();
    if (!hasCompletedPipelineStatistics().load(std::memory_order_acquire)
        && pendingPipelineStatisticsQueries().load(std::memory_order_acquire) != 0u
        && metalRhiNativePipelineStatisticsQueriesAvailable()) {
        sample.availability = rhi::PipelineStatisticsAvailability::Pending;
        sample.diagnostic = "Metal GPU pipeline-statistics query data is pending completion";
    } else if (!hasCompletedPipelineStatistics().load(std::memory_order_acquire)) {
        sample.availability = rhi::PipelineStatisticsAvailability::NotRecorded;
        sample.diagnostic = "Metal pipeline-statistics query has not been recorded";
    }
    return sample;
}

std::uint64_t allocateMetalRhiNativeTimingFrameSerial()
{
    return timingSerial().fetch_add(1u, std::memory_order_acq_rel) + 1u;
}

bool metalRhiNativeTimestampQueriesAvailable()
{
    if (!@available(macOS 10.15, iOS 14.0, *))
        return false;
    id<MTLDevice> device = defaultMetalDevice();
    id<MTLCounterSet> counterSet = findTimestampCounterSet(device);
    if (device == nil || counterSet == nil || !supportsEncoderCounterSampling(device))
        return false;
    MTLCounterSampleBufferDescriptor* descriptor = [[MTLCounterSampleBufferDescriptor alloc] init];
    descriptor.counterSet = counterSet;
    descriptor.sampleCount = 1u;
    descriptor.storageMode = MTLStorageModeShared;
    NSError* error = nil;
    id<MTLCounterSampleBuffer> sampleBuffer =
        [device newCounterSampleBufferWithDescriptor:descriptor error:&error];
    [descriptor release];
    const bool available = sampleBuffer != nil;
    [sampleBuffer release];
    return available;
}

bool metalRhiNativePipelineStatisticsQueriesAvailable()
{
    if (!@available(macOS 10.15, iOS 14.0, *))
        return false;
    id<MTLDevice> device = defaultMetalDevice();
    id<MTLCounterSet> counterSet = findPipelineStatisticsCounterSet(device);
    if (device == nil || counterSet == nil || !supportsEncoderCounterSampling(device))
        return false;
    MTLCounterSampleBufferDescriptor* descriptor = [[MTLCounterSampleBufferDescriptor alloc] init];
    descriptor.counterSet = counterSet;
    descriptor.sampleCount = 1u;
    descriptor.storageMode = MTLStorageModeShared;
    NSError* error = nil;
    id<MTLCounterSampleBuffer> sampleBuffer =
        [device newCounterSampleBufferWithDescriptor:descriptor error:&error];
    [descriptor release];
    const bool available = sampleBuffer != nil;
    [sampleBuffer release];
    return available;
}

} // namespace arp::rhi_metal
