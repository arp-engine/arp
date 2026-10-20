#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_queries.h"
#include "rhi/backends/metal/metal_native_runtime.h"

#include <Metal/Metal.h>
#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Metal query ownership tests require assertions
#endif

namespace {
void verifyMetadataTransfer()
{
    using namespace arp;
    using namespace arp::rhi_metal;
    // CPU metadata only. These are not native query pools and must never be
    // submitted or used to advertise native query completion support.
    auto timestampPool = core::makeShared<MetalRhiNativeQueryPoolPayload>();
    auto statisticsPool = core::makeShared<MetalRhiNativeQueryPoolPayload>();
    std::weak_ptr<MetalRhiNativeQueryPoolPayload> timestampWeak = timestampPool,
                                                  statisticsWeak = statisticsPool;
    core::pmr::Vector<std::shared_ptr<MetalRhiNativeQueryPoolPayload>> pools{
        timestampPool, statisticsPool};
    core::pmr::Vector<MetalRhiRecordedTimestampQuery> timestamps{
        {timestampPool, 0u}, {timestampPool, 1u}};
    core::pmr::Vector<MetalRhiRecordedPipelineStatisticsQuery> statistics{{statisticsPool, 0u}};
    auto preparedOwner = core::makeShared<MetalRhiSubmittedQueryRetention>();
    auto foreign =
        core::pmr::Vector<MetalRhiRecordedTimestampQuery>(std::pmr::new_delete_resource());
    assert(!preparedOwner->canTakeRecorded(pools, foreign, statistics));
    assert(!preparedOwner->takeRecorded(pools, foreign, statistics));
    assert(pools.size() == 2u && timestamps.size() == 2u && statistics.size() == 1u);
    const auto* poolData = pools.data();
    const auto* timestampData = timestamps.data();
    const auto* statisticsData = statistics.data();
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    assert(preparedOwner->canTakeRecorded(pools, timestamps, statistics));
    assert(preparedOwner->takeRecorded(pools, timestamps, statistics));
    assert(preparedOwner->pools().data() == poolData
        && preparedOwner->timestamps().data() == timestampData
        && preparedOwner->statistics().data() == statisticsData);
    assert(pools.empty() && timestamps.empty() && statistics.empty());
    assert(!preparedOwner->takeRecorded(pools, timestamps, statistics));
    timestampPool.reset();
    statisticsPool.reset();
    const auto handlerAlias = preparedOwner;
    preparedOwner.reset();
    assert(!timestampWeak.expired() && !statisticsWeak.expired());
    handlerAlias->releaseAfterResolution();
    assert(timestampWeak.expired() && statisticsWeak.expired());
    assert(handlerAlias->pools().empty() && handlerAlias->timestamps().empty()
        && handlerAlias->statistics().empty());
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    std::puts(
        "queryMetadata: nonempty vectors moved without allocation; exact owners retired before handler destruction");
}
} // namespace

int main()
{
    @autoreleasepool {
        using namespace arp::rhi_metal;
        verifyMetadataTransfer();
        id<MTLDevice> device = reinterpret_cast<id<MTLDevice>>(metalRhiNativeDeviceHandle());
        assert(device);
        bool draw = false, dispatch = false, stage = false;
        if (@available(macOS 11.0, iOS 14.0, *)) {
            draw = [device supportsCounterSampling:MTLCounterSamplingPointAtDrawBoundary];
            dispatch = [device supportsCounterSampling:MTLCounterSamplingPointAtDispatchBoundary];
            stage = [device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary];
        }
        const bool timestamps = metalRhiNativeTimestampQueriesAvailable();
        const bool statistics = metalRhiNativePipelineStatisticsQueriesAvailable();
        assert(!timestamps || (draw && dispatch));
        assert(!statistics || (draw && dispatch));
        arp::rhi::QueryPoolDesc desc;
        desc.type = arp::rhi::QueryType::Timestamp;
        desc.count = 2u;
        auto pool = createMetalRhiNativeQueryPoolPayload(desc);
        assert(pool);
        assert(pool->nativeRuntimeBacked == timestamps);
        std::pmr::string diagnostics;
        assert(!recordMetalRhiNativeTimestamp({}, pool, 0u, diagnostics));
        assert(pool->timestampStates[0] == MetalRhiNativeQueryState::NotRecorded);
        // Compare native probe facts with the public device gate. Unsupported
        // encoder sampling must reject before creating an executable pool.
        MetalRhiDeviceDesc deviceDesc;
        deviceDesc.capabilityContract =
            arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Metal);
        deviceDesc.capabilityContract.queriedFromNativeDevice = true;
        deviceDesc.capabilityContract.queriedExtendedFeatures = true;
        deviceDesc.memoryBudgetBytes = 1024u * 1024u;
        deviceDesc.uploadStagingPageSizeBytes = 4096u;
        deviceDesc.uploadStagingMaxPageCount = 1u;
        auto publicDevice = createMetalRhiDevice(deviceDesc);
        assert(publicDevice);
        assert(arp::rhi::validateQueryPoolDesc(desc).success);
        if (!timestamps) {
            assert(!publicDevice->capabilities().features.timestampQueries);
            assert(!publicDevice->createQueryPool(desc));
        }
        desc.type = arp::rhi::QueryType::PipelineStatistics;
        desc.pipelineStatisticsMask = arp::rhi::kAllPipelineStatistics;
        assert(arp::rhi::validateQueryPoolDesc(desc).success);
        if (!statistics) {
            assert(!publicDevice->capabilities().features.pipelineStatisticsQueries);
            assert(!publicDevice->createQueryPool(desc));
        }
        std::printf("draw=%d dispatch=%d stage=%d timestamp=%d statistics=%d\n",
            draw,
            dispatch,
            stage,
            timestamps,
            statistics);
    }
    return 0;
}
