#include "core/completion_page_slice_allocator.h"
#include "core/page_slice_allocator.h"

#include <atomic>
#include <cassert>
#include <thread>

void exactCallbackReservations()
{
    using namespace arp::core;
    CompletionPageSliceAllocator allocator({512u, 1u});
    const auto factory = [](std::uint32_t, std::uint64_t) {
        return true;
    };
    const auto first = allocator.allocate({256u, 1u, 1u}, factory);
    const auto second = allocator.allocate({256u, 1u, 1u}, factory);
    auto firstToken = allocator.find(first);
    auto secondToken = allocator.find(second);
    assert(firstToken && secondToken && first.frameSerial != second.frameSerial);
    std::atomic<bool> started{false};
    std::thread callback([&] {
        started.store(true, std::memory_order_release);
        assert(secondToken->complete());
    });
    while (!started.load(std::memory_order_acquire)) {
    }
    // Concurrent collection never mutates callback-owned data or infers a prefix.
    allocator.collect();
    callback.join();
    assert(allocator.stats().liveSliceCount == 1u);
    const auto tail = allocator.allocate({256u, 1u, 1u}, factory);
    assert(tail.success && tail.offsetBytes == 256u);
    assert(!secondToken->cancel() && !secondToken->complete());
    assert(!allocator.cancel(second));
    CompletionPageSliceAllocator foreign({512u, 1u});
    const auto foreignAllocation = foreign.allocate({256u, 1u, 1u}, factory);
    assert(foreignAllocation.success && !foreign.find(first) && !allocator.find(foreignAllocation));
    auto stale = tail;
    stale.frameSerial = second.frameSerial;
    assert(!allocator.find(stale));
    assert(firstToken->cancel());
    assert(allocator.cancel(tail));
    assert(allocator.stats().liveSliceCount == 0u);
    assert(allocator.stats().retiredSliceCount == 1u);
    std::shared_ptr<CompletionPageSliceReservation> survivor;
    {
        CompletionPageSliceAllocator temporary({1u, 1u});
        const auto a = temporary.allocate({1u, 1u, 0u}, factory);
        survivor = temporary.find(a);
    }
    assert(survivor->cancel());
}

int main()
{
    exactCallbackReservations();
    using arp::core::FrameRetiredPageSliceAllocator;
    using arp::core::FrameRetiredPageSliceAllocatorDesc;
    using arp::core::FrameRetiredPageSliceRequest;

    FrameRetiredPageSliceAllocator allocator({64, 2});
    std::uint32_t createdPages = 0;
    auto createPage = [&](std::uint32_t pageIndex, std::uint64_t capacityBytes) {
        assert(pageIndex == createdPages);
        assert(capacityBytes == 64);
        ++createdPages;
        return true;
    };

    auto first = allocator.allocate({8, 8, 1}, createPage);
    assert(first.success);
    assert(first.pageIndex == 0);
    assert(first.offsetBytes == 0);
    assert(first.sizeBytes == 8);
    assert(first.frameSerial == 1);

    auto second = allocator.allocate({8, 16, 2}, createPage);
    assert(second.success);
    assert(second.pageIndex == 0);
    assert(second.offsetBytes == 16);
    assert(second.sizeBytes == 8);

    auto third = allocator.allocate({48, 8, 3}, createPage);
    assert(third.success);
    assert(third.pageIndex == 1);
    assert(third.offsetBytes == 0);
    assert(createdPages == 2);

    auto tooLarge = allocator.allocate({65, 8, 4, "oversized-upload"}, createPage);
    assert(!tooLarge.success);
    assert(tooLarge.diagnostics != nullptr);
    assert(tooLarge.diagnosticId == arp::core::kFrameRetiredPageSliceDiagnosticRequestExceedsPage);
    assert(allocator.stats().lastRequestBytes == 65);
    assert(allocator.stats().lastPageSizeBytes == 64);
    assert(allocator.stats().lastAllocatedBytes == 128);
    assert(allocator.stats().lastRequestFrameSerial == 4);
    assert(allocator.stats().lastRequestAlignmentBytes == 8);
    assert(allocator.stats().lastAllocatedPageCount == 2);
    assert(allocator.stats().lastMaxPageCount == 2);
    assert(!allocator.stats().lastRequestSucceeded);

    auto exhausted = allocator.allocate({48, 8, 5}, createPage);
    assert(!exhausted.success);
    assert(allocator.stats().failedAllocationCount == 2);

    assert(allocator.stats().allocatedPageCount == 2);
    assert(allocator.stats().successfulAllocationCount == 3);
    assert(allocator.stats().liveSliceCount == 3);
    assert(allocator.stats().allocatedBytes == 128);
    assert(allocator.stats().peakLiveBytes == 64);

    const auto retired = allocator.retireCompletedFrames(2);
    assert(retired == 2);
    assert(allocator.stats().liveSliceCount == 1);
    assert(allocator.stats().retiredSliceCount == 2);

    auto afterRetire = allocator.allocate({32, 32, 6}, createPage);
    assert(afterRetire.success);
    assert(afterRetire.pageIndex == 0);
    assert(afterRetire.offsetBytes == 0);

    FrameRetiredPageSliceAllocator invalidAlignment({64, 1});
    auto invalid = invalidAlignment.allocate({8, 3, 1}, createPage);
    assert(!invalid.success);
    assert(invalid.diagnosticId == arp::core::kFrameRetiredPageSliceDiagnosticAlignmentInvalid);

    FrameRetiredPageSliceAllocator failingFactory({64, 1});
    auto failedPage = failingFactory.allocate(
        FrameRetiredPageSliceRequest{8, 8, 1}, [](std::uint32_t, std::uint64_t) {
            return false;
        });
    assert(!failedPage.success);
    assert(failedPage.diagnosticId == arp::core::kFrameRetiredPageSliceDiagnosticPageCreate);
    assert(failingFactory.stats().allocatedPageCount == 0);
    assert(failingFactory.stats().failedAllocationCount == 1);

    return 0;
}
