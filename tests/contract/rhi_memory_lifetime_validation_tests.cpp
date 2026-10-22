#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/frame_resources.h"
#include "rhi/frontend/memory_lifetime_validation.h"

#include <cassert>

namespace {

arp::rhi::FrameResourceRecyclerStats makeRetiredRecyclerStats()
{
    arp::rhi::FrameResourceRecycler recycler({2, 4, 4});
    assert(recycler
            .deferDestroy(
                {
                    arp::rhi::GpuResourceId{1},
                    64,
                    arp::rhi::FrameResourceKind::Buffer,
                    arp::rhi::FrameResourceScope::Device,
                },
                4)
            .success);
    const auto retired = recycler.retireCompletedFrames(6);
    assert(retired.success);
    assert(retired.releasedCount == 1u);
    return recycler.stats();
}

arp::rhi::MemoryLifetimeValidationDesc makeCompleteDesc()
{
    arp::rhi::MemoryLifetimeValidationDesc desc;
    desc.frameResources = makeRetiredRecyclerStats();
    desc.resourceDestructionOrderValidated = true;
    desc.descriptorLifetimeValidated = true;
    desc.descriptorArenaResetInvalidatedOldAllocations = true;
    desc.swapchainResizeRecreateValidated = true;
    desc.surfaceResourcesReleasedOnResize = true;
    desc.transientAliasingValidated = true;
    desc.transientAliasesNonOverlapping = true;
    desc.uploadStagingValidated = true;
    desc.readbackStagingValidated = true;
    desc.stagingResourcesBackendOwned = true;
    desc.uploadStagingBounded = true;
    desc.uploadStagingNativeHandlesHidden = true;
    desc.noHotPathUnboundedAllocation = true;
    desc.noBlockingWait = true;
    return desc;
}

} // namespace

int main()
{
    const auto complete = arp::rhi::evaluateMemoryLifetimeValidation(makeCompleteDesc());
    assert(complete.success);
    assert(complete.resourceDestructionOrderValid);
    assert(complete.descriptorLifetimeValid);
    assert(complete.swapchainResizeRecreateValid);
    assert(complete.transientAliasingValid);
    assert(complete.uploadReadbackStagingValid);
    assert(complete.hotPathAllocationBounded);
    assert(!complete.nativeHandleExposedToUpperLayer);

    auto pendingDestroy = makeCompleteDesc();
    arp::rhi::FrameResourceRecycler pendingRecycler({2, 4, 4});
    assert(pendingRecycler
            .deferDestroy(
                {
                    arp::rhi::GpuResourceId{2},
                    128,
                    arp::rhi::FrameResourceKind::Texture,
                    arp::rhi::FrameResourceScope::Device,
                },
                10)
            .success);
    pendingDestroy.frameResources = pendingRecycler.stats();
    const auto pendingDestroyReport = arp::rhi::evaluateMemoryLifetimeValidation(pendingDestroy);
    assert(!pendingDestroyReport.success);
    assert(!pendingDestroyReport.resourceDestructionOrderValid);
    assert(pendingDestroyReport.diagnostics.find("destruction order") != std::pmr::string::npos);

    auto staleDescriptor = makeCompleteDesc();
    staleDescriptor.descriptorArenaResetInvalidatedOldAllocations = false;
    const auto staleDescriptorReport = arp::rhi::evaluateMemoryLifetimeValidation(staleDescriptor);
    assert(!staleDescriptorReport.success);
    assert(!staleDescriptorReport.descriptorLifetimeValid);
    assert(staleDescriptorReport.diagnostics.find("descriptor lifetime") != std::pmr::string::npos);

    auto unboundedHotPath = makeCompleteDesc();
    unboundedHotPath.noHotPathUnboundedAllocation = false;
    const auto unboundedHotPathReport =
        arp::rhi::evaluateMemoryLifetimeValidation(unboundedHotPath);
    assert(!unboundedHotPathReport.success);
    assert(!unboundedHotPathReport.hotPathAllocationBounded);
    assert(unboundedHotPathReport.diagnostics.find("hot path") != std::pmr::string::npos);

    auto nativeLeak = makeCompleteDesc();
    nativeLeak.nativeHandleExposedToUpperLayer = true;
    const auto nativeLeakReport = arp::rhi::evaluateMemoryLifetimeValidation(nativeLeak);
    assert(!nativeLeakReport.success);
    assert(nativeLeakReport.nativeHandleExposedToUpperLayer);
    assert(nativeLeakReport.diagnostics.find("native") != std::pmr::string::npos);

    return 0;
}
