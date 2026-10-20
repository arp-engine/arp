// Contract tests for the shared staging upload pool helpers in
// rhi/frontend/memory_staging_upload_pool.h.
//
// These helpers consolidate the boilerplate that was previously duplicated
// verbatim across vulkan_staging_upload_pool.cpp and
// metal_staging_upload_pool.{cpp,mm}. The tests pin the semantics of each
// helper so that future backend changes cannot silently drift the shared
// behavior. WebGPU does not use these helpers (different ring model), so its
// name is intentionally absent here.
//
// Status: Contract Complete.

#include "core/cpu_allocator.h"
#include "core/page_slice_allocator.h"
#include "rhi/frontend/memory_staging_upload_pool.h"

#include <cassert>
#include <cstdint>

namespace {

// A minimal backend stats struct shaped like VulkanRhiStagingUploadPoolStats /
// MetalRhiStagingUploadPoolStats (the common 7 fields). Used to verify
// fillStagingCommonStats maps every field without relying on a real backend.
struct FakeBackendStats {
    std::uint32_t allocatedPageCount = 0;
    std::uint32_t liveSliceCount = 0;
    std::uint32_t retiredSliceCount = 0;
    std::uint32_t failedAllocationCount = 0;
    std::uint64_t allocatedBytes = 0;
    std::uint64_t peakAllocatedBytes = 0;
    bool boundedCapacity = false;
};

void assertNormalizeStagingAlignmentClampsZeroToOne()
{
    using arp::rhi::normalizeStagingAlignment;
    assert(normalizeStagingAlignment(0) == 1);
    assert(normalizeStagingAlignment(1) == 1);
    assert(normalizeStagingAlignment(256) == 256);
    assert(normalizeStagingAlignment(4096) == 4096);
}

void assertResolveStagingAlignmentPrefersRequestOverDesc()
{
    using arp::rhi::resolveStagingAlignment;
    // Non-zero request wins over any desc.
    assert(resolveStagingAlignment(512, 256) == 512);
    assert(resolveStagingAlignment(512, 0) == 512);
    // Zero request falls back to desc (including a normalized desc of 1).
    assert(resolveStagingAlignment(0, 256) == 256);
    assert(resolveStagingAlignment(0, 1) == 1);
    // Both zero: desc is expected to have been normalized upstream, so the
    // helper returns desc verbatim (0 here); the contract is that the caller
    // normalizes desc at construction, not per-request.
    assert(resolveStagingAlignment(0, 0) == 0);
}

void assertStagingCreatePageInvariantOnlyAppendsAtEndWithNonZeroCapacity()
{
    using arp::rhi::stagingCreatePageInvariant;
    // pageIndex must equal current pageCount (append-only) and capacity > 0.
    assert(stagingCreatePageInvariant(0, 0, 4096));
    assert(stagingCreatePageInvariant(1, 1, 4096));
    assert(stagingCreatePageInvariant(2, 2, 1));
    // Out-of-order index rejected.
    assert(!stagingCreatePageInvariant(0, 1, 4096));
    assert(!stagingCreatePageInvariant(2, 1, 4096));
    // Zero capacity rejected even for a valid append index.
    assert(!stagingCreatePageInvariant(0, 0, 0));
    assert(!stagingCreatePageInvariant(1, 1, 0));
}

void assertStagingMappedDataAtAdvancesByOffsetAndNullsWhenUnmapped()
{
    using arp::rhi::stagingMappedDataAt;
    unsigned char buffer[64] = {};
    void* base = buffer;
    assert(stagingMappedDataAt(base, 0) == base);
    assert(stagingMappedDataAt(base, 16) == static_cast<void*>(buffer + 16));
    assert(stagingMappedDataAt(base, 63) == static_cast<void*>(buffer + 63));
    // Unmapped page (nullptr base) always yields nullptr regardless of offset.
    assert(stagingMappedDataAt(nullptr, 0) == nullptr);
    assert(stagingMappedDataAt(nullptr, 1024) == nullptr);
}

void assertFillStagingCommonStatsMapsAllCommonFields()
{
    arp::core::FrameRetiredPageSliceAllocatorStats coreStats;
    coreStats.allocatedPageCount = 3;
    coreStats.liveSliceCount = 5;
    coreStats.retiredSliceCount = 7;
    coreStats.failedAllocationCount = 2;
    coreStats.allocatedBytes = 4096;
    coreStats.peakLiveBytes = 8192;
    coreStats.boundedCapacity = true;

    FakeBackendStats stats;
    arp::rhi::fillStagingCommonStats(coreStats, stats);

    assert(stats.allocatedPageCount == 3);
    assert(stats.liveSliceCount == 5);
    assert(stats.retiredSliceCount == 7);
    assert(stats.failedAllocationCount == 2);
    assert(stats.allocatedBytes == 4096);
    // peakAllocatedBytes = max(allocatedBytes, peakLiveBytes).
    assert(stats.peakAllocatedBytes == 8192);
    assert(stats.boundedCapacity);
}

void assertFillStagingCommonStatsPeakUsesAllocatedWhenLarger()
{
    // When allocatedBytes exceeds peakLiveBytes (e.g. after retirement the
    // live peak drops but allocated accounting has not), the helper must keep
    // the larger of the two so the reported peak never regresses below the
    // current allocation.
    arp::core::FrameRetiredPageSliceAllocatorStats coreStats;
    coreStats.allocatedBytes = 12000;
    coreStats.peakLiveBytes = 4000;

    FakeBackendStats stats;
    arp::rhi::fillStagingCommonStats(coreStats, stats);
    assert(stats.peakAllocatedBytes == 12000);
}

void assertHelpersCompileWithoutNativeHeaders()
{
    // The common helpers must be usable from any backend TU without pulling in
    // Vulkan/Metal/WebGPU native headers. This assertion is a compile-time
    // guarantee: if staging_upload_pool_common.h transitively included a native
    // header, this TU would not build on a non-Apple / non-Vulkan / non-WebGPU
    // host. The fake stats struct above already exercises the template; this
    // function exists to make the layering intent explicit in the test name.
    FakeBackendStats stats;
    arp::core::FrameRetiredPageSliceAllocatorStats coreStats;
    arp::rhi::fillStagingCommonStats(coreStats, stats);
    assert(stats.allocatedPageCount == 0);
    assert(stats.peakAllocatedBytes == 0);
    // core::FrameRetiredPageSliceAllocatorStats defaults boundedCapacity to true
    // (a zero-page allocator is still considered bounded at zero capacity).
    assert(stats.boundedCapacity);
}

} // namespace

int main()
{
    assertNormalizeStagingAlignmentClampsZeroToOne();
    assertResolveStagingAlignmentPrefersRequestOverDesc();
    assertStagingCreatePageInvariantOnlyAppendsAtEndWithNonZeroCapacity();
    assertStagingMappedDataAtAdvancesByOffsetAndNullsWhenUnmapped();
    assertFillStagingCommonStatsMapsAllCommonFields();
    assertFillStagingCommonStatsPeakUsesAllocatedWhenLarger();
    assertHelpersCompileWithoutNativeHeaders();
    return 0;
}
