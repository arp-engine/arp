#include "renderer/volumetric_cloud/cloud_reference.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace arp::renderer::cloud {
namespace {
float ramp(float lo, float hi, float value) noexcept
{
    const auto t = std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool positive(core::Vec3 v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && v.x >= 0 && v.y >= 0
        && v.z >= 0;
}
} // namespace

CloudResult CloudReference::configure(
    const assets::cloud::CloudAsset& asset, const scene3d::CloudLayerDesc& layer)
{
    asset_ = nullptr;
    const auto a = assets::cloud::CloudAssetCooker::validate(asset);
    if (!a.success)
        return CloudResult::failure(a.diagnostic);
    const auto l = CloudContract::validate(layer);
    if (!l.success)
        return l;
    layer_ = layer;
    asset_ = &asset;
    return {};
}

float CloudReference::density(core::Vec3 world, float time) const noexcept
{
    if (!asset_ || !std::isfinite(time) || !std::isfinite(world.x) || !std::isfinite(world.y)
        || !std::isfinite(world.z))
        return std::numeric_limits<float>::quiet_NaN();
    const auto& d = layer_;
    const double x = double(world.x) - d.planetCenter.x;
    const double y = double(world.y) - d.planetCenter.y;
    const double z = double(world.z) - d.planetCenter.z;
    const auto h =
        float((std::sqrt(x * x + y * y + z * z) - d.planetRadius - d.baseAltitude) / d.thickness);
    if (h <= 0 || h >= 1 || d.coverage <= 0)
        return 0;
    world = world - core::scale(d.windVelocity, time);
    const auto weather = assets::cloud::CloudAssetCooker::sampleWeather(
        *asset_, {world.x / d.weatherPeriod, world.z / d.weatherPeriod});
    const auto type = std::clamp(weather.y, 0.0f, 1.0f) * 2.0f;
    const auto index = std::min(std::uint32_t(type), 1u);
    const auto p = core::scale(asset_->cells[index], 1.0f - (type - float(index)))
        + core::scale(asset_->cells[index + 1u], type - float(index));
    const float height = ramp(p.x, p.y, h) * (1.0f - ramp(p.z, p.w, h));
    const auto coverage = std::clamp(weather.x * d.coverage, 0.0f, 1.0f);
    const auto shape = assets::cloud::CloudAssetCooker::sampleShape(
        *asset_, core::scale(world, 1.0f / d.shapePeriod));
    const auto base =
        coverage > 0 ? std::clamp((shape - (1.0f - coverage)) / coverage, 0.0f, 1.0f) : 0.0f;
    const auto detail = assets::cloud::CloudAssetCooker::sampleDetail(
        *asset_, core::scale(world, 1.0f / d.detailPeriod));
    return std::clamp(base - d.erosion * detail * (1.0f - base), 0.0f, 1.0f) * height * weather.z;
}

CloudResult CloudReference::integrate(const CloudRay& ray,
    std::uint32_t viewSteps,
    std::uint32_t lightSteps,
    core::Vec3 sun,
    core::Vec3 sunRadiance,
    core::Vec3 ambient,
    float time,
    volume::MediumIntegral& output,
    float samplePhase,
    float* extinctionDistance) const noexcept
{
    output = {};
    if (extinctionDistance)
        *extinctionDistance = static_cast<float>(ray.maxDistance);
    if (!asset_ || !viewSteps || !lightSteps || !std::isfinite(time) || !std::isfinite(samplePhase)
        || samplePhase < 0.f || samplePhase >= 1.f || !std::isfinite(core::length(sun))
        || std::fabs(core::length(sun) - 1.0f) > .0001f || !positive(sunRadiance)
        || !positive(ambient))
        return CloudResult::failure("CLOUD-REFERENCE-INPUT");
    CloudRayIntervals intervals;
    const auto hit = CloudContract::intersect(layer_, ray, intervals);
    if (!hit.success)
        return hit;
    const auto phase = volume::henyeyGreenstein(core::dot(ray.direction, sun), layer_.anisotropy);
    double moment = 0.0;
    double momentWeight = 0.0;
    double momentTransmittance = 1.0;
    for (std::uint32_t segment = 0; segment < intervals.count; ++segment) {
        const auto span = intervals.intervals[segment];
        const auto step = (span.end - span.begin) / viewSteps;
        for (std::uint32_t i = 0; i < viewSteps; ++i) {
            const auto distance = span.begin + (i + samplePhase) * step;
            const auto position = ray.origin + core::scale(ray.direction, float(distance));
            const auto sigma = layer_.extinction * density(position, time);
            if (sigma <= 0)
                continue;
            float visibility = 1;
            if (core::length(sunRadiance) > 0) {
                // An infinite sun ray blocked by the opaque planet has zero
                // visibility. Use an explicit sphere quadratic in double.
                const double x = double(position.x) - layer_.planetCenter.x;
                const double y = double(position.y) - layer_.planetCenter.y;
                const double z = double(position.z) - layer_.planetCenter.z;
                const auto b = x * sun.x + y * sun.y + z * sun.z;
                const auto c =
                    x * x + y * y + z * z - double(layer_.planetRadius) * layer_.planetRadius;
                if (b < 0 && b * b >= c)
                    visibility = 0;
                else {
                    CloudRayIntervals light;
                    const auto l = CloudContract::intersect(
                        layer_, {position, sun, std::numeric_limits<double>::max()}, light);
                    if (!l.success)
                        return l;
                    double opticalDepth = 0;
                    for (std::uint32_t s = 0; s < light.count; ++s) {
                        const auto interval = light.intervals[s];
                        const auto ds = (interval.end - interval.begin) / lightSteps;
                        for (std::uint32_t j = 0; j < lightSteps; ++j)
                            opticalDepth += layer_.extinction
                                * density(position
                                        + core::scale(sun, float(interval.begin + (j + .5) * ds)),
                                    time)
                                * ds;
                    }
                    visibility = float(std::exp(-opticalDepth));
                }
            }
            const auto incident = ambient + core::scale(sunRadiance, phase * visibility);
            const core::Vec3 source{sigma * layer_.scatteringAlbedo.x * incident.x,
                sigma * layer_.scatteringAlbedo.y * incident.y,
                sigma * layer_.scatteringAlbedo.z * incident.z};
            const auto integrated = volume::homogeneous(sigma, source, float(step));
            const double segmentAbsorption = -std::expm1(-double(sigma) * step);
            const double absorbed = momentTransmittance * segmentAbsorption;
            moment += absorbed * distance;
            momentWeight += absorbed;
            momentTransmittance *= std::exp(-double(sigma) * step);
            output = volume::compose(output, integrated);
        }
    }
    if (extinctionDistance && output.transmittance < 1.f && momentWeight > 0.0)
        *extinctionDistance = static_cast<float>(moment / momentWeight);
    return {};
}

} // namespace arp::renderer::cloud
