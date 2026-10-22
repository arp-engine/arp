#pragma once

#include "core/memory/cpu_allocator.h"

#include <algorithm>
#include <utility>

namespace arp::core {

// Serialized-owner record storage. Reserve once; enforce the declared count
// even when vector capacity is larger. Element construction has its own costs.
template <class T> class BoundedSequence final {
public:
    explicit BoundedSequence(std::size_t capacity = 0u) : capacity_(capacity)
    {
        values_.reserve(capacity);
    }

    BoundedSequence(BoundedSequence&& other) noexcept
        : capacity_(std::exchange(other.capacity_, 0u)), values_(std::move(other.values_))
    {}

    BoundedSequence& operator=(BoundedSequence&& other) noexcept
    {
        if (this != &other) {
            capacity_ = std::exchange(other.capacity_, 0u);
            values_ = std::move(other.values_);
        }
        return *this;
    }

    BoundedSequence(const BoundedSequence&) = delete;

    bool canAppend(std::size_t count = 1u) const noexcept
    {
        return count <= capacity_ - values_.size();
    }

    bool append(const T& value)
    {
        if (!canAppend())
            return false;
        values_.push_back(value);
        return true;
    }

    bool append(T&& value)
    {
        if (!canAppend())
            return false;
        values_.push_back(std::move(value));
        return true;
    }

    bool contains(const T& value) const
    {
        return std::find(values_.begin(), values_.end(), value) != values_.end();
    }

    // Read-only lookup preserves the bounded storage/identity contract. The
    // pointer remains valid until append, clear, move or sequence destruction.
    template <class Predicate> const T* findIf(Predicate&& predicate) const
    {
        const auto found =
            std::find_if(values_.begin(), values_.end(), std::forward<Predicate>(predicate));
        return found == values_.end() ? nullptr : &*found;
    }

    template <class Predicate> const T* findLastIf(Predicate&& predicate) const
    {
        const auto found =
            std::find_if(values_.rbegin(), values_.rend(), std::forward<Predicate>(predicate));
        return found == values_.rend() ? nullptr : &*found;
    }

    const pmr::Vector<T>& values() const noexcept { return values_; }

    // Update existing element fields without exposing the vector's capacity or
    // allowing a caller to bypass the declared append bound.
    template <class Visitor> void visitMutable(Visitor&& visitor)
    {
        for (auto& value : values_)
            visitor(value);
    }

    void clear() noexcept { values_.clear(); }

private:
    std::size_t capacity_;
    pmr::Vector<T> values_{&defaultCpuMemoryResource()};
};

} // namespace arp::core
