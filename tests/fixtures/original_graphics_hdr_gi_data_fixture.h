#pragma once
#include "core/ibl_bake.h"
#include "original_graphics_color_gi_data_fixture.h"

#include <cstring>

namespace arp::test {
class OriginalGraphicsHdrGiDataFixture final {
public:
    static constexpr std::size_t wordsPerPixel = 28u, pixels = 4u;
    static constexpr std::uint32_t resultBinding = 3u;
    static constexpr const char* packageName = "GraphicsHdrGi";
    using Numeric = std::array<float, wordsPerPixel * pixels>;

    static void initializeTexturePixels(
        std::uint32_t index, std::uint32_t layers, core::pmr::Vector<std::uint8_t>& pixels)
    {
        constexpr std::size_t texelBytes = 8u;
        assert(pixels.size() == 5u * texelBytes * layers);
        for (std::uint32_t layer = 0u; layer < layers; ++layer)
            for (std::uint32_t texel = 0u; texel < 5u; ++texel) {
                const auto value =
                    float((index + 1u) * ((texel < 4u ? texel + 1u : 8u) + 2u * layer));
                const auto offset = texel < 4u ? (layer * 4u + texel) * texelBytes
                                               : (4u * layers + layer) * texelBytes;
                std::array<std::uint16_t, 4u> rgba{};
                core::ibl::packRgba16f({value, value * 0.5f, -value}, 1.f, rgba.data());
                std::memcpy(pixels.data() + offset, rgba.data(), sizeof(rgba));
            }
    }

    static auto expected() noexcept
    {
        std::array<float, wordsPerPixel> values{};
        for (std::uint32_t face = 0u; face < 6u; ++face) {
            const float base = 2.5f + 2.f * float(face), mip = 8.f + 2.f * float(face);
            values[face * 3u] = 17.f + base;
            values[face * 3u + 1u] = 17.f + mip;
            values[face * 3u + 2u] = 17.f + (base + mip) * 0.5f;
        }
        values[18] = 19.f;
        values[19] = 33.f;
        values[20] = 26.f;
        for (std::uint32_t layer = 0u; layer < 2u; ++layer) {
            const float base = 3.f * (1.f + 2.f * float(layer));
            const float mip = 3.f * (8.f + 2.f * float(layer));
            values[21u + layer * 3u] = 17.f + base;
            values[22u + layer * 3u] = 17.f + mip;
            values[23u + layer * 3u] = 17.f + base * 0.75f + mip * 0.25f;
        }
        values[27] = values[2];
        return values;
    }

    static void verify(const Numeric& actual)
    {
        const auto values = expected();
        for (std::size_t i = 0u; i < actual.size(); ++i) {
            if (!std::isfinite(actual[i])
                || std::abs(actual[i] - values[i % wordsPerPixel]) >= 0.00001f)
                std::fprintf(stderr,
                    "Graphics HDR GI lane%zu actual%.9g expected%.9g\n",
                    i,
                    actual[i],
                    values[i % wordsPerPixel]);
            assert(std::isfinite(actual[i])
                && std::abs(actual[i] - values[i % wordsPerPixel]) < 0.00001f);
        }
    }

    static constexpr auto expectedColor(std::size_t x) noexcept
    {
        return OriginalGraphicsColorGiDataFixture::expectedColor(x);
    }
};
} // namespace arp::test
