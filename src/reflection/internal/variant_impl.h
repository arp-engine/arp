#pragma once

// variant template implementation.
//
// Key design decisions (reverted to match the original):
// 1. vtable_ is a VALUE member (not a pointer to a static instance).
// 2. is<T>() uses *vtable_.ctype == typeid(T) -- no null check (default ctor
//    sets ctype = &typeid(void)).
// 3. cast<T>() is a direct reinterpret_cast through vtable_.ptr -- no is<T>()
//    check (caller's responsibility).
// 4. Three storage policies: stack (T fits in void*), heap (T too large),
//    array (T[N]).

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/internal/type_name.h"
#include "reflection/type.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace arp::reflection {
namespace detail {

template <typename T, typename = void> struct has_source_allocator_copy : std::false_type {};

template <typename T>
struct has_source_allocator_copy<T,
    std::void_t<typename T::allocator_type, decltype(std::declval<const T&>().get_allocator())>>
    : std::bool_constant<std::is_constructible_v<T, const T&, typename T::allocator_type>> {};

template <typename T> T copyWithSourceAllocator(const T& value)
{
    if constexpr (has_source_allocator_copy<T>::value) {
        return T(value, value.get_allocator());
    } else {
        return T(value);
    }
}

// ---------------------------------------------------------------------------
// Heap policy: allocate through the engine PMR resource and store the pointer
// in the inline buffer so reflected Values participate in CpuMemoryQuota.
// ---------------------------------------------------------------------------
template <typename T> struct variant_heap_policy {
    static T* allocate(const T& value)
    {
        std::pmr::polymorphic_allocator<T> allocator{&core::defaultCpuMemoryResource()};
        T* result = allocator.allocate(1);
        try {
            ::new (static_cast<void*>(result)) T(copyWithSourceAllocator(value));
        } catch (...) {
            allocator.deallocate(result, 1);
            throw;
        }
        return result;
    }

    static T* allocate(T&& value)
    {
        std::pmr::polymorphic_allocator<T> allocator{&core::defaultCpuMemoryResource()};
        T* result = allocator.allocate(1);
        try {
            ::new (static_cast<void*>(result)) T(std::move(value));
        } catch (...) {
            allocator.deallocate(result, 1);
            throw;
        }
        return result;
    }

    static void destroyValue(T* value) noexcept
    {
        if (value == nullptr)
            return;
        value->~T();
        std::pmr::polymorphic_allocator<T> allocator{&core::defaultCpuMemoryResource()};
        allocator.deallocate(value, 1);
    }

    static void construct(storage_t& data, const T& val)
    {
        reinterpret_cast<T*&>(data) = allocate(val);
    }

    static void construct(storage_t& data, T&& val)
    {
        reinterpret_cast<T*&>(data) = allocate(std::move(val));
    }

    static void destroy(storage_t& data)
    {
        destroyValue(reinterpret_cast<T*&>(data));
        reinterpret_cast<T*&>(data) = nullptr;
    }

    static void copy(storage_t& data, const storage_t& other)
    {
        variant_heap_policy::construct(data, *reinterpret_cast<T* const&>(other));
    }

    static void move(storage_t& data, storage_t&& other)
    {
        T*& dst = reinterpret_cast<T*&>(data);
        T*& src = reinterpret_cast<T*&>(other);
        dst = src;
        src = nullptr;
    }

    static void* ptr(storage_t& data) { return reinterpret_cast<void*&>(data); }
};

// ---------------------------------------------------------------------------
// Stack policy: placement-new T into the inline buffer.
// ---------------------------------------------------------------------------
template <typename T> struct variant_stack_policy {
    static void construct(storage_t& data, const T& val) { new (&data) T(val); }

    static void construct(storage_t& data, T&& val) noexcept { new (&data) T(std::move(val)); }

    static void destroy(storage_t& data) { reinterpret_cast<T&>(data).~T(); }

    static void copy(storage_t& data, const storage_t& other)
    {
        new (&data) T(reinterpret_cast<const T&>(other));
    }

    static void move(storage_t& data, storage_t&& other)
    {
        new (&data) T(std::move(reinterpret_cast<T&>(other)));
    }

    static void* ptr(storage_t& data) { return reinterpret_cast<void*>(&data); }
};

// ---------------------------------------------------------------------------
// Array policy: T[N]
// ---------------------------------------------------------------------------
template <typename T> constexpr bool isStackArrayV = isStackAllocV<T> && std::is_trivial_v<T>;

template <typename T, typename = void> struct variant_array_policy;

// Trivial array -- fits in inline storage.
template <typename T, std::size_t N>
struct variant_array_policy<T[N], std::enable_if_t<isStackArrayV<T[N]>>> {
    static void construct(storage_t& data, const T (&val)[N])
    {
        std::copy(std::begin(val), std::end(val), reinterpret_cast<T(&)[N]>(data));
    }

    static void destroy(storage_t&) {}

    static void copy(storage_t& data, const storage_t& other)
    {
        auto& src = reinterpret_cast<const T(&)[N]>(other);
        std::copy(std::begin(src), std::end(src), reinterpret_cast<T(&)[N]>(data));
    }

    static void move(storage_t& data, storage_t&& other)
    {
        auto& src = reinterpret_cast<T(&)[N]>(other);
        std::copy(std::begin(src), std::end(src), reinterpret_cast<T(&)[N]>(data));
    }

    static void* ptr(storage_t& data) { return reinterpret_cast<void*>(&data); }
};

// Non-trivial array -- allocated through the engine PMR resource so each
// element participates in the same quota and ownership contract as scalars.
template <typename T, std::size_t N>
struct variant_array_policy<T[N], std::enable_if_t<!isStackArrayV<T[N]>>> {
    static void construct(storage_t& data, const T (&val)[N])
    {
        std::pmr::polymorphic_allocator<T> allocator{&core::defaultCpuMemoryResource()};
        T* ptr = allocator.allocate(N);
        std::size_t constructed = 0u;
        try {
            for (; constructed < N; ++constructed)
                ::new (static_cast<void*>(ptr + constructed)) T();
            for (std::size_t index = 0u; index < N; ++index)
                ptr[index] = val[index];
        } catch (...) {
            while (constructed != 0u) {
                --constructed;
                (ptr + constructed)->~T();
            }
            allocator.deallocate(ptr, N);
            throw;
        }
        reinterpret_cast<T*&>(data) = ptr;
    }

    static void destroy(storage_t& data)
    {
        T* ptr = reinterpret_cast<T*&>(data);
        if (ptr == nullptr)
            return;
        for (std::size_t index = N; index != 0u; --index)
            (ptr + index - 1u)->~T();
        std::pmr::polymorphic_allocator<T> allocator{&core::defaultCpuMemoryResource()};
        allocator.deallocate(ptr, N);
        reinterpret_cast<T*&>(data) = nullptr;
    }

    static void copy(storage_t& data, const storage_t& other)
    {
        auto src = reinterpret_cast<T* const&>(other);
        std::pmr::polymorphic_allocator<T> allocator{&core::defaultCpuMemoryResource()};
        T* dst = allocator.allocate(N);
        std::size_t constructed = 0u;
        try {
            for (; constructed < N; ++constructed)
                ::new (static_cast<void*>(dst + constructed)) T();
            for (std::size_t index = 0u; index < N; ++index)
                dst[index] = src[index];
        } catch (...) {
            while (constructed != 0u) {
                --constructed;
                (dst + constructed)->~T();
            }
            allocator.deallocate(dst, N);
            throw;
        }
        reinterpret_cast<T*&>(data) = dst;
    }

    static void move(storage_t& data, storage_t&& other)
    {
        reinterpret_cast<T*&>(data) = reinterpret_cast<T*&>(other);
        reinterpret_cast<T*&>(other) = nullptr;
    }

    static void* ptr(storage_t& data) { return reinterpret_cast<void*&>(data); }
};

// Policy selector: array -> array_policy, stack -> stack_policy, else heap_policy.
template <typename T, typename Raw = std::remove_cv_t<std::remove_reference_t<T>>>
using variant_policy_default = std::conditional_t<std::is_array_v<Raw>,
    variant_array_policy<Raw, void>,
    std::conditional_t<isStackAllocV<Raw>, variant_stack_policy<Raw>, variant_heap_policy<Raw>>>;

template <typename T> bool enumToIntValue(storage_t& data, int& value)
{
    using Raw = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_enum_v<Raw>) {
        using Policy = variant_policy_default<Raw>;
        const auto* stored = static_cast<const Raw*>(Policy::ptr(data));
        if (stored == nullptr)
            return false;
        value = static_cast<int>(*stored);
        return true;
    }
    return false;
}

} // namespace detail

// ---------------------------------------------------------------------------
// variant template constructor -- fills vtable_ value fields + constructs data.
// ---------------------------------------------------------------------------
template <typename T,
    std::enable_if_t<!std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, variant>, int>>
variant::variant(T&& value)
{
    using Policy = detail::variant_policy_default<T>;

    vtable_.ctype = &typeid(T);
    vtable_.typefunc = &type_of<std::remove_reference_t<T>>;
    vtable_.enumToInt = &detail::enumToIntValue<T>;
    vtable_.destroy = &Policy::destroy;
    vtable_.copy = &Policy::copy;
    vtable_.move = &Policy::move;
    vtable_.ptr = &Policy::ptr;

    Policy::construct(data_, std::forward<T>(value));
}

// ---------------------------------------------------------------------------
// variant::is<T>() -- no null check; default ctor sets ctype = &typeid(void).
// ---------------------------------------------------------------------------
template <typename T> constexpr bool variant::is() const
{
    // Undefined is represented by the default vtable, whose typefunc is the
    // canonical type_of<void> entry. Do not fall through to type_of<void>()
    // while builtin reflection registration is still in progress: that would
    // recursively enter the same function-local type cache and deadlock.
    if constexpr (std::is_void_v<std::remove_cv_t<T>>) {
        return vtable_.typefunc == &type_of<void>;
    }
    // Web/Wasm builds intentionally disable C++ RTTI. In that mode typeid
    // comparisons are not a stable identity for distinct reflected classes
    // and can collapse script carriers (for example ReflectedMethod and
    // ReflectedTypeBinding). ARP's generated typeinfo is the portable
    // identity; retain the RTTI check as a cheap fast path where available.
    return (*vtable_.ctype == typeid(T))
        || (vtable_.typefunc != nullptr && vtable_.typefunc() == type_of<T>());
}

// ---------------------------------------------------------------------------
// variant::cast<T>() -- direct reinterpret_cast, no is<T>() check.
// Caller's responsibility to ensure the type matches.
// ---------------------------------------------------------------------------
template <typename T> const T* variant::exactPayloadPointer() const noexcept
{
    static_assert(!std::is_void_v<T> && !std::is_reference_v<T> && !std::is_array_v<T>);
    if (vtable_.typefunc != &type_of<T>)
        return nullptr;
    // Typed construction binds this selector to its storage policy; copy,
    // move and swap carry that vtable together with the payload. The pointer
    // therefore denotes the actual owned object, never a converted temporary.
    using Policy = detail::variant_policy_default<T>;
    return static_cast<const T*>(Policy::ptr(*const_cast<storage_t*>(&data_)));
}

template <typename T>
constexpr const std::enable_if_t<!std::is_array_v<T>, T*> variant::cast() const
{
    return reinterpret_cast<T*>(vtable_.ptr(*const_cast<storage_t*>(&data_)));
}

template <typename T>
constexpr const std::enable_if_t<std::is_array_v<T>, std::decay_t<T>> variant::cast() const
{
    return cast<std::remove_pointer_t<std::decay_t<T>>>();
}

} // namespace arp::reflection
