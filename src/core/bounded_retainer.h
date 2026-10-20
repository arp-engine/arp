#pragma once

#include "core/cpu_allocator.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace arp::core {

// Reserve during preparation, retain without allocating during recording.
// Object identity is pointer identity; duplicates do not consume capacity.
template <class T> class BoundedRetainer final {
public:
    explicit BoundedRetainer(std::size_t capacity = 0) : capacity_(capacity)
    {
        values_.reserve(capacity);
    }

    ~BoundedRetainer() { clear(); }

    BoundedRetainer(BoundedRetainer&& other) noexcept
        : capacity_(std::exchange(other.capacity_, 0)), values_(std::move(other.values_))
    {}

    BoundedRetainer& operator=(BoundedRetainer&& other) noexcept
    {
        if (this != &other) {
            clear();
            capacity_ = std::exchange(other.capacity_, 0);
            values_ = std::move(other.values_);
        }
        return *this;
    }

    BoundedRetainer(const BoundedRetainer&) = delete;

    bool contains(const std::shared_ptr<const T>& object) const noexcept
    {
        return std::find(values_.begin(), values_.end(), object) != values_.end();
    }

    bool canRetain(const std::shared_ptr<const T>& object) const noexcept
    {
        return object && (contains(object) || values_.size() < capacity_);
    }

    bool retain(const std::shared_ptr<const T>& object)
    {
        if (!canRetain(object))
            return false;
        if (!contains(object))
            values_.push_back(object);
        return true;
    }

    // Preflight an entire owner batch so rejection cannot partially retain it.
    // Pointer identity is unchanged from retain(); adapters validate provenance.
    // Map immutable indexed metadata directly to owners, without constructing
    // a temporary owner array. The adapter supplies a deterministic,
    // allocation-free, nonthrowing projection over serialized immutable input.
    // Mapping costs remain adapter-owned. If that precondition is violated
    // during commit, false may follow partial retention; execution must fail
    // terminally rather than treat it as an ordinary capacity rejection.
    template <class OwnerAt>
    bool canRetainMapped(std::size_t count, OwnerAt&& ownerAt) const noexcept
    {
        static_assert(noexcept(ownerAt(std::size_t{})), "Owner projection must not throw");
        static_assert(std::is_same_v<std::decay_t<decltype(ownerAt(std::size_t{}))>,
                          std::shared_ptr<const T>>,
            "Owner projection must expose exact shared owners");
        std::size_t additional = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            const auto object = ownerAt(i);
            if (!object)
                return false;
            if (contains(object))
                continue;
            bool duplicate = false;
            for (std::size_t j = 0u; j < i; ++j)
                if (ownerAt(j) == object) {
                    duplicate = true;
                    break;
                }
            if (!duplicate && ++additional > capacity_ - values_.size())
                return false;
        }
        return true;
    }

    template <class OwnerAt> bool retainMapped(std::size_t count, OwnerAt&& ownerAt)
    {
        if (!canRetainMapped(count, ownerAt))
            return false;
        for (std::size_t i = 0u; i < count; ++i)
            if (!retain(ownerAt(i)))
                return false;
        return true;
    }

    bool canRetainAll(const std::shared_ptr<const T>* objects, std::size_t count) const noexcept
    {
        return (!count || objects)
            && canRetainMapped(
                count, [objects](std::size_t index) noexcept -> const std::shared_ptr<const T>& {
                    return objects[index];
                });
    }

    bool retainAll(const std::shared_ptr<const T>* objects, std::size_t count)
    {
        return (!count || objects)
            && retainMapped(
                count, [objects](std::size_t index) noexcept -> const std::shared_ptr<const T>& {
                    return objects[index];
                });
    }

    const pmr::Vector<std::shared_ptr<const T>>& values() const noexcept { return values_; }

    // Completion/abandonment releases ownership but preserves the reservation.
    // Backing owners admitted first outlive dependent owners admitted later.
    // Do not depend on the standard library's vector destruction order.
    void clear() noexcept
    {
        while (!values_.empty())
            values_.pop_back();
    }

private:
    std::size_t capacity_;
    pmr::Vector<std::shared_ptr<const T>> values_{&defaultCpuMemoryResource()};
};

} // namespace arp::core
