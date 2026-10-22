#pragma once
#include "3d/vegetation/foliage_geometry_profile.h"
#include "3d/vegetation/template.h"
#include "core/math/math.h"
#include "rhi/frontend/command_sources.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <type_traits>

namespace arp::test {
class OriginalGraphicsInstance64DataFixture final {
public:
    static constexpr const char* packageName = "GraphicsInstance64";
    static constexpr const char* numericDescription =
        "instance64 base1 rotation-normal-tangent metadata-bits wind-current-previous-grass-canopy-rigid";
    using Numeric = std::array<float, 176u>;
    static constexpr std::uint64_t runtimeStride = 64u, runtimeCapacity = 4u;
    using Instances = std::array<vegetation::InstanceDataGpu, 4u>;

    struct Probe {
        std::array<float, 4u> position, normal, tangent;
        std::array<std::uint32_t, 4u> selectors, offsets;
        std::array<float, 4u> windWeightsAndHeight;
        vegetation::WindFieldParameters currentWind, previousWind;
    };

    static_assert(std::is_standard_layout_v<vegetation::InstanceDataGpu>);
    static_assert(sizeof(vegetation::InstanceDataGpu) == 64u);
    static_assert(offsetof(vegetation::InstanceDataGpu, positionScale) == 0u);
    static_assert(offsetof(vegetation::InstanceDataGpu, yawPhaseIdLod) == 16u);
    static_assert(offsetof(vegetation::InstanceDataGpu, extraTransform) == 32u);
    static_assert(offsetof(vegetation::InstanceDataGpu, speciesLodState) == 48u);
    static_assert(sizeof(Instances) == 256u && sizeof(Probe) == 224u);
    static_assert(offsetof(Probe, selectors) == 48u && offsetof(Probe, offsets) == 64u);
    static_assert(offsetof(Probe, windWeightsAndHeight) == 80u
        && offsetof(Probe, currentWind) == 96u && offsetof(Probe, previousWind) == 160u);
    static constexpr std::array<std::uint64_t, 2u> ordinaryBytes{sizeof(Instances), sizeof(Probe)};
    static constexpr float tolerance = 2e-5f;

    static Instances instances()
    {
        Instances result;
        constexpr std::array<std::array<float, 4u>, 4u> positions{{{111.f, 222.f, 333.f, 7.f},
            {2.f, -3.f, 5.f, 2.f},
            {-8.f, 4.f, 12.f, 0.5f},
            {16.f, -2.f, -6.f, 1.5f}}};
        constexpr std::array<std::array<float, 4u>, 4u> angles{{{1.f, 0.f, 900.f, 0.f},
            {0.37f, 0.25f, 101.f, 0.f},
            {-0.63f, 0.5f, 202.f, 1.f},
            {1.12f, 0.75f, 303.f, 2.f}}};
        // Includes below-threshold tilt (both skipped), positive Y override,
        // negative Y fallback and an above-threshold roll with tiny pitch.
        constexpr std::array<std::array<float, 4u>, 4u> extra{{{0.f, 0.f, 0.f, 0.f},
            {0.0005f, -0.0005f, 0.f, -0.75f},
            {0.41f, -0.27f, 1.75f, -1.25f},
            {0.0005f, 0.33f, -1.f, 0.5f}}};
        for (std::size_t i = 0u; i < result.size(); ++i) {
            std::copy(positions[i].begin(), positions[i].end(), result[i].positionScale);
            std::copy(angles[i].begin(), angles[i].end(), result[i].yawPhaseIdLod);
            std::copy(extra[i].begin(), extra[i].end(), result[i].extraTransform);
            vegetation::setInstanceMetadata(result[i],
                13u + i,
                31u + i,
                201u - i,
                3u,
                i,
                2u,
                static_cast<std::uint16_t>(0x7fc1u + i));
            vegetation::packInstanceMetadataFloat(result[i].speciesLodState[2], 0x80000000u);
            vegetation::packInstanceMetadataFloat(result[i].speciesLodState[3], 1u + i);
        }
        return result;
    }

    static Probe probe()
    {
        return {{2.f, -3.f, 5.f, 1.f},
            {2.f, 3.f, -4.f, 0.f},
            {-3.f, 2.f, 1.f, -1.f},
            {1u, 2u, 0u, 0u},
            {0u, 1u, 2u, 1u},
            {0.7f, 0.4f, 0.2f, 0.3f},
            {{0.6f, 0.f, 0.8f, 4.5f},
                {1.25f, 0.45f, 0.7f, 0.3f},
                {7.f, 0.6f, 64.f, 0.9f},
                {0.f, 0.f, 0.f, 0.f}},
            {{-0.8f, 0.f, 0.6f, 2.f},
                {0.75f, 0.1f, 0.5f, 0.2f},
                {3.f, 0.1f, 32.f, 0.6f},
                {3.f, 0.f, 7.f, 0.f}}};
    }

    static Instances ordinary0() { return instances(); }

    static Probe ordinary1() { return probe(); }

    static core::Mat4 rotation(const vegetation::InstanceDataGpu& instance)
    {
        const auto yaw = core::Mat4::rotation(
            core::Quat::fromAxisAngle({0.f, 1.f, 0.f}, instance.yawPhaseIdLod[0]));
        const float pitch = instance.extraTransform[0], roll = instance.extraTransform[1];
        if (std::abs(pitch) <= 0.001f && std::abs(roll) <= 0.001f)
            return yaw;
        return core::multiply(core::Mat4::rotation(core::Quat::fromAxisAngle({0, 0, 1}, roll)),
            core::multiply(core::Mat4::rotation(core::Quat::fromAxisAngle({1, 0, 0}, pitch)), yaw));
    }

    static core::Vec3 position(const vegetation::InstanceDataGpu& instance, const Probe& p)
    {
        const float uniform = instance.positionScale[3];
        const float scaleY =
            instance.extraTransform[2] > 0.f ? instance.extraTransform[2] : uniform;
        const auto transformed = core::transformVector(rotation(instance),
            {p.position[0] * uniform, p.position[1] * scaleY, p.position[2] * uniform});
        return transformed
            + core::Vec3{instance.positionScale[0],
                instance.positionScale[1] + instance.extraTransform[3],
                instance.positionScale[2]};
    }

    // Frozen float64 reference from tests/contract/foliage_geometry_reference.py.
    // These are authored-input arithmetic, never recorded GPU output.
    static constexpr std::array<std::array<float, 12u>, 3u> windReference{{{9.472304696f,
                                                                               -9.74835654f,
                                                                               13.01046912f,
                                                                               1.f,
                                                                               9.397221556f,
                                                                               -9.749019668f,
                                                                               12.91531334f,
                                                                               1.f,
                                                                               9.377173989f,
                                                                               -9.749589135f,
                                                                               12.91022627f,
                                                                               1.f},
        {-10.20378517f,
            -2.715489626f,
            12.30985675f,
            1.f,
            -10.28205141f,
            -2.715489626f,
            12.34722558f,
            1.f,
            -10.20278007f,
            -2.715489626f,
            12.30266317f,
            1.f},
        {25.10004926f,
            -3.146384239f,
            -5.487728185f,
            1.f,
            25.06062063f,
            -3.146384239f,
            -5.403589296f,
            1.f,
            25.08599216f,
            -3.146384239f,
            -5.448131935f,
            1.f}}};

    static Numeric expected()
    {
        const auto values = instances();
        const auto p = probe();
        const auto vertex = position(values[p.selectors[0] + p.selectors[1]], p);
        Numeric result{};
        for (std::size_t pixel = 0u; pixel < 4u; ++pixel) {
            const auto& instance = values[p.selectors[0] + p.offsets[pixel]];
            const auto point = position(instance, p);
            const auto r = rotation(instance);
            const auto normal =
                core::normalize(core::transformVector(r, {p.normal[0], p.normal[1], p.normal[2]}));
            const auto tangent =
                core::transformVector(r, {p.tangent[0], p.tangent[1], p.tangent[2]});
            const std::array<float, 16u> geometry{vertex.x,
                vertex.y,
                vertex.z,
                1.f,
                point.x,
                point.y,
                point.z,
                1.f,
                normal.x,
                normal.y,
                normal.z,
                0.f,
                tangent.x,
                tangent.y,
                tangent.z,
                p.tangent[3]};
            const auto base = pixel * 44u;
            std::copy(geometry.begin(), geometry.end(), result.begin() + base);
            std::memcpy(result.data() + base + 16u, instance.speciesLodState, 16u);
            std::copy_n(instance.yawPhaseIdLod, 4u, result.begin() + base + 20u);
            std::copy_n(windReference[2u].begin(), 4u, result.begin() + base + 24u);
            const auto& wind = windReference[p.offsets[pixel]];
            std::copy(wind.begin(), wind.end(), result.begin() + base + 28u);
            std::copy_n(geometry.begin() + 4u, 4u, result.begin() + base + 40u);
        }
        return result;
    }

    static bool matches(const Numeric& actual) noexcept
    {
        const auto oracle = expected();
        for (std::size_t i = 0u; i < actual.size(); ++i) {
            if (i % 44u >= 16u && i % 44u < 20u) {
                if (vegetation::unpackInstanceMetadataFloat(actual[i])
                    != vegetation::unpackInstanceMetadataFloat(oracle[i]))
                    return false;
            } else if (!std::isfinite(actual[i])
                || std::abs(actual[i] - oracle[i]) > tolerance * (1.f + std::abs(oracle[i])))
                return false;
        }
        return true;
    }

    static void verify(const Numeric& actual)
    {
        if (!matches(actual)) {
            const auto oracle = expected();
            for (std::size_t i = 0u; i < actual.size(); ++i)
                std::fprintf(stderr,
                    "Instance64 lane%zu actual%.9g/%08x expected%.9g/%08x\n",
                    i,
                    actual[i],
                    vegetation::unpackInstanceMetadataFloat(actual[i]),
                    oracle[i],
                    vegetation::unpackInstanceMetadataFloat(oracle[i]));
        }
        assert(matches(actual));
        std::puts(
            "BINDLESS_GRAPHICS_INSTANCE64_NUMERIC176_OK base1 last3 normal tangent metadata-bits wind-current-previous grass0.5 canopy2 rigid tolerance2e-5");
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
