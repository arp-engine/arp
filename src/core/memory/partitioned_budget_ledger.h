#pragma once

#include "core/container/bounded_sequence.h"
#include "core/container/generation_pool.h"
#include "core/diagnostics/diagnostic_error.h"
#include "core/memory/budget_vector_preflight.h"
#include "core/memory/cpu_allocation_footprint.h"

#include <atomic>
#include <thread>

namespace arp::core {

// Serialized-owner multidimensional accounting. Remote final owners publish
// only a generation release; the preparation thread collects whole records.
// Metadata only: no resource, execution, completion or reuse authority.
template <class Counters, std::size_t Dimensions, std::size_t Partitions>
class PartitionedBudgetLedger final {
    static_assert(Dimensions && Partitions);

    struct Entry {
        Counters charge;
        std::size_t partition;
        bool tracked = false;
    };

    struct Marker {
        std::atomic<std::uint64_t> generation{0u};
    };

    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

public:
    using Fields = typename BudgetVectorPreflight<Counters, Dimensions>::Fields;

    struct Snapshot {
        const char* diagnostic = nullptr;
        std::array<Counters, Partitions> usage{};
        Counters total{};
        std::uint32_t liveOwners = 0u;
    };

private:
    struct State {
        State(const Counters& capacity, const Fields& fields, std::uint32_t slots)
            : capacity(capacity), fields(fields), entries(slots), markers(slots)
        {
            for (std::uint32_t i = 0u; i < slots; ++i)
                markers.append(makeUnique<Marker>());
        }

        const char* threadDiagnostic() const noexcept
        {
            return owner == std::this_thread::get_id() ? nullptr : "CORE-PARTITION-THREAD";
        }

        void release(PoolHandle ticket) noexcept
        {
            // Only tickets issued by this state's ledger reach this method.
            auto expected = ticket.generation;
            markers.values()[ticket.index]->generation.compare_exchange_strong(
                expected, 0u, std::memory_order_release, std::memory_order_relaxed);
        }

        void collect() noexcept
        {
            entries.forEach([&](PoolHandle ticket, Entry& entry) {
                if (markers.values()[ticket.index]->generation.load(std::memory_order_acquire))
                    return;
                for (auto field : fields)
                    usage[entry.partition].*field -= entry.charge.*field;
                entries.erase(ticket);
            });
        }

        const Counters capacity;
        const Fields fields;
        GenerationPool<Entry> entries;
        BoundedSequence<UniquePtr<Marker>> markers;
        std::array<Counters, Partitions> usage{};
        const std::thread::id owner = std::this_thread::get_id();
        std::uint64_t storageBytes = 0u;
        bool closed = false;
    };

public:
    // Non-releasing metadata projection. It neither owns the tracked object
    // nor permits its caller to publish a release notification.
    class Identity final {
    public:
        Identity() noexcept = default;

    private:
        friend class PartitionedBudgetLedger;

        Identity(const std::shared_ptr<State>& state, PoolHandle ticket) noexcept
            : state_(state), ticket_(ticket)
        {}

        std::weak_ptr<State> state_;
        PoolHandle ticket_;
    };

    class Lease final {
    public:
        Lease() noexcept = default;

        ~Lease() { reset(); }

        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        Lease(Lease&& other) noexcept
            : state_(std::move(other.state_)), ticket_(std::exchange(other.ticket_, {}))
        {}

        Lease& operator=(Lease&& other) noexcept
        {
            if (this != &other) {
                reset();
                state_ = std::move(other.state_);
                ticket_ = std::exchange(other.ticket_, {});
            }
            return *this;
        }

        explicit operator bool() const noexcept { return bool(state_); }

        void reset() noexcept
        {
            if (state_)
                state_->release(ticket_);
            detach();
        }

        // Successful tracked allocation publication transfers notification to
        // its allocator. This does not release the slot.
        void detach() noexcept
        {
            state_.reset();
            ticket_ = {};
        }

    private:
        friend class PartitionedBudgetLedger;

        Lease(std::shared_ptr<State> state, PoolHandle ticket) noexcept
            : state_(std::move(state)), ticket_(ticket)
        {}

        std::shared_ptr<State> state_;
        PoolHandle ticket_;
    };

    static std::optional<PartitionedBudgetLedger> prepareCold(const Counters& capacity,
        const Fields& fields,
        std::uint32_t slots,
        const std::shared_ptr<CpuMemoryQuota>& quota,
        const char*& diagnostic)
    {
        diagnostic = nullptr;
        for (std::size_t i = 0u; i < Dimensions; ++i) {
            if (!fields[i]) {
                diagnostic = "CORE-PARTITION-FIELDS";
                return {};
            }
            for (std::size_t j = 0u; j < i; ++j)
                if (fields[i] == fields[j]) {
                    diagnostic = "CORE-PARTITION-FIELDS";
                    return {};
                }
        }
        if (!slots || !quota || !quota->limitBytes()) {
            diagnostic = "CORE-PARTITION-STORAGE";
            return {};
        }
        try {
            CpuMemoryQuotaScope scope(quota);
            auto state = makeShared<State>(capacity, fields, slots);
            CpuAllocationFootprint footprint(quota);
            footprint.add(state.get());
            state->entries.inspectStorage(footprint);
            footprint.add(state->markers.values().data());
            for (const auto& marker : state->markers.values())
                footprint.add(marker.get());
            if ((diagnostic = footprint.diagnostic()))
                return {};
            state->storageBytes = footprint.bytes();
            return PartitionedBudgetLedger(std::move(state));
        } catch (const std::bad_alloc&) {
            diagnostic = "CORE-PARTITION-ALLOCATION";
            return {};
        }
    }

    PartitionedBudgetLedger(PartitionedBudgetLedger&&) noexcept = default;
    PartitionedBudgetLedger& operator=(PartitionedBudgetLedger&&) noexcept = default;
    PartitionedBudgetLedger(const PartitionedBudgetLedger&) = delete;

    const char* threadDiagnostic() const noexcept { return state_->threadDiagnostic(); }

    Identity identity(const Lease& lease, const char*& diagnostic) noexcept
    {
        diagnostic = validate(lease);
        if (!diagnostic && state_->closed)
            diagnostic = "CORE-PARTITION-CLOSED";
        return diagnostic ? Identity{} : Identity(state_, lease.ticket_);
    }

    const char* validatePartition(const Identity& identity, std::size_t expected) noexcept
    {
        if (const auto* id = threadDiagnostic())
            return id;
        if (identity.state_.lock() != state_)
            return "CORE-PARTITION-SOURCE";
        state_->collect();
        if (state_->closed)
            return "CORE-PARTITION-CLOSED";
        const auto* entry = state_->entries.get(identity.ticket_);
        if (!entry)
            return "CORE-PARTITION-STALE";
        return entry->partition == expected ? nullptr : "CORE-PARTITION-TRANSITION";
    }

    std::uint64_t retainedStorageBytes() const noexcept { return state_->storageBytes; }

    Lease reserve(const Counters& charge, std::size_t partition, const char*& diagnostic) noexcept
    {
        diagnostic = threadDiagnostic();
        if (diagnostic)
            return {};
        state_->collect();
        if (state_->closed)
            diagnostic = "CORE-PARTITION-CLOSED";
        else if (partition >= Partitions)
            diagnostic = "CORE-PARTITION-INDEX";
        else {
            bool nonzero = false;
            for (auto field : state_->fields)
                nonzero |= charge.*field != 0u;
            if (!nonzero)
                diagnostic = "CORE-PARTITION-EMPTY";
            else if (const auto* id = preflight(charge))
                diagnostic = id;
            else {
                auto ticket = state_->entries.emplace(Entry{charge, partition});
                if (!ticket)
                    diagnostic = state_->entries.diagnostic();
                else {
                    state_->markers.values()[ticket.index]->generation.store(
                        ticket.generation, std::memory_order_release);
                    for (auto field : state_->fields)
                        state_->usage[partition].*field += charge.*field;
                    return Lease(state_, ticket);
                }
            }
        }
        return {};
    }

    const char* amend(Lease& lease, const Counters& addition) noexcept
    {
        if (const auto* id = validate(lease))
            return id;
        if (const auto* id = preflight(addition))
            return id;
        auto& entry = *state_->entries.get(lease.ticket_);
        for (auto field : state_->fields) {
            entry.charge.*field += addition.*field;
            state_->usage[entry.partition].*field += addition.*field;
        }
        return nullptr;
    }

    const char* transfer(Lease& lease, std::size_t expected, std::size_t destination) noexcept
    {
        if (const auto* id = validate(lease))
            return id;
        auto& entry = *state_->entries.get(lease.ticket_);
        if (destination >= Partitions || expected != entry.partition)
            return "CORE-PARTITION-TRANSITION";
        for (auto field : state_->fields) {
            state_->usage[entry.partition].*field -= entry.charge.*field;
            state_->usage[destination].*field += entry.charge.*field;
        }
        entry.partition = destination;
        return nullptr;
    }

    Snapshot snapshot() const noexcept
    {
        if (const auto* id = threadDiagnostic())
            return {id};
        state_->collect();
        Snapshot result;
        result.usage = state_->usage;
        result.liveOwners = state_->entries.liveCount();
        for (const auto& partition : result.usage)
            for (auto field : state_->fields)
                result.total.*field += partition.*field;
        return result;
    }

    const char* close() noexcept
    {
        if (const auto* id = threadDiagnostic())
            return id;
        state_->closed = true;
        return nullptr;
    }

    template <class T, class... Args>
    std::shared_ptr<T> makeSharedTracked(Lease& lease, Args&&... args)
    {
        // This helper deliberately handles plain metadata only. Objects with
        // core shared identity must use its binding-aware allocation entry.
        static_assert(!std::is_base_of_v<SharedObjectIdentity, T>);
        if (const auto* id = validate(lease))
            throw DiagnosticError(id);
        auto& entry = *state_->entries.get(lease.ticket_);
        if (entry.tracked)
            throw DiagnosticError("CORE-PARTITION-ALREADY-TRACKED");
        entry.tracked = true;
        return std::allocate_shared<T>(
            TrackedAllocator<T>(state_, lease.ticket_), std::forward<Args>(args)...);
    }

private:
    template <class T> class TrackedAllocator {
    public:
        using value_type = T;

        TrackedAllocator(std::shared_ptr<State> state, PoolHandle ticket) noexcept
            : state_(std::move(state)), ticket_(ticket)
        {}

        template <class U>
        TrackedAllocator(const TrackedAllocator<U>& other) noexcept
            : state_(other.state_), ticket_(other.ticket_)
        {}

        T* allocate(std::size_t count)
        {
            if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
                throw std::bad_alloc();
            return static_cast<T*>(
                defaultCpuMemoryResource().allocate(count * sizeof(T), alignof(T)));
        }

        void deallocate(T* pointer, std::size_t count) noexcept
        {
            defaultCpuMemoryResource().deallocate(pointer, count * sizeof(T), alignof(T));
            state_->release(ticket_);
        }

        template <class U> bool operator==(const TrackedAllocator<U>& other) const noexcept
        {
            return state_ == other.state_ && ticket_ == other.ticket_;
        }

        template <class U> bool operator!=(const TrackedAllocator<U>& other) const noexcept
        {
            return !(*this == other);
        }

    private:
        template <class> friend class TrackedAllocator;
        std::shared_ptr<State> state_;
        PoolHandle ticket_;
    };

    explicit PartitionedBudgetLedger(std::shared_ptr<State> state) noexcept
        : state_(std::move(state))
    {}

    const char* validate(const Lease& lease) noexcept
    {
        if (const auto* id = threadDiagnostic())
            return id;
        state_->collect();
        if (lease.state_ != state_)
            return "CORE-PARTITION-SOURCE";
        return state_->entries.get(lease.ticket_) ? nullptr : "CORE-PARTITION-STALE";
    }

    const char* preflight(const Counters& addition) const noexcept
    {
        const auto result = BudgetVectorPreflight<Counters, Dimensions>::assess(
            state_->capacity, state_->usage, addition, state_->fields);
        return result.fits()                                ? nullptr
            : result.status == BudgetVectorStatus::Overflow ? "CORE-PARTITION-OVERFLOW"
                                                            : "CORE-PARTITION-CAPACITY";
    }

    std::shared_ptr<State> state_;
};

} // namespace arp::core
