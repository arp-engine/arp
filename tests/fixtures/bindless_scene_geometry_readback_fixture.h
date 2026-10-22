#pragma once
#include "bindless_geometry_numeric_contract.h"
#include "renderer/bindless/bindless_pbr_geometry.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace arp::test {
// Independent raster geometry oracle for the authored fullscreen triangle,
// identity current/previous/shadow matrices, zero wind and identity skinning.
// Surface values are checked for finiteness only; material/lighting parity
// requires its separate numerical and attachment comparison.
class BindlessSceneGeometryReadbackFixture final {
public:
    static constexpr auto lanes = arp::test::BindlessGeometryNumericContract::numericLanesPerPixel;
    using Words = std::array<std::uint32_t, 4u * lanes>;

    static const char* inspectAndReport(
        const char* entry, const void* bytes, std::uint64_t size, bool terrain) noexcept
    {
        if (const auto* id = inspect(bytes, size, terrain))
            return id;
        Words words;
        std::memcpy(words.data(), bytes, sizeof(words));
        std::printf("BINDLESS_SCENE_GEOMETRY_WORDS entry=%s words=", entry);
        for (std::size_t i = 0u; i < words.size(); ++i)
            std::printf(i ? ",%u" : "%u", words[i]);
        std::puts("");
        return nullptr;
    }

    static const char* inspect(const void* bytes, std::uint64_t size, bool terrain) noexcept
    {
        if (!bytes || size != sizeof(Words))
            return "SCENE-GEOMETRY-READBACK-SIZE";
        Words words;
        std::memcpy(words.data(), bytes, sizeof(words));
        const auto value = [&](std::size_t offset) noexcept {
            float result;
            std::memcpy(&result, &words[offset], sizeof(result));
            return result;
        };
        const auto equal = [](float actual, float expected) noexcept {
            return std::isfinite(actual) && std::abs(actual - expected) <= 0.00001f;
        };
        std::array<bool, 4u> seen{};
        for (std::size_t pixel = 0u; pixel < 4u; ++pixel) {
            const auto first = pixel * lanes;
            for (std::size_t lane = 0u; lane < lanes; ++lane) {
                if (!terrain && lane >= 18u && lane < 22u) {
                    if (words[first + lane])
                        return "SCENE-GEOMETRY-READBACK-METADATA";
                } else if (!std::isfinite(value(first + lane)))
                    return "SCENE-GEOMETRY-READBACK-NONFINITE";
            }
            const auto x = value(first), y = value(first + 1u);
            if (!equal(std::abs(x), 0.5f) || !equal(std::abs(y), 0.5f)
                || !equal(value(first + 2u), 0.5f))
                return "SCENE-GEOMETRY-READBACK-WORLD";
            // Explicitly permit the platforms' declared raster origin. Every
            // pixel center must still occur once, with consistent clip values.
            const auto corner = (x > 0.f ? 1u : 0u) + (y > 0.f ? 2u : 0u);
            if (seen[corner])
                return "SCENE-GEOMETRY-READBACK-COVERAGE";
            seen[corner] = true;
            for (const auto clip : terrain ? std::array{11u, 15u, 19u} : std::array{6u, 10u, 14u})
                if (!equal(value(first + clip), x) || !equal(value(first + clip + 1u), y)
                    || !equal(value(first + clip + 2u), 0.5f)
                    || !equal(value(first + clip + 3u), 1.f))
                    return "SCENE-GEOMETRY-READBACK-TEMPORAL-CLIP";
            if (!terrain && !equal(value(first + 22u), 0.f))
                return "SCENE-GEOMETRY-READBACK-WIND-TIME";
        }
        return nullptr;
    }
};
} // namespace arp::test
