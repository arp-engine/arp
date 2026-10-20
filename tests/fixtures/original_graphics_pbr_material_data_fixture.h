#pragma once
#include "core/math.h"
#include "rhi/frontend/command_sources.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace arp::test {
// Independent CPU oracle for the explicit test transport of WOCC material lanes.
// This record is not a new production renderer ABI.
class OriginalGraphicsPbrMaterialDataFixture final {
public:
    struct Record {
        std::array<float, 4u> baseColorAlpha, pbrParams, pbrExtraParams, environmentParams;
        std::array<std::uint32_t, 4u> textures, occlusionSampler;
    };

    static constexpr const char* packageName = "GraphicsPbrMaterial";
    static constexpr const char* numericDescription =
        "five-role material mip1 discard filters leaf normal";
    static constexpr std::uint32_t resultBinding = 12u;
    static constexpr std::uint32_t bindingCount = 17u, sampledTextureCount = 10u;
    static constexpr float tolerance = 2e-5f;
    static constexpr std::uint64_t runtimeStride = 96u, runtimeCapacity = 3u;
    static constexpr std::array<std::uint64_t, 2u> ordinaryBytes{288u, 16u};
    using Numeric = std::array<float, 128u>;
    using Pixel = std::array<std::uint8_t, 4u>;

    static std::array<Record, 3u> ordinary0()
    {
        return {{{{9, 8, 7, 6}, {9, 8, 7, 6}, {}, {}, {9, 9, 9, 9}, {9, 1, 0, 0}},
            {{0.5f, 0.75f, 1.f, 0.8f},
                {2.f, 0.01f, 0.4f, 1.f},
                {1.5f, 2.f, 1.f, 0.25f},
                {0.7f, 0.3f, 0.2f, 0.1f},
                {0, 1, 2, 3},
                {4, 0, 0, 0}},
            {{1.f, 0.5f, 0.25f, 0.5f},
                {0.25f, 2.f, 0.4f, 2.125f},
                {-0.25f, 0.5f, -0.75f, 0.9f},
                {-0.5f, 0.6f, 0.7f, 0.8f},
                {5, 6, 7, 8},
                {9, 1, 0, 0}}}};
    }

    static std::array<std::uint32_t, 4u> ordinary1() { return {1u, 2u, 1u, 0u}; }

    static constexpr Pixel expectedColor(std::size_t x)
    {
        return x ? Pixel{0, 0, 0, 255} : Pixel{64, 128, 191, 255};
    }

    static constexpr std::array<Pixel, 10u> mip1{{{64, 128, 192, 128},
        {128, 192, 255, 255},
        {16, 128, 192, 255},
        {96, 64, 32, 255},
        {32, 64, 128, 255},
        {192, 64, 128, 192},
        {192, 64, 255, 255},
        {128, 192, 32, 255},
        {32, 96, 160, 255},
        {224, 64, 32, 255}}};

    static float texel(std::uint32_t image, std::size_t channel)
    {
        return static_cast<float>(mip1.at(image).at(channel)) / 255.f;
    }

    static void fillTexture(
        std::uint32_t image, std::uint32_t layers, core::pmr::Vector<std::uint8_t>& pixels)
    {
        assert(image < mip1.size() && layers == 1u && pixels.size() == 20u);
        for (std::size_t pixel = 0u; pixel < 4u; ++pixel)
            for (std::size_t channel = 0u; channel < 4u; ++channel)
                pixels[pixel * 4u + channel] = (255u - mip1[image][channel] + 17u * pixel) % 256u;
        std::copy(mip1[image].begin(), mip1[image].end(), pixels.begin() + 16u);
    }

    static bool covered(float alpha, float mode, float cutoff)
    {
        const bool mask = (mode > 0.5f && mode < 1.5f) || (mode >= 2.f && mode <= 3.f);
        return !mask || !(alpha < cutoff);
    }

    static float filterProbe(std::size_t pixel, std::size_t channel, bool linear)
    {
        const auto corner = [&](std::size_t index) {
            return static_cast<float>((255u - mip1[0][channel] + 17u * index) % 256u) / 255.f;
        };
        if (!linear)
            return corner(3u);
        const float x = 0.5f + 0.25f * (static_cast<float>(pixel % 2u) + 0.5f);
        const float y = 0.5f + 0.25f * (static_cast<float>(pixel / 2u) + 0.5f);
        return (corner(0u) * (1.f - x) + corner(1u) * x) * (1.f - y)
            + (corner(2u) * (1.f - x) + corner(3u) * x) * y;
    }

    static Numeric expected()
    {
        Numeric result{};
        const auto records = ordinary0();
        for (std::size_t pixel = 0u; pixel < 4u; ++pixel) {
            const auto selected = 1u + pixel % 2u;
            const auto& m = records[selected];
            const auto base = pixel * 32u;
            std::copy(records[1].baseColorAlpha.begin(),
                records[1].baseColorAlpha.end(),
                result.begin() + base);
            for (std::size_t lane = 0u; lane < 4u; ++lane) {
                result[base + 4u + lane] = texel(m.textures[0], lane) * m.baseColorAlpha[lane];
            }
            result[base + 24u] = filterProbe(pixel, 0u, false);
            result[base + 25u] = filterProbe(pixel, 0u, true);
            result[base + 26u] = filterProbe(pixel, 1u, false);
            result[base + 27u] = filterProbe(pixel, 1u, true);
            result[base + 8u] = std::clamp(texel(m.textures[2], 2u) * m.pbrParams[0], 0.f, 1.f);
            result[base + 9u] = std::clamp(texel(m.textures[2], 1u) * m.pbrParams[1], 0.045f, 1.f);
            const float strength = std::clamp(m.pbrExtraParams[0], 0.f, 1.f);
            result[base + 10u] = 1.f + (texel(m.occlusionSampler[0], 0u) - 1.f) * strength;
            result[base + 11u] =
                covered(result[base + 7u], m.pbrParams[3], m.pbrParams[2]) ? 1.f : 0.f;
            const bool leaf = m.pbrParams[3] >= 2.f && m.pbrParams[3] <= 3.f;
            const float maximum =
                std::max({result[base + 4u], result[base + 5u], result[base + 6u]});
            const float minimum =
                std::min({result[base + 4u], result[base + 5u], result[base + 6u]});
            const float chroma = maximum - minimum;
            for (std::size_t channel = 0u; channel < 3u; ++channel) {
                result[base + 12u + channel] = texel(m.textures[3], channel) * m.pbrExtraParams[1];
                // HSV retains hue/value and changes saturation. Express this
                // as interpolation from V toward RGB, independently of Slang.
                const float saturation = leaf && chroma > 1e-8f && maximum > 0.f
                    ? std::min(-m.environmentParams[0], maximum / chroma)
                    : 1.f;
                result[base + 16u + channel] =
                    maximum + (result[base + 4u + channel] - maximum) * saturation;
            }
            result[base + 15u] = leaf ? m.pbrParams[3] - 2.f : 0.04f;
            result[base + 19u] = leaf ? std::max(-m.pbrExtraParams[2], 0.f) : 0.f;
            // The authored non-orthogonal tangent (2,0,1) must project to +X.
            // Both front/back bitangent signs produce -Y; back-face double-
            // sided lighting flips N only after fixing the tangent frame.
            const bool front = pixel < 2u;
            const bool flip = !front && m.pbrExtraParams[2] < 0.5f;
            const auto normal = core::normalize(core::Vec3{2.f * texel(m.textures[1], 0u) - 1.f,
                -(2.f * texel(m.textures[1], 1u) - 1.f),
                (flip ? -1.f : 1.f) * (2.f * texel(m.textures[1], 2u) - 1.f)});
            result[base + 20u] = normal.x;
            result[base + 21u] = normal.y;
            result[base + 22u] = normal.z;
            result[base + 23u] = front ? 1.f : 0.f;
            result[base + 28u] = static_cast<float>(selected);
            result[base + 29u] = static_cast<float>(m.occlusionSampler[1]);
            result[base + 30u] = result[base + 31u] = 1.f;
        }
        return result;
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
                std::fprintf(stderr,
                    "PBR material lane%zu actual%.9g expected%.9g\n",
                    i,
                    actual[i],
                    oracle[i]);
        assert(matches(actual));
        std::puts(
            "BINDLESS_GRAPHICS_PBR_MATERIAL_NUMERIC128_OK five-role mip1 discard filters leaf normal tolerance2e-5");
    }

    static void configureOrdinaryBuffer(
        rhi::BufferDesc& desc, std::uint32_t set, const rhi::DescriptorBindingDesc& binding)
    {
        static_assert(sizeof(Record) == 96u && offsetof(Record, textures) == 64u
            && offsetof(Record, occlusionSampler) == 80u);
        assert(set == 0u && binding.binding < ordinaryBytes.size());
        desc.memory = rhi::MemoryUsage::CpuToGpu;
        desc.sizeBytes = ordinaryBytes[binding.binding];
    }
};
} // namespace arp::test
