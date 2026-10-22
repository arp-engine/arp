#pragma once

#include "3d/scene_3d_render_queue.h"
#include "rhi/gpu_capabilities.h"

#include <array>
#include <cmath>
#include <limits>

namespace arp::renderer::fog {

inline constexpr std::uint32_t kFogContractVersion = 1u;
enum class FogQuality { Low, Medium, High };

struct FogConfig {
    FogQuality quality = FogQuality::Medium;
    std::uint32_t pixelsPerCell = 0u;
    std::uint32_t depthSlices = 0u;
    std::uint32_t maxLocalVolumes = 64u;
    std::uint32_t framesInFlight = 2u;
    std::uint64_t gpuBudgetBytes = 512ull * 1024u * 1024u;
    float distance = 0.0f;
    float historyWeight = 0.9f;
    float mediumRejection = 0.15f;
    float depthRejection = 0.02f;
    bool temporal = true;
};

struct FogGrid {
    std::uint32_t width = 0u, height = 0u, depth = 0u;
    std::uint64_t cells = 0u, budgetBytes = 0u;
};

struct FogResult {
    bool success = true;
    core::pmr::String diagnostic;

    static FogResult failure(const char* id) { return {false, core::pmr::String(id)}; }
};

struct alignas(16) FogGpuVolume {
    core::Mat4 worldToLocal;
    core::Vec4 scatteringDensity;
    core::Vec4 shapeEdgeScaleStrength;
    core::Vec4 velocity; // xyz flow, w FogDensityNoiseContrast/1 exponent
    std::array<std::uint32_t, 4> seed{};
};

static_assert(sizeof(FogGpuVolume) == 128u);
static_assert(
    offsetof(FogGpuVolume, scatteringDensity) == 64u && offsetof(FogGpuVolume, seed) == 112u);

struct alignas(16) FogConstants {
    core::Mat4 inverseViewProjection;
    core::Mat4 view;
    core::Mat4 previousViewProjection;
    core::Mat4 previousView;
    std::array<core::Mat4, 4> shadowMatrices;
    std::array<core::Vec4, 4> shadowRects;
    core::Vec4 cascadeEnds;
    std::array<std::uint32_t, 4> grid{};
    core::Vec4 depthTime;
    core::Vec4 cameraPosition;
    core::Vec4 globalMedium;
    core::Vec4 globalColorAnisotropy;
    core::Vec4 lightDirectionIntensity;
    core::Vec4 lightColor;
    core::Vec4 temporal;
    core::Vec4 extentJitter;
    std::array<std::uint32_t, 4> counts{};
    // xyz selection origin, w: 0 view depth, 1 distance from origin.
    core::Vec4 shadowSelection;
    core::Vec4 ambientRadiance;
};

static_assert(sizeof(FogConstants) == 784u);
static_assert(
    offsetof(FogConstants, shadowMatrices) == 256u && offsetof(FogConstants, shadowRects) == 512u);
static_assert(offsetof(FogConstants, grid) == 592u && offsetof(FogConstants, depthTime) == 608u);
static_assert(offsetof(FogConstants, counts) == 736u);
static_assert(offsetof(FogConstants, shadowSelection) == 752u);
static_assert(offsetof(FogConstants, ambientRadiance) == 768u);

inline float integratedLength(float extinction, float distance) noexcept
{
    return extinction == 0.0f ? distance : -std::expm1(-extinction * distance) / extinction;
}

inline float henyeyGreenstein(float cosine, float g) noexcept
{
    const float d = 1.0f + g * g - 2.0f * g * cosine;
    return (1.0f - g * g) / (4.0f * 3.14159265358979323846f * d * std::sqrt(d));
}

FogResult planFog(const FogConfig& config,
    rhi::Extent2D extent,
    const rhi::DeviceCapabilities& capabilities,
    FogGrid& grid);
FogResult buildFogVolumes(
    const scene3d::Scene3DRenderQueue& queue, core::pmr::Vector<FogGpuVolume>& volumes);

} // namespace arp::renderer::fog
