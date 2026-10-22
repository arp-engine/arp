#pragma once

#include "core/math/math.h"

#include <array>
#include <cstddef>

namespace arp::renderer::cloud {

struct alignas(16) CloudGpuConstants {
    core::Mat4 inverseViewProjection;
    core::Mat4 previousViewProjection;
    core::Vec4 cameraTime;
    // Camera-centre vector and double-evaluated |camera-centre|^2 - R^2.
    core::Vec4 cameraRadial;
    core::Vec4 shell;
    core::Vec4 albedoAnisotropy;
    core::Vec4 periodsCoverage;
    core::Vec4 windErosion;
    core::Vec4 sunDirection;
    core::Vec4 sunRadiance;
    core::Vec4 ambientRadiance;
    std::array<std::uint32_t, 4> dimensions{};
    std::array<std::uint32_t, 4> sampling{};
    std::array<std::uint32_t, 4> assetLayout{};
    core::Vec4 temporal;
    core::Vec4 previousCamera;
    core::Vec4 jitter;
};

struct alignas(16) CloudGpuSample {
    core::Vec4 radianceTransmittance;
    core::Vec4 metadata;
};

static_assert(sizeof(CloudGpuConstants) == 368u);
static_assert(offsetof(CloudGpuConstants, cameraTime) == 128u);
static_assert(offsetof(CloudGpuConstants, dimensions) == 272u);
static_assert(offsetof(CloudGpuConstants, jitter) == 352u);
static_assert(sizeof(CloudGpuSample) == 32u);

} // namespace arp::renderer::cloud
