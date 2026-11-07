#pragma once

#include "core/cpu_allocator.h"
#include "core/hash/stable_hash.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#include <optional>
#include <thread>
#include <utility>

namespace arp::scriptvm::detail {

// Indexed ownership metadata, private to the experimental VM frame domain.
template <class Carrier, class Traits, std::size_t Slots, std::size_t Cells>
class PrivateDomain final {
    enum class State { Free, Active, Retiring };
    static constexpr std::size_t absent = std::numeric_limits<std::size_t>::max();

    struct Cell {
        std::optional<Carrier> root;
        std::size_t aliases = 0;
        State state = State::Free;
    };

    struct Slot {
        std::optional<Carrier> boxed;
        std::size_t cell = absent;
    };

    struct IndexEntry {
        const void* identity = nullptr;
        std::size_t cell = absent;
    };

    struct Storage {
        explicit Storage(std::pmr::memory_resource* resource)
            : cells(resource), slots(resource), index(resource), freeCells(resource)
        {}

        arp::core::pmr::Deque<Cell> cells;
        arp::core::pmr::Deque<Slot> slots;
        arp::core::pmr::Vector<IndexEntry> index;
        arp::core::pmr::Vector<std::size_t> freeCells;
    };

public:
    enum class Preparation { Ready, DeclinedQuota, DeclinedAllocation };

    explicit PrivateDomain(
        std::pmr::memory_resource* resource = &arp::core::defaultCpuMemoryResource(),
        bool vmQuotaPresent = false)
    {
        // optional Storage is disengaged: even deque map blocks cannot allocate
        // before this admission gate. No private owner exists on refusal.
        if (vmQuotaPresent || arp::core::currentCpuMemoryQuota()) {
            preparation_ = Preparation::DeclinedQuota;
            return;
        }
        try {
            storage_.emplace(resource);
            storage_->cells.resize(Cells);
            storage_->slots.resize(Slots);
            const auto buckets = indexCapacity(Cells);
            if (buckets == 0 || buckets > storage_->index.max_size())
                throw std::bad_alloc();
            storage_->index.resize(buckets);
            storage_->freeCells.reserve(Cells);
            for (std::size_t i = Cells; i > 0; --i)
                storage_->freeCells.push_back(i - 1);
            cellLimit_ = Cells;
            slotLimit_ = Slots;
            preparation_ = Preparation::Ready;
        } catch (const std::bad_alloc&) {
            storage_.reset();
            preparation_ = Preparation::DeclinedAllocation;
        }
    }

    bool ready() const noexcept { return preparation_ == Preparation::Ready; }

    bool onOwnerThread() const noexcept { return ownerThread_ == std::this_thread::get_id(); }

    Preparation preparation() const noexcept { return preparation_; }

    std::size_t slotCount() const noexcept { return slotLimit_; }

    std::size_t debugLookupProbes() const noexcept { return lookupProbes_; }

    void debugValidateIndex() const
    {
        checkThread();
        std::size_t active = 0;
        for (std::size_t i = 0; i < cellLimit_; ++i) {
            const auto& cell = storage_->cells[i];
            if (cell.state == State::Active) {
                ++active;
                assert(matchingCell(Traits::identity(*cell.root)) == i);
            }
        }
        std::size_t indexed = 0;
        for (const auto& entry : storage_->index) {
            if (entry.cell == absent)
                continue;
            ++indexed;
            assert(entry.cell < cellLimit_);
            const auto& cell = storage_->cells[entry.cell];
            assert(cell.state == State::Active && Traits::identity(*cell.root) == entry.identity);
        }
        assert(active == indexed);
        for (std::size_t n = 0; n < storage_->freeCells.size(); ++n) {
            const auto i = storage_->freeCells[n];
            assert(i < cellLimit_ && storage_->cells[i].state == State::Free);
            for (std::size_t previous = 0; previous < n; ++previous)
                assert(storage_->freeCells[previous] != i);
        }
        assert(active + storage_->freeCells.size() + retiringCells() == cellLimit_);
    }

    std::size_t cellCapacity() const noexcept { return cellLimit_; }

    // Only append empty metadata. Existing public Carrier addresses and root
    // addresses remain stable. A failed append can retain unused empty backing
    // storage, but neither logical limit commits until both arrays are ready.
    bool growMetadata(std::size_t slots, std::size_t cells, bool vmQuotaPresent = false)
    {
        checkThread();
        if (!ready() || closing_)
            return false;
        if (slots <= slotLimit_ && cells <= cellLimit_)
            return true;
        if (vmQuotaPresent || arp::core::currentCpuMemoryQuota())
            return false;
        if (slots > storage_->slots.max_size() || cells > storage_->cells.max_size())
            return false;
        const auto buckets = indexCapacity(std::max(cellLimit_, cells));
        if (buckets == 0 || buckets > storage_->index.max_size())
            return false;
        arp::core::pmr::Vector<IndexEntry> replacement(storage_->index.get_allocator());
        try {
            // Stage the index before changing any root or logical capacity.
            if (buckets > storage_->index.size()) {
                replacement.resize(buckets);
                for (const auto& entry : storage_->index)
                    if (entry.cell != absent)
                        insertIndex(replacement, entry.identity, entry.cell);
            }
            storage_->freeCells.reserve(std::max(cellLimit_, cells));
            while (storage_->cells.size() < cells)
                storage_->cells.emplace_back();
            while (storage_->slots.size() < slots)
                storage_->slots.emplace_back();
        } catch (const std::bad_alloc&) {
            return false;
        }
        if (!replacement.empty())
            storage_->index.swap(replacement);
        for (std::size_t i = cells; i > cellLimit_; --i)
            storage_->freeCells.push_back(i - 1);
        slotLimit_ = std::max(slotLimit_, slots);
        cellLimit_ = std::max(cellLimit_, cells);
        return true;
    }

    PrivateDomain(const PrivateDomain&) = delete;
    PrivateDomain(PrivateDomain&&) = delete;
    PrivateDomain& operator=(const PrivateDomain&) = delete;
    PrivateDomain& operator=(PrivateDomain&&) = delete;

    ~PrivateDomain()
    {
        // Inactive pooled frames may be handed to another thread with their VM.
        // Active references remain thread confined. Empty metadata owns no
        // payload and the core allocator supports cross-thread deallocation.
        if (!onOwnerThread()) {
            if (ready())
                for (const auto& cell : storage_->cells)
                    if (cell.state != State::Free || cell.root || cell.aliases != 0)
                        std::abort();
            return;
        }
        checkThread();
        if (!ready())
            return;
        closing_ = true;
        for (std::size_t index = 0; index < slotLimit_; ++index)
            drop(index);
        for (const auto& cell : storage_->cells)
            assert(cell.state == State::Free && !cell.root && cell.aliases == 0);
    }

    bool empty(std::size_t index) const
    {
        check(index);
        return !storage_->slots[index].boxed && storage_->slots[index].cell == absent;
    }

    bool import(std::size_t index, const Carrier& source)
    {
        check(index);
        if (closing_)
            return false;
        if (storage_->slots[index].boxed) {
            // Once borrowed, keep the public Carrier object at its address.
            *storage_->slots[index].boxed = source;
            return true;
        }
        const auto matched = matchingCell(Traits::identity(source));
        const auto free = matched != absent ? matched : freeCell();
        if (free == absent)
            return false;
        auto& cell = storage_->cells[free];
        if (matched != absent) {
            // Capture the incoming owner before releasing any destination.
            ++cell.aliases;
        } else {
            cell.root.emplace(source);
            cell.aliases = 1;
            cell.state = State::Active;
            assert(!storage_->freeCells.empty() && storage_->freeCells.back() == free);
            storage_->freeCells.pop_back();
            insertIndex(storage_->index, Traits::identity(*cell.root), free);
        }
        drop(index);
        // Same-destination mutation in a destructor is outside this prototype.
        assert(empty(index));
        storage_->slots[index].cell = free;
        return true;
    }

    // Transactional move admission into a reserved, empty destination.
    // Capacity refusal leaves source untouched. No public destination exists.
    bool adoptEmpty(std::size_t index, Carrier&& source)
    {
        check(index);
        assert(empty(index));
        if (closing_)
            return false;
        const auto matched = matchingCell(Traits::identity(source));
        const auto free = matched != absent ? matched : freeCell();
        if (free == absent)
            return false;
        auto& cell = storage_->cells[free];
        if (matched != absent) {
            ++cell.aliases;
            storage_->slots[index].cell = free;
            // Capture before a release, including a genuinely moved-from input.
            Carrier consumed(std::move(source));
        } else {
            cell.root.emplace(std::move(source));
            cell.aliases = 1;
            cell.state = State::Active;
            storage_->freeCells.pop_back();
            insertIndex(storage_->index, Traits::identity(*cell.root), free);
            storage_->slots[index].cell = free;
        }
        return true;
    }

    const Carrier& inspectPrivate(std::size_t index) const { return carrier(index); }

    bool copy(std::size_t source, std::size_t destination)
    {
        check(source);
        check(destination);
        assert(!empty(source));
        if (closing_)
            return false;
        if (source == destination)
            return true;
        if (storage_->slots[source].boxed || storage_->slots[destination].boxed)
            return import(destination, carrier(source));
        const auto cellIndex = storage_->slots[source].cell;
        ++storage_->cells[cellIndex].aliases; // Retain the pending source before any callback.
        drop(destination);
        assert(empty(destination));
        storage_->slots[destination].cell = cellIndex;
        return true;
    }

    void moveToEmpty(std::size_t source, std::size_t destination)
    {
        check(source);
        check(destination);
        assert(!empty(source) && empty(destination) && !closing_);
        if (storage_->slots[source].boxed) {
            storage_->slots[destination].boxed.emplace(std::move(*storage_->slots[source].boxed));
            storage_->slots[source].boxed.reset();
        } else
            storage_->slots[destination].cell = std::exchange(storage_->slots[source].cell, absent);
    }

    Carrier copyOut(std::size_t index) const { return carrier(index); }

    Carrier takeOut(std::size_t index)
    {
        check(index);
        assert(!empty(index));
        auto& slot = storage_->slots[index];
        if (slot.boxed) {
            Carrier result(std::move(*slot.boxed));
            slot.boxed.reset();
            return result;
        }
        auto& cell = storage_->cells[slot.cell];
        if (cell.aliases != 1) {
            Carrier result(*cell.root);
            drop(index);
            return result;
        }
        eraseIndex(Traits::identity(*cell.root));
        const auto releasedCell = std::exchange(slot.cell, absent);
        Carrier result(std::move(*cell.root));
        cell.root.reset();
        cell.aliases = 0;
        cell.state = State::Free;
        storage_->freeCells.push_back(releasedCell);
        return result;
    }

    Carrier& materializeBorrow(std::size_t index)
    {
        check(index);
        assert(!empty(index));
        if (!storage_->slots[index].boxed) {
            auto result = takeOut(index);
            storage_->slots[index].boxed.emplace(std::move(result));
        }
        return *storage_->slots[index].boxed;
    }

    bool unique(std::size_t index) const
    {
        const auto& value = carrier(index);
        return Traits::unique(value)
            && (storage_->slots[index].boxed
                || storage_->cells[storage_->slots[index].cell].aliases == 1);
    }

    const void* identity(std::size_t index) const { return Traits::identity(carrier(index)); }

    // Boundary operation: expose separate ordinary Carrier objects, never roots.
    void materializeAll()
    {
        checkThread();
        for (std::size_t index = 0; index < slotLimit_; ++index)
            if (!empty(index))
                materializeBorrow(index);
    }

    std::size_t activeCells() const
    {
        checkThread();
        std::size_t count = 0;
        for (const auto& cell : storage_->cells)
            count += cell.state == State::Active;
        return count;
    }

    std::size_t retiringCells() const
    {
        checkThread();
        std::size_t count = 0;
        for (const auto& cell : storage_->cells)
            count += cell.state == State::Retiring;
        return count;
    }

    void drop(std::size_t index)
    {
        check(index);
        auto& slot = storage_->slots[index];
        if (slot.boxed) {
            // Detach before arbitrary payload destruction/reentry.
            std::optional<Carrier> dying(std::move(slot.boxed));
            slot.boxed.reset();
            dying.reset();
            return;
        }
        const auto cellIndex = std::exchange(slot.cell, absent);
        if (cellIndex == absent)
            return;
        auto& cell = storage_->cells[cellIndex];
        assert(cell.state == State::Active && cell.aliases != 0);
        if (--cell.aliases != 0)
            return;
        // Unpublish before payload destruction; Retiring must never match.
        eraseIndex(Traits::identity(*cell.root));
        cell.state = State::Retiring;
        std::optional<Carrier> dying(std::move(cell.root));
        cell.root.reset();
        dying.reset();
        cell.state = State::Free;
        storage_->freeCells.push_back(cellIndex);
    }

private:
    const Carrier& carrier(std::size_t index) const
    {
        check(index);
        const auto& slot = storage_->slots[index];
        if (slot.boxed)
            return *slot.boxed;
        assert(slot.cell != absent && storage_->cells[slot.cell].state == State::Active);
        return *storage_->cells[slot.cell].root;
    }

    static std::size_t indexCapacity(std::size_t cells) noexcept
    {
        // Active load <= 1/2 is an explicit occupancy invariant, not a guessed
        // performance parameter. Power-of-two overflow refuses growth.
        if (cells > std::numeric_limits<std::size_t>::max() / 2)
            return 0;
        const auto required = cells * 2;
        std::size_t buckets = 2;
        while (buckets < required) {
            if (buckets > std::numeric_limits<std::size_t>::max() / 2)
                return 0;
            buckets *= 2;
        }
        return buckets;
    }

    static std::size_t identityBucket(const void* identity, std::size_t buckets) noexcept
    {
        arp::core::StableHash64 hash;
        hash.appendU64(reinterpret_cast<std::uintptr_t>(identity));
        return static_cast<std::size_t>(hash.value()) & (buckets - 1);
    }

    static void insertIndex(
        arp::core::pmr::Vector<IndexEntry>& table, const void* identity, std::size_t cell) noexcept
    {
        const auto mask = table.size() - 1;
        auto position = identityBucket(identity, table.size());
        for (std::size_t visited = 0; visited < table.size(); ++visited) {
            auto& entry = table[position];
            assert(entry.cell == absent || entry.identity != identity);
            if (entry.cell == absent) {
                entry = {identity, cell};
                return;
            }
            position = (position + 1) & mask;
        }
        std::abort(); // Capacity invariant violation, never a fallback result.
    }

    void eraseIndex(const void* identity) noexcept
    {
        auto& table = storage_->index;
        const auto mask = table.size() - 1;
        auto hole = identityBucket(identity, table.size());
        std::size_t visited = 0;
        while (table[hole].cell != absent && table[hole].identity != identity) {
            hole = (hole + 1) & mask;
            if (++visited == table.size())
                std::abort();
        }
        assert(table[hole].cell != absent);
        if (table[hole].cell == absent)
            std::abort();
        // Close the probe-chain hole without tombstones or allocation. No
        // consumer retains entry addresses; cell/root addresses are unchanged.
        for (auto scan = (hole + 1) & mask; table[scan].cell != absent; scan = (scan + 1) & mask) {
            const auto home = identityBucket(table[scan].identity, table.size());
            if (((scan - home) & mask) >= ((scan - hole) & mask)) {
                table[hole] = table[scan];
                hole = scan;
            }
        }
        table[hole] = {};
    }

    std::size_t matchingCell(const void* identity) const noexcept
    {
        const auto& table = storage_->index;
        const auto mask = table.size() - 1;
        auto position = identityBucket(identity, table.size());
        for (std::size_t visited = 0; visited < table.size(); ++visited) {
#ifdef ARP_VM_REFERENCE_DIAGNOSTICS
            ++lookupProbes_;
#endif
            const auto& entry = table[position];
            if (entry.cell == absent)
                return absent;
            if (entry.identity == identity) {
                assert(
                    entry.cell < cellLimit_ && storage_->cells[entry.cell].state == State::Active);
                return entry.cell;
            }
            position = (position + 1) & mask;
        }
        std::abort();
    }

    std::size_t freeCell() const noexcept
    {
        return storage_->freeCells.empty() ? absent : storage_->freeCells.back();
    }

    void checkThread() const { assert(ownerThread_ == std::this_thread::get_id()); }

    void check(std::size_t index) const
    {
        checkThread();
        assert(ready() && index < slotLimit_);
    }

    std::optional<Storage> storage_;
    std::size_t cellLimit_ = 0, slotLimit_ = 0;
    Preparation preparation_ = Preparation::DeclinedAllocation;
    const std::thread::id ownerThread_ = std::this_thread::get_id();
    bool closing_ = false;
    mutable std::size_t lookupProbes_ = 0;
};

} // namespace arp::scriptvm::detail
