#pragma once
#include <cstdint>
#include <limits>
#include <type_traits>

namespace arp::core {
template <class To, class From> constexpr bool checkedIntegerCast(From value, To& output) noexcept
{
    static_assert(std::is_integral_v<To> && std::is_integral_v<From>);
    static_assert(sizeof(To) <= sizeof(std::uintmax_t) && sizeof(From) <= sizeof(std::uintmax_t));
    if constexpr (std::is_signed_v<From>) {
        if (value < 0) {
            if constexpr (!std::is_signed_v<To>)
                return false;
            else if (static_cast<std::intmax_t>(value)
                < static_cast<std::intmax_t>(std::numeric_limits<To>::lowest()))
                return false;
            output = static_cast<To>(value);
            return true;
        }
    }
    if (static_cast<std::uintmax_t>(value)
        > static_cast<std::uintmax_t>(std::numeric_limits<To>::max()))
        return false;
    output = static_cast<To>(value);
    return true;
}
} // namespace arp::core
