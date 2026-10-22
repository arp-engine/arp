#pragma once
#include "core/memory/cpu_allocator.h"
#include "core/ownership/versioned_mutation_value.h"

namespace arp::core {
// Stable, fixed-size original values with a conservative whole-array revision.
// Snapshot storage is allocated at construction; neither array ever changes size.
// Revisions alone do not bind owner identity. Operations are externally serialized.
template <class T> class VersionedMutationArray final {
    static_assert(std::is_trivially_copyable_v<T> && std::is_nothrow_copy_assignable_v<T>);
    using Stamp = VersionedMutationValue<bool>;

public:
    using Observation = typename Stamp::Observation;
    using PendingObservation = typename Stamp::PendingObservation;
    enum class WriteResult { Written, InvalidRange, Conflict, Exhausted, EnlistmentExhausted };

    VersionedMutationArray(std::size_t size,
        T initial,
        std::uint64_t firstRevision = 1u,
        std::uint64_t firstEnlistmentEpoch = 1u)
        : values_(size, initial, &defaultCpuMemoryResource()),
          stamp_(false, firstRevision, firstEnlistmentEpoch), snapshot_(values_)
    {}

    VersionedMutationArray(const VersionedMutationArray&) = delete;

    std::size_t size() const noexcept { return values_.size(); }

    const T* valueAt(std::size_t index) const noexcept
    {
        return index < size() ? &values_[index] : nullptr;
    }

    std::uint64_t revision() const noexcept { return stamp_.revision(); }

    bool active() const noexcept { return stamp_.active(); }

    bool pending() const noexcept { return stamp_.pending() || snapshot_.pending(); }

    bool enlistmentExhausted() const noexcept { return stamp_.enlistmentExhausted(); }

    bool canPrepare(const MutationJournal& journal) const noexcept
    {
        return stamp_.canPrepare(journal) && snapshot_.canPrepare(journal);
    }

    bool canWriteBatch(const MutationJournal& journal, std::uint64_t writes) const noexcept
    {
        return canPrepare(journal) && stamp_.canWriteBatch(journal, writes);
    }

    bool prepare(MutationJournal& journal) noexcept
    {
        return canPrepare(journal) && snapshot_.prepare(journal) && stamp_.prepare(journal);
    }

    void invalidate() noexcept { stamp_.invalidate(); }

    auto observe() const noexcept
    {
        return pending() ? std::optional<Observation>{} : stamp_.observe();
    }

    bool matches(const Observation& observation) const noexcept
    {
        return !pending() && stamp_.matches(observation);
    }

    auto observePending(const MutationJournal& journal) const noexcept
    {
        return snapshot_.ownedBy(journal) ? stamp_.observePending(journal)
                                          : std::optional<PendingObservation>{};
    }

    bool matchesPending(
        const PendingObservation& observation, const MutationJournal& journal) const noexcept
    {
        return snapshot_.ownedBy(journal) && stamp_.matchesPending(observation, journal);
    }

    template <class IndexAt>
    WriteResult writeIndices(
        std::size_t count, IndexAt&& indexAt, T after, MutationJournal* journal = nullptr) noexcept
    {
        static_assert(
            noexcept(indexAt(std::size_t{})), "index projection must not throw or mutate");
        if (!active())
            return WriteResult::Exhausted;
        if (journal && enlistmentExhausted())
            return WriteResult::EnlistmentExhausted;
        if (journal ? !canPrepare(*journal) : pending())
            return WriteResult::Conflict;
        if (!count || count > size())
            return WriteResult::InvalidRange;
        for (std::size_t i = 0u; i < count; ++i)
            if (indexAt(i) >= size())
                return WriteResult::InvalidRange;
        // Reserve the revision before enlisting the array; exhausted writes
        // leave all values unchanged. Preflight fixes exact journal ownership.
        const auto result = stamp_.tryWrite(false, journal);
        if (result == Stamp::WriteResult::Exhausted)
            return WriteResult::Exhausted;
        if (result == Stamp::WriteResult::Conflict)
            return WriteResult::Conflict;
        if (result == Stamp::WriteResult::EnlistmentExhausted)
            return WriteResult::EnlistmentExhausted;
        // Fixed equal sizes/capacities and trivial T prove nonallocating copy.
        if (journal && !snapshot_.prepare(*journal))
            return WriteResult::Conflict;
        for (std::size_t i = 0u; i < count; ++i)
            values_[indexAt(i)] = after;
        return WriteResult::Written;
    }

private:
    pmr::Vector<T> values_;
    Stamp stamp_;
    MutationSnapshot<pmr::Vector<T>> snapshot_;
};
} // namespace arp::core
