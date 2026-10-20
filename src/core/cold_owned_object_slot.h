#pragma once
#include "core/cpu_allocator.h"

#include <type_traits>

namespace arp::core {
// ColdOwnedObjectSlot/1. One borrowed cold receipt, then one allocation-free
// transfer of an original core-owned object. A receipt is an address comparison
// only: it neither dereferences nor retains a dead caller object and is not an
// identity/lifetime proof. The required caller validator must recheck original
// stable identity and semantics (including address reuse) before adoption.
template <class T> class ColdOwnedObjectSlot final {
public:
    ColdOwnedObjectSlot() = default;
    ColdOwnedObjectSlot(const ColdOwnedObjectSlot&) = delete;
    ColdOwnedObjectSlot& operator=(const ColdOwnedObjectSlot&) = delete;
    ColdOwnedObjectSlot(ColdOwnedObjectSlot&&) = delete;
    ColdOwnedObjectSlot& operator=(ColdOwnedObjectSlot&&) = delete;

    bool bindCold(const UniquePtr<T>& source) noexcept
    {
        if (!source || candidate_ || object_)
            return false;
        candidate_ = source.get();
        return true;
    }

    bool matchesCold(const T& candidate) const noexcept
    {
        return !object_ && candidate_ == &candidate;
    }

    template <class Validate> bool adoptOriginal(UniquePtr<T>& source, Validate&& validate) noexcept
    {
        static_assert(std::is_nothrow_invocable_r_v<bool, Validate, const T&>);
        if (!source || !matchesCold(*source) || !validate(*source))
            return false;
        object_ = std::move(source);
        candidate_ = nullptr;
        return true;
    }

    const T* originalObject() const noexcept { return object_.get(); }

private:
    UniquePtr<T> object_;
    const T* candidate_ = nullptr;
};
} // namespace arp::core
