#pragma once

// Storage types and variant vtable.
//
// The variant uses a small-buffer-optimization (SBO): an inline buffer the size
// of a single pointer. Types that fit are stored directly (stack policy);
// larger types heap-allocate and store the pointer (heap policy); arrays use
// the array policy. The vtable is a struct of 6 function pointers stored BY
// VALUE inside variant (not a pointer to a static instance).

#include "core/api.h"

#include <cstddef>
#include <type_traits>
#include <typeinfo>

namespace arp::reflection {

class typeinfo;

// Inline storage: sizeof(void*) = 8 bytes on 64-bit.
// Small types (int, double, bool, int64_t, pointers) are stored here directly.
// Large types store a heap pointer here.
using storage_t = std::aligned_storage_t<sizeof(void*), alignof(void*)>;

template <typename T>
constexpr bool isStackAllocV = sizeof(T) <= sizeof(void*) && alignof(T) <= alignof(void*);

struct variant_vtable {
    const std::type_info* ctype = nullptr;
    const typeinfo* (*typefunc)() = nullptr;
    bool (*enumToInt)(storage_t&, int&) = nullptr;
    void (*destroy)(storage_t&) = nullptr;
    void (*copy)(storage_t&, const storage_t&) = nullptr;
    void (*move)(storage_t&, storage_t&&) = nullptr;
    void* (*ptr)(storage_t&) = nullptr;
};

} // namespace arp::reflection
