#pragma once
#include "core/cpu_allocator.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace arp::test {
class OriginalGraphicsColorGiDataFixture final {
public:
    static constexpr std::size_t wordsPerPixel = 19u, pixels = 4u;
    static constexpr std::uint32_t resultBinding = 3u;
    static constexpr const char* packageName = "GraphicsColorGi";
    using Numeric = std::array<float, wordsPerPixel * pixels>;

    static void initializeTexturePixels(
        std::uint32_t index, std::uint32_t layers, core::pmr::Vector<std::uint8_t>& pixels)
    {
        assert(pixels.size() == 20u * layers);
        std::fill(pixels.begin(), pixels.end(), 0u);
        for (std::uint32_t layer = 0u; layer < layers; ++layer) {
            for (std::uint32_t texel = 0u; texel < 4u; ++texel) {
                const auto offset = layer * 16u + texel * 4u;
                pixels[offset] =
                    static_cast<std::uint8_t>((index + 1u) * (16u * (texel + 1u) + 8u * layer));
                pixels[offset + 3u] = 255u;
            }
            const auto offset = 16u * layers + layer * 4u;
            pixels[offset] = static_cast<std::uint8_t>((index + 1u) * (32u + 8u * layer));
            pixels[offset + 3u] = 255u;
        }
    }

    static auto expected() noexcept
    {
        std::array<float, wordsPerPixel> values{};
        for (std::uint32_t face = 0u; face < 6u; ++face) {
            values[face * 2u] = 17.f + float(40u + 8u * face) / 255.f;
            values[face * 2u + 1u] = 17.f + float(32u + 8u * face) / 255.f;
        }
        values[12] = 17.f + 32.f / 255.f;
        values[13] = 17.f + 64.f / 255.f;
        values[14] = 17.f + 48.f / 255.f;
        values[15] = 17.f + 96.f / 255.f;
        values[16] = 17.f + 72.f / 255.f;
        values[17] = 17.f + 120.f / 255.f;
        values[18] = values[0];
        return values;
    }

    static void verify(const Numeric& actual)
    {
        const auto values = expected();
        for (std::size_t i = 0u; i < actual.size(); ++i) {
            if (!std::isfinite(actual[i])
                || std::abs(actual[i] - values[i % wordsPerPixel]) >= 0.00001f)
                std::fprintf(stderr,
                    "Graphics GI lane%zu actual%.9g expected%.9g\n",
                    i,
                    actual[i],
                    values[i % wordsPerPixel]);
            assert(std::isfinite(actual[i])
                && std::abs(actual[i] - values[i % wordsPerPixel]) < 0.00001f);
        }
    }

    static constexpr std::array<std::uint8_t, 4u> expectedColor(std::size_t x) noexcept
    {
        return x ? std::array<std::uint8_t, 4u>{0u, 255u, 0u, 255u}
                 : std::array<std::uint8_t, 4u>{255u, 0u, 0u, 255u};
    }
};
} // namespace arp::test
