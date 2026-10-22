#pragma once

#include "core/container/bounded_sequence.h"
#include "core/ownership/bounded_retainer.h"
#include "rhi/frontend/command_retention_validation.h"

#include <new>
#include <stdexcept>

namespace arp::rhi::common {

struct CommandRetainedQueryRecord {
    std::shared_ptr<void> pool;
    std::uint32_t index = 0u;

    bool operator==(const CommandRetainedQueryRecord& other) const noexcept
    {
        return pool == other.pool && index == other.index;
    }
};

// Backend-owned metadata only. The adapter must validate native registry,
// query type/index/state and completion proof; this class authorizes no GPU work.
class CommandRetentionStorage final {
    class ConstructionKey {
        friend class CommandRetentionStorage;

        ConstructionKey() {}

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    enum class State { Recording, Sealed, Submitted, Retired };

    CommandRetentionStorage(
        ConstructionKey, CommandRetentionBudget budget, std::shared_ptr<core::CpuMemoryQuota> quota)
        : budget_(budget), quota_(std::move(quota)), resources_(budget.maxResourceOwners),
          pools_(budget.maxQueryPools), timestamps_(budget.maxTimestampWrites),
          statistics_(budget.maxPipelineStatisticsWrites)
    {}

    static core::UniquePtr<CommandRetentionStorage> prepare(const CommandRetentionBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = semantics::CommandRetentionBudgetLowering::validateIntent(budget);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return core::makeUnique<CommandRetentionStorage>(
                ConstructionKey{}, budget, std::move(quota));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED";
            return {};
        }
    }

    const char* preflightResources(
        const std::shared_ptr<const void>* owners, std::size_t count) const noexcept
    {
        if (state_ != State::Recording)
            return "RHI-COMMAND-RETENTION-RECORDING-CLOSED";
        if (!resources_.canRetainAll(owners, count))
            return "RHI-COMMAND-RETENTION-OWNER-CAPACITY";
        return nullptr;
    }

    const char* retainResources(const std::shared_ptr<const void>* owners, std::size_t count)
    {
        if (const auto* diagnostic = preflightResources(owners, count))
            return diagnostic;
        resources_.retainAll(owners, count);
        return nullptr;
    }

    // Immutable indexed metadata may enumerate arbitrarily shaped descriptor
    // owner batches without a guessed owner-count-derived temporary budget.
    // The core projection contract applies; a mapping change is terminal and
    // requires the adapter to poison recording before any native execution.
    template <class OwnerAt>
    const char* preflightResourceProjection(std::size_t count, OwnerAt&& ownerAt) const noexcept
    {
        if (state_ != State::Recording)
            return "RHI-COMMAND-RETENTION-RECORDING-CLOSED";
        if (!resources_.canRetainMapped(count, ownerAt))
            return "RHI-COMMAND-RETENTION-OWNER-CAPACITY";
        return nullptr;
    }

    template <class OwnerAt>
    const char* retainResourceProjection(std::size_t count, OwnerAt&& ownerAt)
    {
        if (const auto* diagnostic = preflightResourceProjection(count, ownerAt))
            return diagnostic;
        if (!resources_.retainMapped(count, ownerAt))
            return "RHI-COMMAND-RETENTION-OWNER-MAPPING-CHANGED";
        return nullptr;
    }

    // Adapters preflight before native mutation, then commit on the same
    // serialized owner thread. Preflight itself changes no metadata or quota.
    const char* preflightTimestamp(const CommandRetainedQueryRecord& record,
        const std::shared_ptr<const void>* resources = nullptr,
        std::size_t count = 0u) const noexcept
    {
        return preflightQuery(timestamps_, record, resources, count);
    }

    const char* preflightStatistics(const CommandRetainedQueryRecord& record,
        const std::shared_ptr<const void>* resources = nullptr,
        std::size_t count = 0u) const noexcept
    {
        return preflightQuery(statistics_, record, resources, count);
    }

    const char* admitTimestamp(const CommandRetainedQueryRecord& record,
        const std::shared_ptr<const void>* resources = nullptr,
        std::size_t count = 0u)
    {
        return admitQuery(timestamps_, record, resources, count);
    }

    const char* admitStatistics(const CommandRetainedQueryRecord& record,
        const std::shared_ptr<const void>* resources = nullptr,
        std::size_t count = 0u)
    {
        return admitQuery(statistics_, record, resources, count);
    }

    bool sealForSubmission() noexcept
    {
        if (state_ != State::Recording)
            return false;
        state_ = State::Sealed;
        return true;
    }

    bool markSubmitted() noexcept
    {
        if (state_ != State::Sealed)
            return false;
        state_ = State::Submitted;
        return true;
    }

    bool retireAfterCompletion(bool completionProven) noexcept
    {
        if (state_ != State::Submitted || !completionProven)
            return false;
        retire();
        return true;
    }

    bool abandonUnsubmitted() noexcept
    {
        if (state_ != State::Recording && state_ != State::Sealed)
            return false;
        retire();
        return true;
    }

    State state() const noexcept { return state_; }

    const CommandRetentionBudget& budget() const noexcept { return budget_; }

    const std::shared_ptr<core::CpuMemoryQuota>& quota() const noexcept { return quota_; }

    std::size_t resourceOwnerCount() const noexcept { return resources_.values().size(); }

    std::size_t queryPoolOwnerCount() const noexcept { return pools_.values().size(); }

    const core::pmr::Vector<CommandRetainedQueryRecord>& timestamps() const noexcept
    {
        return timestamps_.values();
    }

    const core::pmr::Vector<CommandRetainedQueryRecord>& statistics() const noexcept
    {
        return statistics_.values();
    }

    std::uint64_t quotaLiveBytes() const noexcept { return quota_->liveBytes(); }

private:
    const char* preflightQuery(const core::BoundedSequence<CommandRetainedQueryRecord>& records,
        const CommandRetainedQueryRecord& record,
        const std::shared_ptr<const void>* resources,
        std::size_t count) const noexcept
    {
        if (state_ != State::Recording)
            return "RHI-COMMAND-RETENTION-RECORDING-CLOSED";
        if (!record.pool)
            return "RHI-COMMAND-RETENTION-QUERY-OWNER-MISSING";
        if (records.contains(record))
            return "RHI-COMMAND-RETENTION-QUERY-DUPLICATE";
        if (!records.canAppend() || !pools_.canRetain(record.pool))
            return "RHI-COMMAND-RETENTION-QUERY-CAPACITY";
        if (!resources_.canRetainAll(resources, count))
            return "RHI-COMMAND-RETENTION-OWNER-CAPACITY";
        return nullptr;
    }

    const char* admitQuery(core::BoundedSequence<CommandRetainedQueryRecord>& records,
        const CommandRetainedQueryRecord& record,
        const std::shared_ptr<const void>* resources,
        std::size_t count)
    {
        if (const auto* diagnostic = preflightQuery(records, record, resources, count))
            return diagnostic;
        // All four reservations are immutable and preflighted on the serialized
        // owner thread. These operations only copy already-owned references.
        resources_.retainAll(resources, count);
        pools_.retain(record.pool);
        records.append(record);
        return nullptr;
    }

    void retire() noexcept
    {
        timestamps_.clear();
        statistics_.clear();
        pools_.clear();
        resources_.clear();
        state_ = State::Retired;
    }

    const CommandRetentionBudget budget_;
    const std::shared_ptr<core::CpuMemoryQuota> quota_;
    core::BoundedRetainer<void> resources_, pools_;
    core::BoundedSequence<CommandRetainedQueryRecord> timestamps_, statistics_;
    State state_ = State::Recording;
};

} // namespace arp::rhi::common
