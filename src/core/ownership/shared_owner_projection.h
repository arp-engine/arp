#pragma once
#include "core/memory/cpu_allocator.h"

#include <memory>

namespace arp::core {

// Cold capture of a serialized immutable ownership graph; hot observation never
// materializes a second vector. Pointer and control-block identity both matter.
class SharedOwnerProjection final {
public:
    SharedOwnerProjection() = default;

    // Observe typed handles by reference. Converting to shared_ptr<void> just
    // to compare identities would retain/release both control blocks on every
    // hot validation; owner_before supports different pointee types directly.
    template <class A, class B>
    static bool sameControlBlock(const std::shared_ptr<A>& a, const std::shared_ptr<B>& b) noexcept
    {
        return !a.owner_before(b) && !b.owner_before(a);
    }

    template <class A, class B>
    static bool sameOwner(const std::shared_ptr<A>& a, const std::shared_ptr<B>& b) noexcept
    {
        return static_cast<const volatile void*>(a.get())
            == static_cast<const volatile void*>(b.get())
            && sameControlBlock(a, b);
    }

    template <class A, class B>
    static bool overlapsOwner(const std::shared_ptr<A>& a, const std::shared_ptr<B>& b) noexcept
    {
        return a && b
            && (static_cast<const volatile void*>(a.get())
                    == static_cast<const volatile void*>(b.get())
                || sameControlBlock(a, b));
    }

    // Preserve the existing erased interface for callers that store it.
    static bool sameControlBlock(
        const std::shared_ptr<const void>& a, const std::shared_ptr<const void>& b) noexcept
    {
        return sameControlBlock<const void, const void>(a, b);
    }

    static bool sameOwner(
        const std::shared_ptr<const void>& a, const std::shared_ptr<const void>& b) noexcept
    {
        return sameOwner<const void, const void>(a, b);
    }

    static bool overlapsOwner(
        const std::shared_ptr<const void>& a, const std::shared_ptr<const void>& b) noexcept
    {
        return overlapsOwner<const void, const void>(a, b);
    }

    // Read-only, allocation-free independence check over a stable serialized
    // projection. Both the object and its owning control block must be distinct.
    // Missing/non-owning aliases and forged owners of the same object reject.
    template <class At> static bool hasIndependentOwners(std::size_t count, At&& at) noexcept
    {
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& current = at(i);
            if (!current || !current.use_count())
                return false;
            for (std::size_t j = 0u; j < i; ++j) {
                const auto& previous = at(j);
                if (current.get() == previous.get() || sameControlBlock(current, previous))
                    return false;
            }
        }
        return true;
    }

    SharedOwnerProjection(const SharedOwnerProjection&) = delete;
    SharedOwnerProjection& operator=(const SharedOwnerProjection&) = delete;

    template <class Projector> bool capture(Projector&& project)
    {
        if (captured_)
            return false;
        pmr::Vector<std::shared_ptr<const void>> pending{&defaultCpuMemoryResource()};
        project([&](std::shared_ptr<const void> owner) {
            if (owner)
                pending.push_back(std::move(owner));
        });
        owners_ = std::move(pending);
        captured_ = true;
        return true;
    }

    template <class Projector> bool matches(Projector&& project) const
    {
        if (!captured_)
            return false;
        std::size_t index = 0;
        bool equal = true;
        project([&](const std::shared_ptr<const void>& owner) {
            if (!owner)
                return;
            if (index >= owners_.size() || !sameOwner(owner, owners_[index]))
                equal = false;
            ++index;
        });
        return equal && index == owners_.size();
    }

    std::size_t size() const noexcept { return owners_.size(); }

    std::shared_ptr<const void> at(std::size_t i) const noexcept
    {
        return i < owners_.size() ? owners_[i] : nullptr;
    }

private:
    pmr::Vector<std::shared_ptr<const void>> owners_{&defaultCpuMemoryResource()};
    bool captured_ = false;
};
} // namespace arp::core
