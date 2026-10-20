#pragma once
#include "core/cpu_allocator.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace arp::test {
// Shared independent CPU expectation for the authored 2x2 depth/color shader.
// Both native and browser tests consume the same forty lanes and RGBA bytes.
struct OrdinaryDepthComparisonOracleFixture final {
    static constexpr std::size_t readbackBytes = 40u * sizeof(float);
    static constexpr std::uint32_t resultBinding = 3u;
    static constexpr const char* sampledReceipt = "ORDINARY_DEPTH";

    static void initializeTexturePixels(
        std::uint32_t image, std::uint32_t layers, core::pmr::Vector<std::uint8_t>& pixels)
    {
        assert(!image && layers == 1u && pixels.size() == 20u);
        for (std::size_t pixel = 0u; pixel < pixels.size() / 4u; ++pixel) {
            pixels[pixel * 4u] = 16u * (pixel + 1u);
            pixels[pixel * 4u + 1u] = pixels[pixel * 4u + 2u] = 0u;
            pixels[pixel * 4u + 3u] = 255u;
        }
    }

    static void verifyValues(const std::array<float, 40u>& values)
    {
        for (std::size_t pixel = 0u; pixel < 4u; ++pixel) {
            const std::array<float, 10u> expected{
                float(16u * (pixel + 1u)) / 255.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
            for (std::size_t lane = 0u; lane < expected.size(); ++lane) {
                const auto actual = values[pixel * 10u + lane];
                if (!std::isfinite(actual) || std::abs(actual - expected[lane]) > 1e-6f)
                    std::fprintf(stderr,
                        "ordinary depth pixel%zu lane%zu actual%.9g expected%.9g\n",
                        pixel,
                        lane,
                        actual,
                        expected[lane]);
                assert(std::isfinite(actual) && std::abs(actual - expected[lane]) <= 1e-6f);
            }
        }
    }

    static void verify(const std::array<std::uint8_t, 512u + readbackBytes>& actual, bool fault)
    {
        for (std::uint32_t y = 0u; y < 2u; ++y)
            for (std::uint32_t x = 0u; x < 2u; ++x) {
                const auto* pixel = actual.data() + y * 256u + x * 4u;
                assert(pixel[0] == 16u * (y * 2u + x + 1u) && !pixel[1] && !pixel[2]
                    && pixel[3] == 255u);
            }
        std::array<float, 40u> values{};
        std::memcpy(values.data(), actual.data() + 512u, readbackBytes);
        verifyValues(values);
        std::printf(
            "BINDLESS_WEBGPU_ORDINARY_DEPTH_NUMERIC_OK fault%u lanes40 rgba16 comparison-less-equal-greater independent-cpu-oracle\n",
            fault ? 1u : 0u);
    }
};
} // namespace arp::test
