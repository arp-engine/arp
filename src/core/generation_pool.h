#pragma once
#include "core/cpu_allocator.h"
#include "core/monotonic_id_sequence.h"

#include <limits>
#include <optional>

namespace arp::core {
namespace detail {
inline MonotonicIdSequence& poolHandleSequence()
{
    static MonotonicIdSequence sequence;
    return sequence;
}
} // namespace detail

struct PoolHandle {
    const void* owner = nullptr;
    std::uint32_t index = 0;
    std::uint64_t generation = 0;

    explicit operator bool() const { return owner && generation; }

    bool operator==(const PoolHandle& h) const
    {
        return owner == h.owner && index == h.index && generation == h.generation;
    }
};

// Fixed capacity, single owner thread. Inline payloads; no allocation after
// construction unless T itself allocates. Clear invalidates every live ticket.
template <class T> class GenerationPool final {
public:
    explicit GenerationPool(std::uint32_t capacity)
    {
        slots_.resize(capacity);
        free_.reserve(capacity);
        for (std::uint32_t i = capacity; i > 0; --i)
            free_.push_back(i - 1);
    }

    GenerationPool(const GenerationPool&) = delete;
    GenerationPool& operator=(const GenerationPool&) = delete;

    template <class... Args> PoolHandle emplace(Args&&... args)
    {
        diagnostic_ = nullptr;
        if (free_.empty()) {
            diagnostic_ = "CORE-POOL-CAPACITY";
            return {};
        }
        const auto generation = detail::poolHandleSequence().tryAcquire();
        if (!generation) {
            diagnostic_ = "CORE-POOL-GENERATION-EXHAUSTED";
            return {};
        }
        auto index = free_.back();
        auto& slot = slots_[index];
        slot.value.emplace(std::forward<Args>(args)...);
        slot.generation = generation;
        free_.pop_back();
        ++live_;
        return {this, index, slot.generation};
    }

    T* get(PoolHandle handle)
    {
        if (handle.owner != this || handle.index >= slots_.size())
            return nullptr;
        auto& slot = slots_[handle.index];
        return slot.value && slot.generation == handle.generation ? &*slot.value : nullptr;
    }

    const T* get(PoolHandle h) const { return const_cast<GenerationPool*>(this)->get(h); }

    bool erase(PoolHandle handle)
    {
        if (!get(handle))
            return false;
        auto& slot = slots_[handle.index];
        slot.value.reset();
        --live_;
        slot.generation = 0;
        free_.push_back(handle.index);
        return true;
    }

    template <class Function> void forEach(Function&& function)
    {
        for (std::uint32_t i = 0; i < slots_.size(); ++i) {
            auto& slot = slots_[i];
            if (slot.value)
                function(PoolHandle{this, i, slot.generation}, *slot.value);
        }
    }

    void clear()
    {
        forEach([&](PoolHandle h, T&) {
            erase(h);
        });
    }

    template <class Function> void forEach(Function&& function) const
    {
        for (std::uint32_t i = 0; i < slots_.size(); ++i) {
            const auto& slot = slots_[i];
            if (slot.value)
                function(PoolHandle{this, i, slot.generation}, *slot.value);
        }
    }

    std::uint32_t liveCount() const { return live_; }

    // Cold inspection only. Both vectors have distinct owning allocations.
    template <class Inspector> void inspectStorage(Inspector& inspector) const noexcept
    {
        inspector.add(slots_.empty() ? nullptr : slots_.data());
        inspector.add(free_.capacity() ? free_.data() : nullptr);
    }

    const char* diagnostic() const { return diagnostic_; }

private:
    struct Slot {
        std::optional<T> value;
        std::uint64_t generation = 0;
    };

    core::pmr::Vector<Slot> slots_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint32_t> free_{&core::defaultCpuMemoryResource()};
    std::uint32_t live_ = 0;
    const char* diagnostic_ = nullptr;
};
} // namespace arp::core
