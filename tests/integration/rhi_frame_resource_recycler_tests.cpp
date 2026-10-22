#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/frame_resources.h"

#include <cassert>
#include <string>

int main()
{
    using namespace arp::rhi;

    FrameResourceRecycler recycler({2, 3, 3});
    assert(recycler.logicalFrameSlots() == 2u);
    assert(recycler.stats().backendFallbacksHiddenFromUpperLayer);
    assert(!recycler.stats().nativeHandleExposedToUpperLayer);

    auto deferred = recycler.deferDestroy(
        {GpuResourceId{10}, 256, FrameResourceKind::Buffer, FrameResourceScope::Device}, 4);
    assert(deferred.success);
    assert(deferred.frameSlot == 0u);
    assert(recycler.stats().pendingDestroyCount == 1u);
    assert(recycler.stats().pendingDestroyBytes == 256u);

    auto earlyRetire = recycler.retireCompletedFrames(5);
    assert(earlyRetire.success);
    assert(earlyRetire.releasedCount == 0u);
    assert(recycler.stats().pendingDestroyCount == 1u);

    auto safeRetire = recycler.retireCompletedFrames(6);
    assert(safeRetire.success);
    assert(safeRetire.releasedCount == 1u);
    assert(safeRetire.releasedBytes == 256u);
    assert(recycler.stats().pendingDestroyCount == 0u);
    assert(recycler.stats().releasedBytes == 256u);

    assert(recycler
            .deferDestroy(
                {GpuResourceId{20}, 64, FrameResourceKind::Texture, FrameResourceScope::Surface}, 7)
            .success);
    assert(recycler
            .deferDestroy({GpuResourceId{21},
                              32,
                              FrameResourceKind::DescriptorSet,
                              FrameResourceScope::Device},
                7)
            .success);
    auto surfaceRelease = recycler.releaseSurfaceScopedResources();
    assert(surfaceRelease.success);
    assert(surfaceRelease.releasedCount == 1u);
    assert(surfaceRelease.releasedBytes == 64u);
    assert(recycler.stats().pendingDestroyCount == 1u);

    auto transientFrame9 = recycler.trackTransient(
        {GpuResourceId{30}, 128, FrameResourceKind::Texture, FrameResourceScope::Transient}, 9);
    assert(transientFrame9.success);
    assert(transientFrame9.frameSlot == 1u);

    auto transientFrame10 = recycler.trackTransient(
        {GpuResourceId{31}, 256, FrameResourceKind::UploadBlock, FrameResourceScope::Transient},
        10);
    assert(transientFrame10.success);
    assert(transientFrame10.frameSlot == 0u);
    assert(recycler.stats().transientResourceCount == 2u);
    assert(recycler.stats().transientBytes == 384u);

    auto frameSlotRelease = recycler.releaseTransientForFrameSlot(1);
    assert(frameSlotRelease.success);
    assert(frameSlotRelease.releasedCount == 1u);
    assert(frameSlotRelease.releasedBytes == 128u);
    assert(recycler.stats().transientResourceCount == 1u);

    auto memoryWarningRelease = recycler.releaseAllTransientResources();
    assert(memoryWarningRelease.success);
    assert(memoryWarningRelease.releasedCount == 1u);
    assert(memoryWarningRelease.releasedBytes == 256u);
    assert(recycler.stats().transientResourceCount == 0u);

    assert(recycler
            .deferDestroy({GpuResourceId{40},
                              16,
                              FrameResourceKind::CommandBuffer,
                              FrameResourceScope::Device},
                11)
            .success);
    assert(recycler
            .deferDestroy({GpuResourceId{41},
                              16,
                              FrameResourceKind::CommandBuffer,
                              FrameResourceScope::Device},
                11)
            .success);
    auto exhausted = recycler.deferDestroy(
        {GpuResourceId{42}, 16, FrameResourceKind::CommandBuffer, FrameResourceScope::Device}, 11);
    assert(!exhausted.success);
    assert(exhausted.diagnostics.find("deferred destroy capacity") != std::pmr::string::npos);

    FrameResourceRecord leakedNativeHandle{
        GpuResourceId{50},
        16,
        FrameResourceKind::Texture,
        FrameResourceScope::Device,
        true,
    };
    auto nativeLeak = recycler.deferDestroy(leakedNativeHandle, 12);
    assert(!nativeLeak.success);
    assert(nativeLeak.diagnostics.find("native handle") != std::pmr::string::npos);
    assert(!recycler.stats().nativeHandleExposedToUpperLayer);

    FrameResourceRecycler deferredAccounting({2, 2, 1});
    auto maxDeferredBytes = deferredAccounting.deferDestroy({GpuResourceId{70},
                                                                0xffffffffffffffffull,
                                                                FrameResourceKind::Buffer,
                                                                FrameResourceScope::Device},
        0);
    assert(maxDeferredBytes.success);
    assert(deferredAccounting.stats().pendingDestroyBytes == 0xffffffffffffffffull);
    auto overflowingDeferredBytes = deferredAccounting.deferDestroy(
        {GpuResourceId{71}, 1, FrameResourceKind::Buffer, FrameResourceScope::Device}, 0);
    assert(!overflowingDeferredBytes.success);
    assert(overflowingDeferredBytes.diagnostics.find("byte") != std::pmr::string::npos);
    assert(deferredAccounting.stats().pendingDestroyCount == 1u);
    assert(deferredAccounting.stats().pendingDestroyBytes == 0xffffffffffffffffull);

    FrameResourceRecycler transientAccounting({2, 1, 2});
    auto maxTransientBytes = transientAccounting.trackTransient({GpuResourceId{80},
                                                                    0xffffffffffffffffull,
                                                                    FrameResourceKind::Texture,
                                                                    FrameResourceScope::Transient},
        0);
    assert(maxTransientBytes.success);
    assert(transientAccounting.stats().transientBytes == 0xffffffffffffffffull);
    auto overflowingTransientBytes = transientAccounting.trackTransient(
        {GpuResourceId{81}, 1, FrameResourceKind::Texture, FrameResourceScope::Transient}, 0);
    assert(!overflowingTransientBytes.success);
    assert(overflowingTransientBytes.diagnostics.find("byte") != std::pmr::string::npos);
    assert(transientAccounting.stats().transientResourceCount == 1u);
    assert(transientAccounting.stats().transientBytes == 0xffffffffffffffffull);

    FrameResourceRecycler serialAccounting({2, 1, 1});
    auto overflowingRetireSerial = serialAccounting.deferDestroy(
        {GpuResourceId{90}, 16, FrameResourceKind::Buffer, FrameResourceScope::Device},
        0xffffffffffffffffull);
    assert(!overflowingRetireSerial.success);
    assert(overflowingRetireSerial.diagnostics.find("serial") != std::pmr::string::npos);
    assert(serialAccounting.stats().pendingDestroyCount == 0u);

    FrameResourceRecycler releaseAccounting({2, 1, 1});
    assert(releaseAccounting
            .trackTransient({GpuResourceId{100},
                                0xffffffffffffffffull,
                                FrameResourceKind::Texture,
                                FrameResourceScope::Transient},
                0)
            .success);
    auto maxRelease = releaseAccounting.releaseAllTransientResources();
    assert(maxRelease.success);
    assert(maxRelease.releasedBytes == 0xffffffffffffffffull);
    assert(releaseAccounting.stats().releasedBytes == 0xffffffffffffffffull);
    assert(releaseAccounting
            .trackTransient(
                {GpuResourceId{101}, 1, FrameResourceKind::Texture, FrameResourceScope::Transient},
                0)
            .success);
    auto overflowingAggregateRelease = releaseAccounting.releaseAllTransientResources();
    assert(!overflowingAggregateRelease.success);
    assert(overflowingAggregateRelease.diagnostics.find("released byte") != std::pmr::string::npos);
    assert(releaseAccounting.stats().releasedBytes == 0xffffffffffffffffull);
    assert(releaseAccounting.stats().transientResourceCount == 1u);

    FrameResourceRecycler surfaceReleaseAccounting({2, 1, 1});
    assert(surfaceReleaseAccounting
            .deferDestroy({GpuResourceId{110},
                              0xffffffffffffffffull,
                              FrameResourceKind::Texture,
                              FrameResourceScope::Surface},
                0)
            .success);
    assert(surfaceReleaseAccounting
            .trackTransient(
                {GpuResourceId{111}, 1, FrameResourceKind::Texture, FrameResourceScope::Surface}, 0)
            .success);
    auto overflowingSurfaceRelease = surfaceReleaseAccounting.releaseSurfaceScopedResources();
    assert(!overflowingSurfaceRelease.success);
    assert(overflowingSurfaceRelease.diagnostics.find("released byte") != std::pmr::string::npos);
    assert(surfaceReleaseAccounting.stats().pendingDestroyCount == 1u);
    assert(surfaceReleaseAccounting.stats().transientResourceCount == 1u);

    FrameResourceRecycler oversizedCapacity({
        2,
        kMaxDeferredFrameResourceDestroys + 1u,
        kMaxTransientFrameResources + 1u,
    });
    auto oversizedDeferred = oversizedCapacity.deferDestroy(
        {GpuResourceId{120}, 16, FrameResourceKind::Buffer, FrameResourceScope::Device}, 0);
    assert(!oversizedDeferred.success);
    assert(oversizedDeferred.diagnostics.find("capacity limit") != std::pmr::string::npos);
    auto oversizedTransient = oversizedCapacity.trackTransient(
        {GpuResourceId{121}, 16, FrameResourceKind::Texture, FrameResourceScope::Transient}, 0);
    assert(!oversizedTransient.success);
    assert(oversizedTransient.diagnostics.find("capacity limit") != std::pmr::string::npos);
    assert(oversizedCapacity.stats().pendingDestroyCount == 0u);
    assert(oversizedCapacity.stats().transientResourceCount == 0u);
    assert(oversizedCapacity.stats().pendingDestroyBytes == 0u);
    assert(oversizedCapacity.stats().transientBytes == 0u);

    FrameResourceRecycler invalidSlots({3, 1, 1});
    auto invalid = invalidSlots.deferDestroy(
        {GpuResourceId{60}, 16, FrameResourceKind::Buffer, FrameResourceScope::Device}, 0);
    assert(!invalid.success);
    assert(invalid.diagnostics.find("frame slot") != std::pmr::string::npos);

    return 0;
}
