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

// CloudReconstruction/1 uses a separate binding. CloudGpuConstants/1 remains
// 368 bytes for existing raymarch, temporal and composite artifacts.
struct alignas(16) CloudReconstructionGpuConstants {
    std::uint32_t contractVersion = 0u;
    float maxRadianceRange = 0.f, maxTransmittanceRange = 0.f;
    std::uint32_t reserved = 0u;
};

static_assert(sizeof(CloudGpuConstants) == 368u);
static_assert(offsetof(CloudGpuConstants, cameraTime) == 128u);
static_assert(offsetof(CloudGpuConstants, dimensions) == 272u);
static_assert(offsetof(CloudGpuConstants, jitter) == 352u);
static_assert(sizeof(CloudGpuSample) == 32u);
static_assert(sizeof(CloudReconstructionGpuConstants) == 16u);
static_assert(offsetof(CloudReconstructionGpuConstants, maxRadianceRange) == 4u);
static_assert(offsetof(CloudReconstructionGpuConstants, maxTransmittanceRange) == 8u);

} // namespace arp::renderer::cloud
