#pragma once
#include "core/math.h"
#include "rhi/frontend/command_sources.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::test {
// One shared row-memory oracle for Native and Emulated. Values are exactly
// representable; every palette element and all four output lanes contribute.
class OriginalGraphicsMatrixDataFixture final {
public:
    static constexpr const char* packageName = "GraphicsMatrixOrdinary";
    static constexpr const char* numericDescription = "current-previous-palette projective-w";
    using Numeric = std::array<float, 32u>;
    static constexpr std::array<std::uint64_t, 2u> ordinaryBytes{320u, 16u};

    struct Matrices {
        core::Mat4 current, previous;
        std::array<core::Mat4, 3u> palette;
    };

    static Matrices matrices()
    {
        return {{{2, 3, 0, 7, 0, 5, 1, 11, 1, 0, 4, 13, 2, 1, 3, 1}},
            {{1, -2, 3, 17, 2, 3, 0, 19, 0, 4, 5, 23, 1, 3, 2, 1}},
            {{{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}},
                {{2, 1, 4, 3, 6, 5, 8, 7, 10, 9, 12, 11, 14, 13, 16, 15}},
                {{3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59}}}}};
    }

    static core::Vec4 revision() { return {2, 4, 8, 16}; }

    static Matrices ordinary0() { return matrices(); }

    static core::Vec4 ordinary1() { return revision(); }

    static core::Vec4 transform(const core::Mat4& m, core::Vec4 v)
    {
        // Reuse the core matrix product as the CPU oracle, including projective w.
        core::Mat4 column;
        column.m[0] = v.x;
        column.m[4] = v.y;
        column.m[8] = v.z;
        column.m[12] = v.w;
        const auto result = core::multiply(m, column);
        return {result.m[0], result.m[4], result.m[8], result.m[12]};
    }

    static std::array<float, 32u> expected()
    {
        const auto m = matrices();
        const core::Vec4 point{2, 3, 5, 1};
        const auto skinned = core::scale(transform(m.palette[0], point), 0.25f)
            + core::scale(transform(m.palette[2], point), 0.75f);
        const auto vertex =
            transform(m.current, skinned) + transform(m.previous, point) + revision();
        std::array<float, 32u> result{};
        for (std::size_t pixel = 0u; pixel < 4u; ++pixel) {
            const core::Vec4 p{static_cast<float>(pixel + 1u), 3, 5, 1};
            const auto fragment = transform(m.current, transform(m.previous, p))
                + transform(m.palette[1], p) + revision();
            const std::array<float, 8u> values{vertex.x,
                vertex.y,
                vertex.z,
                vertex.w,
                fragment.x,
                fragment.y,
                fragment.z,
                fragment.w};
            std::copy(values.begin(), values.end(), result.begin() + pixel * values.size());
        }
        return result;
    }

    static void verify(const std::array<float, 32u>& actual)
    {
        const auto oracle = expected();
        for (std::size_t i = 0u; i < actual.size(); ++i) {
            if (actual[i] != oracle[i])
                std::fprintf(stderr,
                    "Graphics matrix lane %zu: actual %.9g expected %.9g\n",
                    i,
                    actual[i],
                    oracle[i]);
            assert(actual[i] == oracle[i]);
        }
        std::puts(
            "BINDLESS_GRAPHICS_MATRIX_NUMERIC32_OK current previous palette0/1/2 Vertex Fragment projective-w row-memory");
    }

    static void configureOrdinaryBuffer(
        rhi::BufferDesc& desc, std::uint32_t set, const rhi::DescriptorBindingDesc& binding)
    {
        static_assert(sizeof(Matrices) == 320u && sizeof(core::Vec4) == 16u);
        assert(set == 0u && binding.binding < 2u);
        desc.memory = rhi::MemoryUsage::CpuToGpu;
        desc.sizeBytes = binding.binding == 0u ? sizeof(Matrices) : sizeof(core::Vec4);
    }
};
} // namespace arp::test
