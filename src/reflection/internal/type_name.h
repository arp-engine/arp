#pragma once

// Type name extraction via __PRETTY_FUNCTION__.
//
// The original used compiler-specific string-slicing on __PRETTY_FUNCTION__:
//   GCC/Clang: "const char* arp::reflection::detail::f() [with T = TYPE]"
//   MSVC:      "const char *__cdecl arp::reflection::detail::f<T>(void) noexcept"
// We keep the same approach but change the namespace in the signature prefix/suffix
// to match arp::reflection.

#include "core/api.h"

#include <cstddef>
#include <cstring>
#include <string_view>

namespace arp::reflection::detail {

#if defined(_MSC_VER)
constexpr std::size_t skipAtBegin = sizeof("const char *__cdecl arp::reflection::detail::f<") - 2;
constexpr std::size_t skipAtEnd = sizeof(">(void) noexcept") - 2;
#elif defined(__GNUC__) || defined(__clang__)
constexpr std::size_t skipAtBegin =
    sizeof("const char* arp::reflection::detail::f() [with T = ") - 2;
constexpr std::size_t skipAtEnd = sizeof("]") - 2;
#else
#error "Don't know how to extract type signature for this compiler!"
#endif

inline const char* extractTypeSignature(const char* signature) noexcept
{
    return &signature[skipAtBegin];
}

template <typename T> inline const char* typeSignatureFn() noexcept
{
    return extractTypeSignature(
#if defined(_MSC_VER)
        __FUNCSIG__
#elif defined(__GNUC__) || defined(__clang__)
        __PRETTY_FUNCTION__
#else
#error "Unsupported compiler"
#endif
    );
}

template <typename T> inline std::string_view getTypeName() noexcept
{
    const char* s = typeSignatureFn<T>();
    return std::string_view(s, std::char_traits<char>::length(s) - skipAtEnd);
}

} // namespace arp::reflection::detail
