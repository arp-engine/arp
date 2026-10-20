#pragma once
#include "rhi/frontend/command_sources.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>

namespace arp::test {
class OriginalGraphicsTerrain27DataFixture final {
public:
    struct Record {
        std::array<float, 4u> heightSnowWet;
        std::array<std::uint32_t, 4u> selectors;
    };

    static constexpr const char* packageName = "GraphicsTerrain27";
    static constexpr const char* numericDescription =
        "25-2D two-array-pages mip1 six-weights snow-wet";
    static constexpr std::uint32_t resultBinding = 28u;
    static constexpr std::uint32_t bindingCount = 33u, sampledTextureCount = 27u;
    static constexpr std::uint64_t runtimeStride = 32u, runtimeCapacity = 3u;
    static constexpr std::array<std::uint64_t, 2u> ordinaryBytes{96u, 16u};
    static constexpr float tolerance = 2e-5f;
    using Numeric = std::array<float, 128u>;
    using Pixel = std::array<std::uint8_t, 4u>;

    static std::array<Record, 3u> ordinary0()
    {
        return {{{{99.f, 98.f, 97.f, 96.f}, {99u, 98u, 97u, 96u}},
            {{3.f, 1.f, 4.f, 0.f}, {0u, 0u, 0u, 0u}},
            {{1.0005f, 1.f, 0.f, 0.f}, {4u, 0u, 0u, 0u}}}};
    }

    static std::array<std::uint32_t, 4u> ordinary1() { return {1u, 2u, 0u, 0u}; }

    static constexpr Pixel expectedColor(std::size_t) { return {64, 128, 191, 255}; }

    static constexpr Pixel surface(std::uint32_t image)
    {
        Pixel value{};
        for (std::uint32_t c = 0u; c < 4u; ++c)
            value[c] = static_cast<std::uint8_t>((17u + image * 23u + c * 37u) % 251u);
        return value;
    }

    static constexpr std::array<std::array<Pixel, 2u>, 2u> pages{
        {{{{30, 40, 50, 60}, {70, 20, 40, 50}}}, {{{80, 20, 160, 90}, {10, 90, 60, 180}}}}};

    static void fillTexture(
        std::uint32_t image, std::uint32_t layers, core::pmr::Vector<std::uint8_t>& pixels)
    {
        assert((layers == 1u && image < 25u) || (layers == 2u && image < 2u));
        assert(pixels.size() == 20u * layers);
        for (std::uint32_t layer = 0u; layer < layers; ++layer) {
            const auto value = layers == 1u ? surface(image) : pages[image][layer];
            for (std::size_t channel = 0u; channel < 4u; ++channel) {
                for (std::size_t p = 0u; p < 4u; ++p)
                    pixels[layer * 16u + p * 4u + channel] = 255u - value[channel];
                pixels[16u * layers + layer * 4u + channel] = value[channel];
            }
        }
    }

    // Independent CPU math uses double accumulation and explicit weighted
    // channels; no generated shader, backend output or GPU reference is read.
    static Numeric expected()
    {
        Numeric output{};
        const auto records = ordinary0();
        for (std::size_t pixel = 0u; pixel < 4u; ++pixel) {
            auto* lanes = output.data() + pixel * 32u;
            const auto selected = 1u + pixel % 2u;
            const auto layer = pixel / 2u;
            const auto& record = records[selected];
            std::array<double, 6u> weights{};
            for (std::size_t c = 0u; c < 4u; ++c) {
                lanes[c] = pages[0][layer][c] / 255.f;
                lanes[4u + c] = pages[1][layer][c] / 255.f;
                if (c < 4u)
                    weights[c] = pages[0][layer][c] / 255.0;
            }
            weights[4] = pages[1][layer][0] / 255.0;
            weights[5] = pages[1][layer][1] / 255.0;
            const auto normalize = [&]() {
                double sum = 0.0;
                for (const auto value : weights)
                    sum += value;
                for (auto& value : weights)
                    value /= std::max(sum, 0.001);
            };
            normalize();
            const double snow = pages[1][layer][2] / 255.0
                * std::clamp((double(record.heightSnowWet[0]) - record.heightSnowWet[1])
                        / std::max(double(record.heightSnowWet[2]), 0.001),
                    0.0,
                    1.0);
            const double wet = pages[1][layer][3] / 255.0 * 0.3;
            weights[5] += snow * 0.7;
            weights[0] *= (1.0 - snow * 0.7) * (1.0 - wet * 0.5);
            weights[4] *= 1.0 - snow * 0.56;
            weights[2] += wet;
            normalize();
            for (std::size_t i = 0u; i < weights.size(); ++i)
                lanes[8u + i] = static_cast<float>(weights[i]);
            lanes[14] = static_cast<float>(snow);
            lanes[15] = static_cast<float>(wet);
            for (std::size_t i = 0u; i < weights.size(); ++i) {
                const auto base = (i * 4u + record.selectors[0]) % 24u;
                for (std::size_t c = 0u; c < 3u; ++c) {
                    lanes[16u + c] += static_cast<float>(surface(base)[c] / 255.0 * weights[i]);
                    lanes[20u + c] +=
                        static_cast<float>((surface(base + 1u)[c] / 127.5 - 1.0) * weights[i]);
                }
                lanes[23] += static_cast<float>(surface(base + 2u)[0] / 255.0 * weights[i]);
                lanes[24] += static_cast<float>(surface(base + 3u)[0] / 255.0 * weights[i]);
            }
            lanes[19] = surface(24u)[0] / 255.f;
            std::copy_n(records[1].heightSnowWet.begin(), 3u, lanes + 25u);
            lanes[28] = static_cast<float>(selected);
            lanes[29] = static_cast<float>(layer);
            lanes[30] = lanes[31] = 1.f;
        }
        return output;
    }

    static bool matches(const Numeric& actual)
    {
        const auto oracle = expected();
        for (std::size_t i = 0u; i < actual.size(); ++i)
            if (!std::isfinite(actual[i])
                || std::abs(actual[i] - oracle[i]) > tolerance * (1.f + std::abs(oracle[i])))
                return false;
        return true;
    }

    static void verify(const Numeric& actual)
    {
        const auto oracle = expected();
        if (!matches(actual))
            for (std::size_t i = 0u; i < actual.size(); ++i)
                std::fprintf(
                    stderr, "Terrain27 lane%zu actual%.9g expected%.9g\n", i, actual[i], oracle[i]);
        assert(matches(actual));
        std::puts(
            "BINDLESS_GRAPHICS_TERRAIN27_NUMERIC128_OK 25-2D two-array-pages mip1 weights snow-wet tolerance2e-5");
    }

    static void configureOrdinaryBuffer(
        rhi::BufferDesc& desc, std::uint32_t set, const rhi::DescriptorBindingDesc& binding)
    {
        static_assert(sizeof(Record) == 32u && offsetof(Record, selectors) == 16u);
        assert(set == 0u && binding.binding < ordinaryBytes.size());
        desc.memory = rhi::MemoryUsage::CpuToGpu;
        desc.sizeBytes = ordinaryBytes[binding.binding];
    }
};
} // namespace arp::test
