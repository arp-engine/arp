#pragma once

#include "core/memory/cpu_allocator.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory_resource>
#include <string>
#include <system_error>
#include <type_traits>

namespace arp::core {

// Integer serialization used by canonical contracts and filesystem identities.
// The fixed buffer is intentionally larger than the decimal representation of
// every supported integer, so conversion cannot fail for capacity.
template <typename Integer> void appendUnsigned(pmr::String& output, Integer value)
{
    static_assert(std::is_integral_v<Integer>);
    char buffer[64]{};
    const auto converted = std::to_chars(std::begin(buffer), std::end(buffer), value);
    if (converted.ec != std::errc{})
        return;
    output.append(buffer, static_cast<std::size_t>(converted.ptr - buffer));
}

template <typename Integer> pmr::String formatUnsigned(Integer value)
{
    static_assert(std::is_unsigned_v<Integer>);
    pmr::String output{&defaultCpuMemoryResource()};
    appendUnsigned(output, value);
    return output;
}

// Locale-independent round-trip text. Keep a decimal/exponent marker so YAML
// readers retain the floating-point type even for integral values and -0.
template <typename Float> bool appendFloating(pmr::String& output, Float value)
{
    static_assert(std::is_floating_point_v<Float>);
    if (!std::isfinite(value))
        return false;
    char buffer[std::numeric_limits<Float>::max_digits10 + 16]{};
    const auto converted = std::to_chars(std::begin(buffer),
        std::end(buffer),
        value,
        std::chars_format::general,
        std::numeric_limits<Float>::max_digits10);
    if (converted.ec != std::errc{})
        return false;
    const std::string_view text(buffer, static_cast<std::size_t>(converted.ptr - buffer));
    output.append(text);
    if (text.find_first_of(".eE") == std::string_view::npos)
        output += ".0";
    return true;
}

} // namespace arp::core
