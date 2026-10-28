#include "renderer/volumetric_cloud/cloud_frame_parameters.h"

#include "core/math/matrix_validation.h"
#include "renderer/temporal/jitter.h"
#include "renderer/volumetric_cloud/volumetric_cloud_feature.h"
#include "rhi/semantics/render_target_coordinates.h"

#include <cmath>
#include <limits>

namespace arp::renderer::cloud {
namespace {
bool finite(core::Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool radiance(core::Vec3 value) noexcept
{
    return finite(value) && value.x >= 0 && value.y >= 0 && value.z >= 0;
}

bool validInverse(const core::Mat4& matrix, const core::Mat4& inverse) noexcept
{
    return core::isApproximateInverse(matrix, inverse, 0.002f);
}

bool same(core::Vec4 a, core::Vec4 b) noexcept
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

} // namespace

CloudResult CloudFrameParameters::build(const CloudGraphContext& c,
    rhi::Backend backend,
    const CloudRenderConfig& config,
    const CloudExecutionPlan& plan,
    const assets::cloud::CloudCookDesc& asset,
    std::uint32_t samplingFrame,
    const CloudFrameState* previous,
    CloudFrameState& output) noexcept
{
    if (previous == &output)
        return CloudResult::failure("CLOUD-FRAME-STATE-ALIAS");
    output = {};
    if (!c.cameraId)
        return CloudResult::failure("CLOUD-CAMERA-IDENTITY");
    std::uint32_t assetCells = 0u;
    const auto assetPlan = assets::cloud::CloudAssetCooker::plan(asset, assetCells);
    if (!assetPlan.success)
        return CloudResult::failure(assetPlan.diagnostic);
    CloudExecutionPlan described;
    const auto describedResult = CloudExecutionPlanner::describeResources(config,
        plan.extent,
        std::uint64_t(assetCells) * sizeof(core::Vec4),
        sizeof(CloudGpuConstants),
        described);
    if (!describedResult.success)
        return describedResult;
    if (plan.sampleExtent.width != described.sampleExtent.width
        || plan.sampleExtent.height != described.sampleExtent.height
        || plan.sampleCount != described.sampleCount
        || plan.sampleBufferBytes != described.sampleBufferBytes
        || plan.dispatchGroups != described.dispatchGroups)
        return CloudResult::failure("CLOUD-PREPARED-GRAPH-PLAN");
    const auto layerValid = CloudContract::validate(c.layer);
    if (!layerValid.success)
        return layerValid;
    if (!finite(c.cameraPosition) || !finite(c.sunDirection) || !radiance(c.sunRadiance)
        || !radiance(c.ambientRadiance) || !std::isfinite(c.time)
        || std::fabs(core::length(c.sunDirection) - 1.0f) > 0.0001f)
        return CloudResult::failure("CLOUD-FRAME-INPUT");
    if (!std::isfinite(c.projectionJitter.x) || !std::isfinite(c.projectionJitter.y))
        return CloudResult::failure("CLOUD-CAMERA-JITTER");
    auto vp = core::multiply(c.projection, c.view);
    const auto rasterViewProjection = vp;
    rhi::semantics::applyRenderTargetSamplingYConvention(vp.m.data(), backend);
    const auto inv = core::inverse(vp);
    if (!validInverse(vp, inv))
        return CloudResult::failure("CLOUD-CAMERA-MATRIX");
    auto canonical = c.projection;
    canonical.m[2] -= c.projectionJitter.x;
    canonical.m[6] -= c.projectionJitter.y;
    const auto& d = c.layer;
    const double rx = double(c.cameraPosition.x) - d.planetCenter.x;
    const double ry = double(c.cameraPosition.y) - d.planetCenter.y;
    const double rz = double(c.cameraPosition.z) - d.planetCenter.z;
    const double groundC = rx * rx + ry * ry + rz * rz - double(d.planetRadius) * d.planetRadius;
    if (!std::isfinite(groundC) || groundC < 0 || groundC > std::numeric_limits<float>::max()
        || !std::isfinite(d.planetRadius * d.planetRadius)
        || !std::isfinite((d.baseAltitude + d.thickness)
            * (2.0f * d.planetRadius + d.baseAltitude + d.thickness)))
        return CloudResult::failure("CLOUD-CAMERA-SHELL-RANGE");
    auto& p = output.parameters;
    output.rasterViewProjection = rasterViewProjection;
    output.layer = c.layer;
    p = {};
    p.inverseViewProjection = inv;
    p.cameraTime = {c.cameraPosition.x, c.cameraPosition.y, c.cameraPosition.z, c.time};
    p.cameraRadial = {float(rx), float(ry), float(rz), float(groundC)};
    p.shell = {d.baseAltitude, d.thickness, d.planetRadius, d.extinction};
    p.albedoAnisotropy = {
        d.scatteringAlbedo.x, d.scatteringAlbedo.y, d.scatteringAlbedo.z, d.anisotropy};
    p.periodsCoverage = {d.weatherPeriod, d.shapePeriod, d.detailPeriod, d.coverage};
    p.windErosion = {d.windVelocity.x, d.windVelocity.y, d.windVelocity.z, d.erosion};
    p.sunDirection = {c.sunDirection.x, c.sunDirection.y, c.sunDirection.z, 0};
    p.sunRadiance = {c.sunRadiance.x, c.sunRadiance.y, c.sunRadiance.z, 0};
    p.ambientRadiance = {c.ambientRadiance.x, c.ambientRadiance.y, c.ambientRadiance.z, 0};
    p.dimensions = {
        plan.sampleExtent.width, plan.sampleExtent.height, plan.extent.width, plan.extent.height};
    p.sampling = {
        config.viewSteps, config.lightSteps, asset.weatherDimension, asset.shapeDimension};
    const auto shapeOffset =
        assets::cloud::kCloudHeightProfileCount + asset.weatherDimension * asset.weatherDimension;
    p.assetLayout = {asset.detailDimension,
        0u,
        shapeOffset,
        shapeOffset + asset.shapeDimension * asset.shapeDimension * asset.shapeDimension};
    // CloudTemporalSampling/1: sixteen Halton base-5 stratified phases along
    // each primary ray. Fixed XY centres preserve the opaque depth samples.
    // Only successful submission advances this sequence; reference is midpoint.
    constexpr std::uint32_t samplingPeriod = 16u;
    const auto sampleZ =
        config.temporal ? core::halton235(samplingFrame % samplingPeriod + 1u).z : 0.5f;
    p.jitter = {0.5f, 0.5f, sampleZ, c.orthographic ? 1.0f : 0.0f};
    const auto& old = previous ? *previous : output;
    const auto& o = old.parameters;
    const bool history = previous && config.temporal && !c.cameraCut && c.cameraId == old.cameraId
        && p.dimensions == o.dimensions && p.sampling == o.sampling
        && p.assetLayout == o.assetLayout && config.maxDistance == o.temporal.z
        && c.sceneRevision == old.sceneRevision && c.time >= o.cameraTime.w
        && canonical.m == old.canonicalProjection.m && p.jitter.w == o.jitter.w
        && old.planetCenter.x == d.planetCenter.x && old.planetCenter.y == d.planetCenter.y
        && old.planetCenter.z == d.planetCenter.z && same(p.shell, o.shell)
        && same(p.albedoAnisotropy, o.albedoAnisotropy)
        && same(p.periodsCoverage, o.periodsCoverage) && same(p.windErosion, o.windErosion)
        && same(p.sunDirection, o.sunDirection) && same(p.sunRadiance, o.sunRadiance)
        && same(p.ambientRadiance, o.ambientRadiance);
    p.previousViewProjection = history ? old.viewProjection : vp;
    p.previousCamera = history
        ? core::Vec4{o.cameraTime.x, o.cameraTime.y, o.cameraTime.z, 1.0f}
        : core::Vec4{c.cameraPosition.x, c.cameraPosition.y, c.cameraPosition.z, 0.0f};
    p.temporal = {config.historyWeight,
        config.historyDepthTolerance,
        config.maxDistance,
        history ? o.cameraTime.w : c.time};
    output.viewProjection = vp;
    output.canonicalProjection = canonical;
    output.sceneRevision = c.sceneRevision;
    output.planetCenter = d.planetCenter;
    output.cameraId = c.cameraId;
    output.historyUsed = history;
    return {};
}

} // namespace arp::renderer::cloud
