#include "rhi/frontend/completion_page_slice_allocator.h"
#include "rhi/frontend/memory_upload_staging_budget.h"
#include "rhi/frontend/page_slice_allocator.h"

#include <array>
#include <atomic>
#include <cassert>
#include <limits>
#include <string_view>
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

void coldStorageContract()
{
    using namespace arp::rhi;
    using namespace arp::rhi::semantics;
    const UploadStagingColdStorageBudget budget{1u, true, {1u, 64u, 64u, 2u, 128u, 8u}, 2u, 65536u};
    using Contract = UploadStagingColdStorageContract;
    assert(!Contract::validate(budget));
    for (const auto backend : {Backend::Null,
             Backend::Vulkan,
             Backend::Metal,
             Backend::WebGPU,
             Backend::WebGL,
             static_cast<Backend>(255u)}) {
        const auto decision = Contract::lowering(backend, budget);
        assert(!decision.originalExecutionAvailable);
        assert(decision.coldPreparationImplemented == (backend == Backend::Vulkan));
        assert(decision.disposition
            == (backend == Backend::Null         ? Contract::Disposition::ValidatorOnly
                    : backend == Backend::Vulkan ? Contract::Disposition::Prototype
                                                 : Contract::Disposition::DiagnosticFailure));
        assert((decision.diagnostic == nullptr) == (backend == Backend::Vulkan));
    }
    auto bad = budget;
    bad.contractVersion = 0u;
    assert(std::string_view(Contract::validate(bad)) == "RHI-STAGING-COLD-STORAGE-VERSION");
    bad = budget;
    bad.prototypeOptIn = false;
    assert(std::string_view(Contract::validate(bad)) == "RHI-STAGING-COLD-STORAGE-OPT-IN");
    bad = budget;
    bad.maxLiveSlicesPerPage = 0u;
    assert(std::string_view(Contract::validate(bad)) == "RHI-STAGING-COLD-STORAGE-SLICE-CAPACITY");
    bad.maxLiveSlicesPerPage = std::numeric_limits<std::uint32_t>::max();
    assert(std::string_view(Contract::validate(bad)) == "RHI-STAGING-COLD-STORAGE-SLICE-CAPACITY");
    bad = budget;
    bad.maxCpuMetadataBytes = 0u;
    assert(std::string_view(Contract::validate(bad)) == "RHI-STAGING-COLD-STORAGE-CPU-CAPACITY");
    bad = budget;
    bad.capacity.totalCapacityBytes = 127u;
    assert(Contract::validate(bad));
    assert(!Contract::lowering(Backend::Vulkan, bad).coldPreparationImplemented);
}

void coldStorageCapacity()
{
    using namespace arp::core;
    auto quota = makeShared<CpuMemoryQuota>(65536u);
    std::uint64_t bytes = 0u;
    {
        CpuMemoryQuotaScope scope(quota);
        FrameRetiredPageSliceAllocator allocator({64u, 2u});
        std::uint32_t created = 0u;
        assert(!allocator.prepareCold(2u, [&](auto index, auto size) {
            assert(index == created && size == 64u);
            ++created;
            return true;
        }));
        assert(created == 2u && allocator.coldStoragePrepared()
            && allocator.stats().allocatedPageCount == 2u
            && allocator.stats().allocatedBytes == 128u);
        bytes = quota->liveBytes();
        assert(bytes);
        CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - bytes);
        assert(pressure.charged());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto allocations = defaultCpuAllocator().stats().allocationCount;
        const auto late = [&](auto, auto) {
            ++created;
            return false;
        };
        std::array<FrameRetiredPageSliceAllocation, 4u> slices;
        for (std::size_t index = 0u; index < slices.size(); ++index) {
            slices[index] = allocator.allocate({8u, 8u, index + 1u}, late);
            assert(slices[index].success && slices[index].pageIndex == index / 2u);
        }
        const auto full = allocator.allocate({8u, 8u, 5u}, late);
        assert(!full.success
            && std::string_view(full.diagnosticId) == "RHI-STAGING-COLD-SLICE-CAPACITY"
            && allocator.stats().liveSliceCount == 4u);
        assert(allocator.cancelAllocation(slices[1]));
        const auto reused = allocator.allocate({8u, 8u, 6u}, late);
        assert(reused.success && reused.pageIndex == 0u && reused.offsetBytes == 8u);
        assert(allocator.rollbackAllocation(reused) && allocator.retireAllocation(slices[0]));
        const auto whole = allocator.allocate({64u, 8u, 7u}, late);
        assert(whole.success && whole.pageIndex == 0u && whole.offsetBytes == 0u);
        const auto noBytes = allocator.allocate({64u, 8u, 8u}, late);
        assert(
            !noBytes.success && noBytes.diagnosticId == kFrameRetiredPageSliceDiagnosticPageCount);
        assert(allocator.cancelAllocation(whole) && allocator.retireAllocation(slices[2])
            && allocator.retireAllocation(slices[3]) && !allocator.stats().liveSliceCount);
        assert(std::string_view(allocator.prepareCold(2u, late))
            == "RHI-STAGING-COLD-PREPARATION-STATE");
        assert(created == 2u && allocations == defaultCpuAllocator().stats().allocationCount);
        std::pmr::set_default_resource(previous);
    }
    assert(!quota->liveBytes());
    for (const auto available : {bytes, bytes - 1u, std::uint64_t{0u}}) {
        CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - available);
        CpuMemoryQuotaScope scope(quota);
        FrameRetiredPageSliceAllocator allocator({64u, 2u});
        std::uint32_t created = 0u;
        const auto factory = [&](auto, auto) {
            ++created;
            return true;
        };
        const auto* diagnostic = allocator.prepareCold(2u, factory);
        assert((diagnostic == nullptr) == (available == bytes));
        if (available != bytes) {
            assert(!allocator.coldStoragePrepared() && !allocator.stats().allocatedPageCount);
            const auto before = created;
            assert(!allocator.allocate({8u, 8u, 1u}, factory).success && created == before);
        }
    }
    assert(!quota->liveBytes());
    for (const bool throws : {false, true}) {
        FrameRetiredPageSliceAllocator allocator({64u, 2u});
        std::uint32_t calls = 0u;
        assert(allocator.prepareCold(2u, [&](auto index, auto) {
            ++calls;
            if (!index)
                return true;
            if (throws)
                throw std::bad_alloc{};
            return false;
        }));
        assert(calls == 2u && !allocator.coldStoragePrepared()
            && !allocator.stats().allocatedPageCount);
        assert(!allocator
                   .allocate({8u, 8u, 1u},
                       [&](auto, auto) {
                           ++calls;
                           return true;
                       })
                   .success
            && calls == 2u);
    }
}

int main()
{
    exactCallbackReservations();
    coldStorageContract();
    coldStorageCapacity();
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
