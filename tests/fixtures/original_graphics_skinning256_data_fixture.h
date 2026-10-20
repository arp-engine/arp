#pragma once
#include "original_graphics_matrix_data_fixture.h"
#include "renderer/mesh/gpu_skinning_contract.h"

namespace arp::test {
class OriginalGraphicsSkinning256DataFixture final {
public:
    static constexpr const char* packageName = "GraphicsSkinning256";
    using Numeric = std::array<float, 64u>;
    static constexpr const char* numericDescription =
        "skinning256 temporal-high-indices previous-count220 normalized quaternion normal-tangent";

    struct Matrices {
        renderer::GpuSkinningPalette current, previous;
    };

    static constexpr std::array<std::uint64_t, 2u> ordinaryBytes{32864u, 32u};

    static Matrices matrices()
    {
        static_assert(sizeof(renderer::GpuSkinningPalette) == 16432u);
        static_assert(sizeof(Matrices) == ordinaryBytes[0]);
        static_assert(offsetof(Matrices, previous) == 16432u);
        static_assert(offsetof(renderer::GpuSkinningPalette, worldTranslationAndScale) == 16384u);
        static_assert(offsetof(renderer::GpuSkinningPalette, worldRotation) == 16400u);
        static_assert(offsetof(renderer::GpuSkinningPalette, jointCount) == 16416u);
        Matrices result;
        for (std::size_t pose = 0u; pose < 2u; ++pose) {
            auto& p = pose ? result.previous : result.current;
            for (std::size_t joint = 0u; joint < renderer::kMaxGpuSkinningJoints; ++joint) {
                auto& m = p.skinMatrices[joint];
                for (std::size_t lane = 0u; lane < m.size(); ++lane)
                    m[lane] = static_cast<float>(joint * 2u + lane + pose * 3u + 1u);
            }
            p.jointCount = renderer::kMaxGpuSkinningJoints;
        }
        // Previous pose independently clamps 255 to 219, proving its own count.
        result.previous.jointCount = 220u;
        result.current.worldTranslationAndScale = {2.f, 4.f, 8.f, 2.f};
        result.previous.worldTranslationAndScale = {16.f, 32.f, 64.f, 0.5f};
        result.current.worldRotation = {0.f, 0.f, 1.f, 0.f};
        result.previous.worldRotation = {1.f, 0.f, 0.f, 0.f};
        return result;
    }

    static renderer::GpuSkinningInfluenceVertex revision()
    {
        static_assert(sizeof(renderer::GpuSkinningInfluenceVertex) == ordinaryBytes[1]);
        // Sum 0.5 deliberately exercises production normalization.
        return {{0.f, 128.f, 219.f, 255.f}, {0.0625f, 0.125f, 0.0625f, 0.25f}};
    }

    static Matrices ordinary0() { return matrices(); }

    static renderer::GpuSkinningInfluenceVertex ordinary1() { return revision(); }

    static core::Vec4 skin(const Matrices& matrices, core::Vec4 point, bool previous)
    {
        const auto influence = revision();
        const auto& selected = previous ? matrices.previous : matrices.current;
        core::Vec4 blended{};
        float weightSum = 0.f;
        for (std::size_t lane = 0u; lane < influence.joints.size(); ++lane) {
            const auto original = static_cast<std::uint32_t>(influence.joints[lane]);
            const auto joint = std::min(original, matrices.current.jointCount - 1u);
            if (influence.weights[lane] <= 0.f || joint >= renderer::kMaxGpuSkinningJoints)
                continue;
            const auto index = previous ? std::min(original, selected.jointCount - 1u) : joint;
            if (index < renderer::kMaxGpuSkinningJoints) {
                core::Mat4 m;
                std::copy(selected.skinMatrices[index].begin(),
                    selected.skinMatrices[index].end(),
                    m.m.begin());
                blended = blended
                    + core::scale(OriginalGraphicsMatrixDataFixture::transform(m, point),
                        influence.weights[lane]);
            }
            weightSum += influence.weights[lane];
        }
        const float scale = selected.worldTranslationAndScale[3] / weightSum;
        const auto& q = selected.worldRotation;
        auto position = core::rotate(core::Quat{q[0], q[1], q[2], q[3]},
            core::Vec3{blended.x * scale, blended.y * scale, blended.z * scale});
        return {position.x + selected.worldTranslationAndScale[0],
            position.y + selected.worldTranslationAndScale[1],
            position.z + selected.worldTranslationAndScale[2],
            blended.w};
    }

    static core::Vec3 direction(const Matrices& matrices, core::Vec3 value)
    {
        const auto influence = revision();
        core::Vec3 blended{};
        for (std::size_t lane = 0u; lane < influence.joints.size(); ++lane) {
            const auto joint = static_cast<std::uint32_t>(influence.joints[lane]);
            core::Mat4 matrix;
            std::copy(matrices.current.skinMatrices[joint].begin(),
                matrices.current.skinMatrices[joint].end(),
                matrix.m.begin());
            blended = blended
                + core::scale(core::transformVector(matrix, value), influence.weights[lane]);
        }
        const auto& q = matrices.current.worldRotation;
        return core::normalize(core::rotate(core::Quat{q[0], q[1], q[2], q[3]}, blended));
    }

    static Numeric expected()
    {
        const auto m = matrices();
        const auto vertex = skin(m, {2.f, 3.f, 5.f, 1.f}, false);
        const auto normal = direction(m, {2.f, 3.f, 5.f});
        const auto tangent = direction(m, {-3.f, 2.f, 1.f});
        Numeric result{};
        for (std::size_t pixel = 0u; pixel < 4u; ++pixel) {
            const auto fragment = skin(m, {static_cast<float>(pixel + 1u), 3.f, 5.f, 1.f}, true);
            const std::array values{vertex.x,
                vertex.y,
                vertex.z,
                vertex.w,
                fragment.x,
                fragment.y,
                fragment.z,
                fragment.w,
                normal.x,
                normal.y,
                normal.z,
                0.f,
                tangent.x,
                tangent.y,
                tangent.z,
                -1.f};
            std::copy(values.begin(), values.end(), result.begin() + pixel * values.size());
        }
        return result;
    }

    static void verify(const Numeric& actual)
    {
        const auto oracle = expected();
        for (std::size_t i = 0u; i < actual.size(); ++i) {
            const bool directionLane = i % 16u >= 8u;
            const bool match = std::isfinite(actual[i])
                && (directionLane
                        ? std::abs(actual[i] - oracle[i]) <= 2e-5f * (1.f + std::abs(oracle[i]))
                        : actual[i] == oracle[i]);
            if (!match)
                std::fprintf(stderr,
                    "Skinning256 lane %zu actual %.9g expected %.9g\n",
                    i,
                    actual[i],
                    oracle[i]);
            assert(match);
        }
        std::puts(
            "BINDLESS_GRAPHICS_SKINNING256_NUMERIC64_OK temporal32864 influence32 indices0-128-219-255 previous-count220 position-exact normal-tangent2e-5 handedness row-memory");
    }

    static void configureOrdinaryBuffer(
        rhi::BufferDesc& desc, std::uint32_t set, const rhi::DescriptorBindingDesc& binding)
    {
        assert(set == 0u && binding.binding < ordinaryBytes.size());
        desc.memory = rhi::MemoryUsage::CpuToGpu;
        desc.sizeBytes = ordinaryBytes[binding.binding];
    }
};
} // namespace arp::test
