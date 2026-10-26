#include "core/memory/cpu_allocator.h"
#include "core/ownership/mutation_journal.h"
#include "rhi/frontend/command_recorded_state.h"
#include "rhi/frontend/memory_upload_staging_readiness.h"
#include "rhi/frontend/page_slice_allocator.h"
#include "rhi/frontend/transfer_recorded_staging_upload.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

namespace {

using arp::core::FrameRetiredPageSliceAllocation;
using arp::rhi::RecordedStagingUploadSlice;

class TestStagingPool final {
public:
    FrameRetiredPageSliceAllocation allocate(
        std::uint64_t sizeBytes = 8u, std::uint64_t frameSerial = 1u)
    {
        return allocator_.allocate(
            {sizeBytes, 8u, frameSerial}, [](std::uint32_t pageIndex, std::uint64_t capacityBytes) {
                return pageIndex == 0u && capacityBytes == 64u;
            });
    }

    bool cancel(const RecordedStagingUploadSlice& slice) noexcept
    {
        if (cancelCount_ < cancelOffsets_.size()) {
            cancelOffsets_[cancelCount_] = slice.offsetBytes;
        }
        ++cancelCount_;
        return allocator_.cancelAllocation({
            true,
            nullptr,
            slice.pageIndex,
            slice.offsetBytes,
            slice.sizeBytes,
            slice.frameSerial,
            nullptr,
        });
    }

    std::uint32_t retire(std::uint64_t completedSerial)
    {
        return allocator_.retireCompletedFrames(completedSerial);
    }

    bool retire(const RecordedStagingUploadSlice& slice) noexcept
    {
        return allocator_.retireAllocation({true,
            nullptr,
            slice.pageIndex,
            slice.offsetBytes,
            slice.sizeBytes,
            slice.frameSerial,
            nullptr});
    }

    const auto& stats() const { return allocator_.stats(); }

    std::uint32_t cancelCount() const { return cancelCount_; }

    std::uint64_t cancelOffset(std::size_t index) const { return cancelOffsets_[index]; }

private:
    arp::core::FrameRetiredPageSliceAllocator allocator_{{64u, 1u}};
    std::array<std::uint64_t, 8> cancelOffsets_{};
    std::uint32_t cancelCount_ = 0u;
};

RecordedStagingUploadSlice recorded(const FrameRetiredPageSliceAllocation& allocation)
{
    assert(allocation.success);
    return {
        allocation.pageIndex,
        allocation.offsetBytes,
        allocation.sizeBytes,
        allocation.frameSerial,
    };
}

class TestCommandBuffer final : public arp::rhi::RecordedStateCommandBuffer {
public:
    explicit TestCommandBuffer(std::shared_ptr<arp::core::MutationJournal> states)
        : RecordedStateCommandBuffer(std::move(states))
    {}

    TestCommandBuffer(arp::core::UniquePtr<arp::core::MutationJournal> states,
        arp::core::UniquePtr<arp::rhi::RecordedStagingUploadJournal> staging)
        : RecordedStateCommandBuffer(std::move(states), std::move(staging))
    {}

    std::uint32_t commandCount() const override { return 1u; }
};

class PlainCommandBuffer final : public arp::rhi::CommandBuffer {
public:
    std::uint32_t commandCount() const override { return 0u; }
};

void verifyExactCancellationDoesNotReuseHole()
{
    TestStagingPool pool;
    const auto first = pool.allocate();
    const auto second = pool.allocate();
    assert(first.offsetBytes == 0u && second.offsetBytes == 8u);
    assert(pool.cancel(recorded(first)));
    assert(pool.stats().liveSliceCount == 1u);
    assert(pool.stats().liveBytes == 8u);

    const auto third = pool.allocate();
    assert(third.success);
    assert(third.offsetBytes == 16u);
    assert(pool.cancel(recorded(third)));
    assert(pool.cancel(recorded(second)));
    assert(pool.stats().liveSliceCount == 0u);

    const auto afterTailReclaim = pool.allocate();
    assert(afterTailReclaim.success && afterTailReclaim.offsetBytes == 0u);
    assert(pool.cancel(recorded(afterTailReclaim)));
}

void verifyReverseRollbackAndRollbackLast()
{
    TestStagingPool pool;
    const auto first = pool.allocate();
    const auto second = pool.allocate();
    const auto third = pool.allocate();
    arp::rhi::RecordedStagingUploadJournal journal;
    journal.enlist(pool, recorded(first));
    journal.enlist(pool, recorded(second));
    journal.enlist(pool, recorded(third));
    assert(journal.size() == 3u);

    assert(journal.rollbackLast());
    assert(pool.cancelOffset(0u) == third.offsetBytes);
    assert(pool.stats().liveSliceCount == 2u);

    const auto before = arp::core::defaultCpuAllocator().stats();
    assert(journal.rollback());
    const auto after = arp::core::defaultCpuAllocator().stats();
    assert(before.allocationCount == after.allocationCount);
    assert(before.deallocationCount == after.deallocationCount);
    assert(pool.cancelCount() == 3u);
    assert(pool.cancelOffset(1u) == second.offsetBytes);
    assert(pool.cancelOffset(2u) == first.offsetBytes);
    assert(pool.stats().liveSliceCount == 0u);
    assert(!journal.rollbackLast());
    assert(journal.rollback());
}

void verifyCommitLeavesCompletionOwnedSliceLive()
{
    TestStagingPool pool;
    const auto slice = pool.allocate(8u, 4u);
    {
        arp::rhi::RecordedStagingUploadJournal journal;
        journal.enlist(pool, recorded(slice));
        journal.commit();
        assert(journal.empty());
    }
    assert(pool.stats().liveSliceCount == 1u);
    assert(pool.retire(3u) == 0u);
    assert(pool.retire(4u) == 1u);
    assert(pool.stats().liveSliceCount == 0u);
}

void verifyCommandBufferTransferRejectRetryAndAcceptance()
{
    TestStagingPool abandonedPool;
    const auto abandoned = abandonedPool.allocate(8u, 7u);
    {
        auto journal = arp::core::makeUnique<arp::rhi::RecordedStagingUploadJournal>();
        journal->enlist(abandonedPool, recorded(abandoned));
        TestCommandBuffer commands(
            arp::core::makeUnique<arp::core::MutationJournal>(), std::move(journal));
        assert(commands.commandCount() == 1u);
    }
    assert(abandonedPool.stats().liveSliceCount == 0u);

    TestStagingPool acceptedPool;
    const auto accepted = acceptedPool.allocate(8u, 9u);
    {
        auto journal = arp::core::makeUnique<arp::rhi::RecordedStagingUploadJournal>();
        journal->enlist(acceptedPool, recorded(accepted));
        TestCommandBuffer commands(
            arp::core::makeUnique<arp::core::MutationJournal>(), std::move(journal));

        // A rejected submit performs no acceptance action. The same finished
        // command buffer retains its provisional slice for a legal retry.
        assert(acceptedPool.stats().liveSliceCount == 1u);
        assert(arp::rhi::acceptRecordedCommandOwnership(commands));
    }
    // Destruction after acceptance, including accepted-error handling, cannot
    // restore resource state or cancel completion-owned staging.
    assert(acceptedPool.stats().liveSliceCount == 1u);
    assert(acceptedPool.retire(8u) == 0u);
    assert(acceptedPool.retire(9u) == 1u);

    PlainCommandBuffer plain;
    assert(!arp::rhi::acceptRecordedCommandOwnership(plain));
}

void verifyUploadStagingReadinessClassification()
{
    using namespace arp::rhi::semantics;

    const auto pageCount =
        classifyUploadStagingFailure(arp::core::kFrameRetiredPageSliceDiagnosticPageCount);
    assert(pageCount.retryAfterCompletion());
    assert(std::string_view(pageCount.diagnosticId)
        == arp::core::kFrameRetiredPageSliceDiagnosticPageCount);

    const auto liveCapacity =
        classifyUploadStagingFailure(kUploadStagingInFlightCapacityDiagnostic);
    assert(liveCapacity.retryAfterCompletion());

    const std::string_view terminalIds[] = {
        arp::core::kFrameRetiredPageSliceDiagnosticSizeZero,
        arp::core::kFrameRetiredPageSliceDiagnosticNoCapacity,
        arp::core::kFrameRetiredPageSliceDiagnosticRequestExceedsPage,
        arp::core::kFrameRetiredPageSliceDiagnosticAlignmentRange,
        arp::core::kFrameRetiredPageSliceDiagnosticAlignmentInvalid,
        arp::core::kFrameRetiredPageSliceDiagnosticPageCreate,
        arp::core::kFrameRetiredPageSliceDiagnosticOffsetInvalid,
        arp::core::kFrameRetiredPageSliceDiagnosticPageCapacity,
    };
    for (const auto id : terminalIds) {
        assert(!classifyUploadStagingFailure(id).retryAfterCompletion());
    }
    assert(!classifyUploadStagingFailure({}).retryAfterCompletion());
}

void verifyColdJournalCapacityAndOwnership()
{
    using namespace arp;
    using rhi::RecordedStagingUploadJournal;
    auto quota = core::makeShared<core::CpuMemoryQuota>(64u * 1024u);
    std::uint64_t bytes = 0u;
    {
        core::CpuMemoryQuotaScope scope(quota);
        RecordedStagingUploadJournal journal;
        assert(!journal.prepareBounded(2u));
        bytes = quota->liveBytes();
        assert(bytes);
    }
    assert(!quota->liveBytes());
    for (const auto available : {bytes, bytes - 1u, std::uint64_t{0u}}) {
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - available);
        assert(pressure.charged());
        core::CpuMemoryQuotaScope scope(quota);
        RecordedStagingUploadJournal journal;
        const auto* diagnostic = journal.prepareBounded(2u);
        assert((diagnostic == nullptr) == (available == bytes));
        assert(journal.empty());
        assert(std::string_view(journal.prepareBounded(0u))
            == "RHI-STAGING-JOURNAL-PREPARATION-STATE");
        if (available != bytes) {
            assert(std::string_view(diagnostic) == "RHI-STAGING-JOURNAL-COLD-ALLOCATION"
                && journal.preflightEnlist() == diagnostic
                && quota->liveBytes() == quota->limitBytes() - available);
        }
    }
    assert(!quota->liveBytes());
    auto pool = core::makeShared<TestStagingPool>();
    const auto first = recorded(pool->allocate(8u, 1u));
    const auto second = recorded(pool->allocate(8u, 2u));
    const auto third = recorded(pool->allocate(8u, 3u));
    auto page = core::makeShared<int>(37);
    std::weak_ptr<int> observedPage = page;
    std::weak_ptr<TestStagingPool> observedPool = pool;
    {
        core::CpuMemoryQuotaScope scope(quota);
        RecordedStagingUploadJournal journal;
        assert(!journal.prepareBounded(2u));
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(!journal.preflightEnlist());
        assert(journal.enlist(pool, first, page) && journal.enlist(pool, second, page));
        const auto owners = pool.use_count(), pages = page.use_count();
        assert(std::string_view(journal.preflightEnlist()) == "RHI-STAGING-JOURNAL-ENTRY-CAPACITY");
        assert(!journal.enlist(pool, third, page));
        assert(!journal.enlist(*pool, third));
        // Converted shared_ptr arguments live through the full expression.
        // Inspect retained ownership only after those temporary copies expire.
        assert(journal.size() == 2u && pool.use_count() == owners && page.use_count() == pages
            && !pool->cancelCount() && pool->stats().liveSliceCount == 3u);
        assert(journal.rollbackLast() && pool->cancelOffset(0u) == second.offsetBytes);
        assert(!journal.preflightEnlist() && journal.enlist(pool, third, page));
        assert(journal.rollbackLast() && pool->cancelOffset(1u) == third.offsetBytes);
        pool.reset();
        page.reset();
        assert(!observedPool.expired() && !observedPage.expired());
        assert(journal.retireCompleted() && journal.empty() && observedPool.expired()
            && observedPage.expired() && journal.retireCompleted());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
    }
    assert(!quota->liveBytes());
    {
        TestStagingPool raw;
        const auto slice = recorded(raw.allocate());
        core::CpuMemoryQuotaScope scope(quota);
        RecordedStagingUploadJournal journal;
        assert(!journal.prepareBounded(1u) && journal.enlist(raw, slice));
        core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
        assert(pressure.charged());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(!journal.retireCompleted() && journal.size() == 1u && journal.rollback()
            && raw.cancelCount() == 1u && !raw.stats().liveSliceCount);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
    }
    {
        RecordedStagingUploadJournal zero, range, legacy;
        TestStagingPool raw;
        const auto slice = recorded(raw.allocate());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!zero.prepareBounded(0u) && zero.preflightEnlist() && !zero.enlist(raw, slice));
        assert(std::string_view(range.prepareBounded(std::numeric_limits<std::size_t>::max()))
                == "RHI-STAGING-JOURNAL-ENTRY-RANGE"
            && range.preflightEnlist() && !range.enlist(raw, slice));
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        assert(legacy.enlist(raw, slice) && legacy.rollback());
        assert(
            std::string_view(legacy.prepareBounded(1u)) == "RHI-STAGING-JOURNAL-PREPARATION-STATE");
    }
}

} // namespace

int main()
{
    {
        using namespace arp;
        auto pool = core::makeShared<TestStagingPool>();
        const auto first = pool->allocate(8u, 1u);
        const auto second = pool->allocate(8u, 2u);
        rhi::RecordedStagingUploadJournal pending;
        rhi::RecordedStagingUploadJournal completed;
        auto page = core::makeShared<int>(37);
        std::weak_ptr<int> observedPage = page;
        assert(pending.enlist(pool, recorded(first)));
        assert(completed.enlist(pool, recorded(second), page));
        page.reset();
        assert(!observedPage.expired());
        auto wrong = recorded(second);
        ++wrong.frameSerial;
        assert(!pool->retire(wrong) && pool->stats().retiredSliceCount == 0u);
        const auto allocationCount = core::defaultCpuAllocator().stats().allocationCount;
        assert(completed.retireCompleted());
        assert(observedPage.expired());
        assert(pool->stats().liveSliceCount == 1u && pool->stats().retiredSliceCount == 1u);
        assert(completed.retireCompleted());
        assert(!pool->retire(recorded(second)) && pool->stats().retiredSliceCount == 1u);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocationCount);
        std::weak_ptr<TestStagingPool> observed = pool;
        pool.reset();
        assert(!observed.expired());
        assert(pending.rollback());
        assert(observed.expired());
    }
    {
        TestStagingPool pool;
        arp::rhi::RecordedStagingUploadJournal legacy;
        const auto slice = pool.allocate();
        legacy.enlist(pool, recorded(slice));
        assert(!legacy.retireCompleted());
        assert(pool.stats().liveSliceCount == 1u);
        assert(legacy.rollback() && pool.stats().retiredSliceCount == 0u);
    }
    {
        using namespace arp;
        int current = 1;
        core::MutationSnapshot<int> snapshot(current);
        auto states = core::makeShared<core::MutationJournal>();
        assert(snapshot.prepare(*states));
        current = 2;
        {
            TestCommandBuffer command(states);
            assert(rhi::acceptRecordedCommandOwnership(command));
        }
        states->rollback();
        assert(current == 2 && states->empty());
        assert(snapshot.prepare(*states));
        current = 3;
        {
            TestCommandBuffer command(states);
        }
        assert(current == 3 && !states->empty());
        states.reset();
        assert(current == 2);
        TestCommandBuffer missing(std::shared_ptr<core::MutationJournal>{});
        assert(!rhi::acceptRecordedCommandOwnership(missing));
    }
    verifyExactCancellationDoesNotReuseHole();
    verifyReverseRollbackAndRollbackLast();
    verifyCommitLeavesCompletionOwnedSliceLive();
    verifyCommandBufferTransferRejectRetryAndAcceptance();
    verifyUploadStagingReadinessClassification();
    verifyColdJournalCapacityAndOwnership();
    return 0;
}
