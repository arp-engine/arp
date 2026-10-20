#pragma once

#include <charconv>
#include <cstddef>
#include <iterator>
#include <memory_resource>
#include <string>
#include <system_error>
#include <type_traits>

namespace arp::core {

// Integer serialization used by canonical contracts and filesystem identities.
// The fixed buffer is intentionally larger than the decimal representation of
// every supported integer, so conversion cannot fail for capacity.
template <typename Integer> void appendUnsigned(std::pmr::string& output, Integer value)
{
    static_assert(std::is_integral_v<Integer>);
    char buffer[64]{};
    const auto converted = std::to_chars(std::begin(buffer), std::end(buffer), value);
    if (converted.ec != std::errc{})
        return;
    output.append(buffer, static_cast<std::size_t>(converted.ptr - buffer));
}

} // namespace arp::core
