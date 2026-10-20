#pragma once

#include "core/cpu_allocator.h"

#include <cstdint>
#include <cstdio>
#include <string_view>

namespace arp::resource {

/// Deterministically derives a UUID-shaped resource id from an authored alias.
/// Use this only when the alias itself is the immutable resource identity.
inline core::pmr::String makeResourceUuidForAlias(std::string_view alias)
{
    std::uint64_t primary = 1469598103934665603ull;
    for (const auto value : alias) {
        primary ^= static_cast<std::uint8_t>(value);
        primary *= 1099511628211ull;
    }
    const auto secondary = primary ^ 0xC2B2AE3D27D4EB4Full;
    char text[40]{};
    std::snprintf(text,
        sizeof(text),
        "%08llx-%04llx-%04llx-%04llx-%012llx",
        static_cast<unsigned long long>(primary >> 32u),
        static_cast<unsigned long long>((primary >> 16u) & 0xffffull),
        static_cast<unsigned long long>(primary & 0xffffull),
        static_cast<unsigned long long>((secondary >> 48u) & 0xffffull),
        static_cast<unsigned long long>(secondary & 0xffffffffffffull));
    return core::pmr::String(text, &core::defaultCpuMemoryResource());
}

} // namespace arp::resource
