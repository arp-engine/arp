#pragma once
#include <array>
#include <cstdint>

namespace arp::shader {
class TrianglePositionBits final {
public:
    using Positions = std::array<std::array<std::uint32_t, 4>, 3>;
    // Exact IEEE-754 binary32 encodings of the accepted WGSL decimal literals.
    static constexpr std::uint32_t zero = 0x00000000u, one = 0x3f800000u;
    static constexpr std::uint32_t minusOne = 0xbf800000u, three = 0x40400000u;

    static constexpr Positions fullscreen() noexcept
    {
        return {{{minusOne, minusOne, zero, one},
            {three, minusOne, zero, one},
            {minusOne, three, zero, one}}};
    }
};
} // namespace arp::shader
