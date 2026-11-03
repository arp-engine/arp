#pragma once

#include <cstdint>
#include <string_view>

namespace arp::editor::detail {
// Callers bound both credential lengths before accepting an HTTP identity.
inline bool constantTimeEquals(std::string_view lhs, std::string_view rhs) noexcept
{
    std::uint32_t difference = static_cast<std::uint32_t>(lhs.size() ^ rhs.size());
    for (std::size_t i = 0; i < 256; ++i) {
        const auto left = i < lhs.size() ? static_cast<unsigned char>(lhs[i]) : 0u;
        const auto right = i < rhs.size() ? static_cast<unsigned char>(rhs[i]) : 0u;
        difference |= left ^ right;
    }
    return difference == 0;
}

} // namespace arp::editor::detail
