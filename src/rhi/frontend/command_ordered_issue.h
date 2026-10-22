#pragma once

#include "core/container/bounded_sequence.h"
#include "core/ownership/bounded_retainer.h"
#include "core/ownership/lifetime_epoch.h"
#include "rhi/semantics/ordered_command_issue.h"

#include <cstring>
#include <stdexcept>

namespace arp::rhi::common {

// Backend-owned CPU custody. An adapter must join its original registry,
// queue/context and actual bridge consumption before calling issue methods.
// A caller-created instance or a scalar observation authorizes no GPU work.
// All mutation and payload access are serialized on that adapter's owner thread.
class OrderedCommandIssue final {
    class ConstructionKey {
        friend class OrderedCommandIssue;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    enum class Phase { Recording, Sealed, Issuing, Issued, Cancelled, Failed, Lost };
    enum class Failure { NativeCall, PayloadValidation };

    struct PayloadView {
        const std::byte* data = nullptr;
        std::size_t size = 0u;
    };

    struct Stats {
        std::size_t recordedOperations = 0u;
        std::size_t acceptedOperations = 0u;
        bool workAccepted = false;
        bool mayHaveNativeSideEffects = false;
        bool gpuCompleted = false;
        std::uint64_t cpuQuotaLiveBytes = 0u;
        std::uint64_t cpuQuotaPeakBytes = 0u;
    };

    OrderedCommandIssue(ConstructionKey,
        OrderedCommandIssueBudget budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        core::LifetimeEpoch::Ticket context)
        : budget_(budget), quota_(std::move(quota)), context_(std::move(context)),
          owners_(budget.maxResourceOwners), operations_(budget.maxOperations)
    {
        bytes_.reserve(static_cast<std::size_t>(budget.maxSnapshotBytes));
    }

    OrderedCommandIssue(const OrderedCommandIssue&) = delete;
    OrderedCommandIssue& operator=(const OrderedCommandIssue&) = delete;
    OrderedCommandIssue(OrderedCommandIssue&&) = delete;
    OrderedCommandIssue& operator=(OrderedCommandIssue&&) = delete;

    static core::UniquePtr<OrderedCommandIssue> prepare(const OrderedCommandIssueBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        core::LifetimeEpoch::Ticket context,
        const char*& diagnostic)
    {
        diagnostic = semantics::OrderedCommandIssueLowering::validate(budget);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-ORDERED-ISSUE-CPU-QUOTA";
            return {};
        }
        if (!context.isCurrent()) {
            diagnostic = "RHI-ORDERED-ISSUE-CONTEXT";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return core::makeUnique<OrderedCommandIssue>(
                ConstructionKey{}, budget, std::move(quota), std::move(context));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORDERED-ISSUE-ALLOCATION";
        } catch (const std::length_error&) {
            diagnostic = "RHI-ORDERED-ISSUE-ALLOCATION";
        }
        return {};
    }

    const char* recordOperation(const std::shared_ptr<const void>* owners,
        std::size_t count,
        const void* source,
        std::size_t size)
    {
        if (const auto* id = check())
            return id;
        if (phase_ != Phase::Recording)
            return "RHI-ORDERED-ISSUE-STATE";
        if (!operations_.canAppend())
            return "RHI-ORDERED-ISSUE-OPERATION-CAPACITY";
        if (count > budget_.maxOwnerReferences - recordedReferences_)
            return "RHI-ORDERED-ISSUE-REFERENCE-CAPACITY";
        if (!owners_.canRetainAll(owners, count))
            return "RHI-ORDERED-ISSUE-OWNER-CAPACITY";
        if (size && !source)
            return "RHI-ORDERED-ISSUE-SNAPSHOT-SOURCE";
        if (size > budget_.maxSnapshotBytes - bytes_.size())
            return "RHI-ORDERED-ISSUE-SNAPSHOT-CAPACITY";
        // Complete preflight above; all containers are preallocated and all
        // elements below are nonthrowing scalar/reference copies.
        const auto offset = bytes_.size();
        bytes_.resize(offset + size);
        if (size)
            std::memmove(bytes_.data() + offset, source, size);
        owners_.retainAll(owners, count);
        operations_.append(Range{offset, size});
        recordedReferences_ += count;
        return nullptr;
    }

    const char* seal() noexcept
    {
        if (const auto* id = check())
            return id;
        if (phase_ != Phase::Recording)
            return "RHI-ORDERED-ISSUE-STATE";
        if (operations_.values().empty())
            return "RHI-ORDERED-ISSUE-EMPTY";
        phase_ = Phase::Sealed;
        return nullptr;
    }

    // Read-only full preflight while sealed. Does not mark possible effects,
    // advance the issue cursor, or transfer custody. Borrow expires on mutation.
    const char* inspectSealed(const core::LifetimeEpoch::Ticket& issuer,
        std::size_t ordinal,
        PayloadView& payload) noexcept
    {
        payload = {};
        if (const auto* id = checkIssuer(issuer))
            return id;
        if (phase_ != Phase::Sealed)
            return "RHI-ORDERED-ISSUE-STATE";
        if (ordinal >= operationCount())
            return "RHI-ORDERED-ISSUE-NO-OPERATION";
        const auto& range = operations_.values()[ordinal];
        payload = {range.size ? bytes_.data() + range.offset : nullptr, range.size};
        return nullptr;
    }

    const char* beginIssue(const core::LifetimeEpoch::Ticket& issuer) noexcept
    {
        if (const auto* id = checkIssuer(issuer))
            return id;
        if (phase_ != Phase::Sealed)
            return "RHI-ORDERED-ISSUE-STATE";
        phase_ = Phase::Issuing;
        return nullptr;
    }

    const char* beginOperation(
        const core::LifetimeEpoch::Ticket& issuer, PayloadView& payload) noexcept
    {
        payload = {};
        if (const auto* id = checkIssuer(issuer))
            return id;
        if (phase_ != Phase::Issuing)
            return "RHI-ORDERED-ISSUE-STATE";
        if (pending_)
            return "RHI-ORDERED-ISSUE-OPERATION-PENDING";
        if (accepted_ == operationCount())
            return "RHI-ORDERED-ISSUE-NO-OPERATION";
        const auto& range = operations_.values()[accepted_];
        payload = {range.size ? bytes_.data() + range.offset : nullptr, range.size};
        pending_ = true;
        possibleEffects_ = true;
        return nullptr;
    }

    const char* acceptOperation(const core::LifetimeEpoch::Ticket& issuer) noexcept
    {
        if (const auto* id = checkIssuer(issuer))
            return id;
        if (phase_ != Phase::Issuing)
            return "RHI-ORDERED-ISSUE-STATE";
        if (!pending_)
            return "RHI-ORDERED-ISSUE-NO-OPERATION";
        ++accepted_;
        pending_ = false;
        return nullptr;
    }

    const char* finishIssue(const core::LifetimeEpoch::Ticket& issuer) noexcept
    {
        if (const auto* id = checkIssuer(issuer))
            return id;
        if (phase_ != Phase::Issuing)
            return "RHI-ORDERED-ISSUE-STATE";
        if (pending_ || accepted_ != operationCount())
            return "RHI-ORDERED-ISSUE-OPERATIONS-REMAINING";
        releaseCpuCustody(Phase::Issued);
        return nullptr;
    }

    const char* cancel() noexcept
    {
        if (const auto* id = checkContext())
            return id;
        if (phase_ != Phase::Recording && phase_ != Phase::Sealed)
            return "RHI-ORDERED-ISSUE-STATE";
        releaseCpuCustody(Phase::Cancelled);
        return nullptr;
    }

    const char* fail(Failure reason) noexcept
    {
        if (const auto* id = checkContext())
            return id;
        if (phase_ != Phase::Recording && phase_ != Phase::Sealed && phase_ != Phase::Issuing)
            return "RHI-ORDERED-ISSUE-STATE";
        switch (reason) {
        case Failure::NativeCall:
            diagnostic_ = "RHI-ORDERED-ISSUE-NATIVE-FAILURE";
            break;
        case Failure::PayloadValidation:
            diagnostic_ = "RHI-ORDERED-ISSUE-PAYLOAD-FAILURE";
            break;
        default:
            return "RHI-ORDERED-ISSUE-FAILURE-KIND";
        }
        releaseCpuCustody(Phase::Failed);
        return nullptr;
    }

    Phase phase() const noexcept { return phase_; }

    const char* diagnostic() const noexcept { return diagnostic_; }

    std::size_t operationCount() const noexcept { return operations_.values().size(); }

    std::size_t ownerCount() const noexcept { return owners_.values().size(); }

    std::size_t snapshotByteCount() const noexcept { return bytes_.size(); }

    bool nativeExecutionProven() const noexcept { return false; }

    Stats stats() const noexcept
    {
        return {operationCount(),
            accepted_,
            accepted_ != 0u,
            possibleEffects_,
            false,
            quota_->liveBytes(),
            quota_->peakBytes()};
    }

private:
    struct Range {
        std::size_t offset, size;
    };

    const char* checkContext() noexcept
    {
        if (phase_ == Phase::Lost)
            return "RHI-ORDERED-ISSUE-CONTEXT-LOST";
        if (!context_.isCurrent()) {
            if (phase_ != Phase::Cancelled && phase_ != Phase::Failed) {
                diagnostic_ = "RHI-ORDERED-ISSUE-CONTEXT-LOST";
                releaseCpuCustody(Phase::Lost);
            }
            return "RHI-ORDERED-ISSUE-CONTEXT-LOST";
        }
        return nullptr;
    }

    const char* check() noexcept
    {
        if (const auto* id = checkContext())
            return id;
        return quota_->limitBytes() == budget_.maxCpuMetadataBytes ? nullptr
                                                                   : "RHI-ORDERED-ISSUE-CPU-QUOTA";
    }

    const char* checkIssuer(const core::LifetimeEpoch::Ticket& issuer) noexcept
    {
        if (const auto* id = check())
            return id;
        return context_.sameCurrentEpoch(issuer) ? nullptr : "RHI-ORDERED-ISSUE-CONTEXT-MISMATCH";
    }

    void releaseCpuCustody(Phase terminal) noexcept
    {
        pending_ = false;
        bytes_.clear();
        owners_.clear();
        phase_ = terminal;
    }

    const OrderedCommandIssueBudget budget_;
    const std::shared_ptr<core::CpuMemoryQuota> quota_;
    const core::LifetimeEpoch::Ticket context_;
    core::BoundedRetainer<void> owners_;
    core::BoundedSequence<Range> operations_;
    core::pmr::Vector<std::byte> bytes_{&core::defaultCpuMemoryResource()};
    Phase phase_ = Phase::Recording;
    std::size_t accepted_ = 0u;
    std::uint64_t recordedReferences_ = 0u;
    bool pending_ = false;
    bool possibleEffects_ = false;
    const char* diagnostic_ = nullptr;
};
} // namespace arp::rhi::common
