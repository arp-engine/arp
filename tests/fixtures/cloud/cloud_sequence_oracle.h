#pragma once

#include "renderer/volumetric_cloud/cloud_gpu_contract.h"
#include "renderer/volumetric_cloud/cloud_reconstruction.h"
#include "renderer/volumetric_cloud/cloud_reference.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace arp::tests::cloud {

// The fixed clip-space triangle in cloud_foreground.slang, transformed by an
// explicitly authored viewport. Acceptance dimensions keep edges off centres.
struct ForegroundFixture {
    bool enabled = false;
    float x = 0, y = 0, width = 0, height = 0;
    static constexpr std::array<float, 4> color{.75f, .125f, .0625f, .875f};
    static constexpr std::array<float, 4> background{.125f, .375f, .75f, .625f};

    bool covers(unsigned px, unsigned py) const
    {
        if (!enabled)
            return false;
        assert(width > 0 && height > 0);
        const float u = 2 * (px + .5f - x) / width - 1;
        const float v = 2 * (py + .5f - y) / height - 1;
        return v > -.75f && v < .75f && std::fabs(u) < (.75f - v) * .5f;
    }
};

// CPU-only reference state. Previous samples come exclusively from the prior
// CPU integration, never from GPU readback or an uploaded history fixture.
class SequenceOracle final {
public:
    renderer::cloud::CloudExecutionPlan plan;
    renderer::cloud::CloudGpuConstants parameters;
    scene3d::CloudLayerDesc layer;
    ForegroundFixture foreground;
    std::array<float, 4u> foregroundColor = ForegroundFixture::color;
    bool sceneOpaque = false;
    std::array<float, 16u> sceneRasterViewProjection{};
    std::array<float, 36u> sceneOpaqueVertices{};
    // Sampling UVs can use the opposite Y orientation to raster coverage.
    bool flipForegroundY = false;
    bool requireOcclusionChange = true;
    renderer::cloud::CloudReconstructionConfig reconstruction;
    bool fullResolved = false;
    std::uint32_t eligible = 0u, refined = 0u;
    core::pmr::Vector<std::uint8_t> skipHistory;
    core::pmr::Vector<renderer::cloud::CloudGpuSample> previous, raw, resolved, current;
    std::uint32_t accepted = 0u, opaqueRejected = 0u, disoccluded = 0u, newlyOccluded = 0u;

    renderer::cloud::CloudRay ray(float u, float v) const
    {
        const auto& p = parameters;
        const auto nearPoint =
            core::transformPoint(p.inverseViewProjection, {2 * u - 1, 2 * v - 1, 0});
        const auto farPoint =
            core::transformPoint(p.inverseViewProjection, {2 * u - 1, 2 * v - 1, 1});
        const core::Vec3 camera{p.cameraTime.x, p.cameraTime.y, p.cameraTime.z};
        return {p.jitter.w > 0 ? nearPoint : camera,
            core::normalize(farPoint - nearPoint),
            p.temporal.z};
    }

    float opaqueDistance(float u, float v) const
    {
        const auto x = std::min(unsigned(u * plan.extent.width), plan.extent.width - 1);
        const auto y = std::min(unsigned(v * plan.extent.height), plan.extent.height - 1);
        if (!foreground.covers(x, flipForegroundY ? plan.extent.height - 1u - y : y))
            return parameters.temporal.z;
        const auto surface =
            core::transformPoint(parameters.inverseViewProjection, {2 * u - 1, 2 * v - 1, .5f});
        const auto r = ray(u, v);
        return std::max(0.f, core::dot(surface - r.origin, r.direction));
    }

    void build(const assets::cloud::CloudAsset& asset)
    {
        using namespace renderer::cloud;
        const auto& p = parameters;
        previous = std::move(resolved);
        raw.resize(plan.sampleCount);
        CloudReference reference;
        assert(reference.configure(asset, layer).success);
        for (unsigned y = 0u; y < plan.sampleExtent.height; ++y)
            for (unsigned x = 0u; x < plan.sampleExtent.width; ++x) {
                const auto u = (x + .5f) / plan.sampleExtent.width;
                const auto v = (y + .5f) / plan.sampleExtent.height;
                auto r = ray(u, v);
                const auto opaque = opaqueDistance(u, v);
                r.maxDistance = std::min(r.maxDistance, double(opaque));
                renderer::volume::MediumIntegral integral;
                float moment = p.temporal.z;
                assert(reference
                        .integrate(r,
                            p.sampling[0],
                            p.sampling[1],
                            {p.sunDirection.x, p.sunDirection.y, p.sunDirection.z},
                            {p.sunRadiance.x, p.sunRadiance.y, p.sunRadiance.z},
                            {p.ambientRadiance.x, p.ambientRadiance.y, p.ambientRadiance.z},
                            p.cameraTime.w,
                            integral,
                            p.jitter.z,
                            &moment)
                        .success);
                // No extinction means no representative cloud point, even
                // when an opaque surface truncates this ray before the shell.
                if (integral.transmittance == 1.f)
                    moment = p.temporal.z;
                raw[y * plan.sampleExtent.width + x] = {{integral.radiance.x,
                                                            integral.radiance.y,
                                                            integral.radiance.z,
                                                            integral.transmittance},
                    {moment, opaque, 0, 0}};
            }
        resolved = raw;
        fullResolved = reconstruction.contractVersion != 0u;
        if (fullResolved)
            buildSpatial(reference);
        current = resolved;
        accepted = 0u;
        opaqueRejected = disoccluded = newlyOccluded = 0u;
        if (p.previousCamera.w <= 0)
            return;
        assert(previous.size() == resolved.size());
        const auto oldInverse = core::inverse(p.previousViewProjection);
        const auto previousDistance = [&](core::Vec3 point) {
            if (p.jitter.w <= 0)
                return core::length(
                    point - core::Vec3{p.previousCamera.x, p.previousCamera.y, p.previousCamera.z});
            const auto clip = core::transformHomogeneous(
                p.previousViewProjection, {point.x, point.y, point.z, 1});
            // Independent geometric derivation, rather than the shader's
            // affine clip-Z/depth-row calculation.
            const auto nearPoint =
                core::transformPoint(oldInverse, {clip.x / clip.w, clip.y / clip.w, 0});
            const auto farPoint =
                core::transformPoint(oldInverse, {clip.x / clip.w, clip.y / clip.w, 1});
            return core::dot(point - nearPoint, core::normalize(farPoint - nearPoint));
        };
        const auto grid = fullResolved ? plan.extent : plan.sampleExtent;
        for (unsigned y = 0u; y < grid.height; ++y)
            for (unsigned x = 0u; x < grid.width; ++x) {
                const auto index = y * grid.width + x;
                if (fullResolved && skipHistory[index])
                    continue;
                const auto sample = resolved[index];
                const auto u = (x + .5f) / grid.width;
                const auto v = (y + .5f) / grid.height;
                const auto r = ray(u, v);
                const auto world = r.origin + core::scale(r.direction, sample.metadata.x)
                    - core::scale(core::Vec3{p.windErosion.x, p.windErosion.y, p.windErosion.z},
                        p.cameraTime.w - p.temporal.w);
                const auto clip = core::transformHomogeneous(
                    p.previousViewProjection, {world.x, world.y, world.z, 1});
                if (clip.w <= 0)
                    continue;
                const auto oldU = .5f * (clip.x / clip.w + 1), oldV = .5f * (clip.y / clip.w + 1);
                if (oldU < 0 || oldV < 0 || oldU >= 1 || oldV >= 1)
                    continue;
                const auto oldIndex =
                    unsigned(oldV * grid.height) * grid.width + unsigned(oldU * grid.width);
                const auto& old = previous[oldIndex];
                const bool currentSky = sample.metadata.y >= p.temporal.z;
                const bool previousSky = old.metadata.y >= p.temporal.z;
                const auto opaque =
                    previousDistance(r.origin + core::scale(r.direction, sample.metadata.y));
                const bool sameOpaque = (currentSky && previousSky)
                    || (!currentSky && !previousSky && opaque >= 0
                        && std::fabs(old.metadata.y - opaque) <= p.temporal.y);
                opaqueRejected += !sameOpaque;
                disoccluded += currentSky && !previousSky;
                newlyOccluded += !currentSky && previousSky;
                if (!sameOpaque
                    || std::fabs(old.metadata.x - previousDistance(world)) > p.temporal.y)
                    continue;
                auto lower = components(sample.radianceTransmittance), upper = lower;
                const auto bx =
                    fullResolved ? int(std::floor(u * plan.sampleExtent.width - .5f)) : int(x);
                const auto by =
                    fullResolved ? int(std::floor(v * plan.sampleExtent.height - .5f)) : int(y);
                for (int dy = -1; dy <= (fullResolved ? 2 : 1); ++dy)
                    for (int dx = -1; dx <= (fullResolved ? 2 : 1); ++dx) {
                        const auto nx = std::clamp(bx + dx, 0, int(plan.sampleExtent.width) - 1);
                        const auto ny = std::clamp(by + dy, 0, int(plan.sampleExtent.height) - 1);
                        const auto value = components(
                            raw[ny * plan.sampleExtent.width + nx].radianceTransmittance);
                        for (unsigned c = 0; c < 4; ++c) {
                            lower[c] = std::min(lower[c], value[c]);
                            upper[c] = std::max(upper[c], value[c]);
                        }
                    }
                const auto current = components(sample.radianceTransmittance);
                auto value = components(old.radianceTransmittance);
                for (unsigned c = 0; c < 4; ++c)
                    value[c] = current[c]
                        + p.temporal.x * (std::clamp(value[c], lower[c], upper[c]) - current[c]);
                resolved[index].radianceTransmittance = {value[0], value[1], value[2], value[3]};
                ++accepted;
            }
    }

    // Independent CPU geometry and physical integration. No GPU values enter
    // the classifier, the representative moment or the previous-frame oracle.
    void buildSpatial(renderer::cloud::CloudReference& reference)
    {
        using namespace renderer::cloud;
        const auto& p = parameters;
        assert(CloudReconstruction::validate(reconstruction).success);
        resolved.resize(std::size_t(plan.extent.width) * plan.extent.height);
        skipHistory.assign(resolved.size(), 0u);
        eligible = refined = 0u;
        for (unsigned y = 0; y < plan.extent.height; ++y)
            for (unsigned x = 0; x < plan.extent.width; ++x) {
                const auto index = y * plan.extent.width + x;
                const float u = (x + .5f) / plan.extent.width, v = (y + .5f) / plan.extent.height;
                auto r = ray(u, v);
                const auto opaque = opaqueDistance(u, v);
                r.maxDistance = std::min(r.maxDistance, double(opaque));
                auto& result = resolved[index];
                result = {{0, 0, 0, 1}, {p.temporal.z, opaque, 0, 0}};
                CloudRayIntervals intervals;
                assert(CloudContract::intersect(layer, r, intervals).success);
                if (!intervals.count) {
                    skipHistory[index] = 1u;
                    continue;
                }
                ++eligible;
                const float gx = u * plan.sampleExtent.width - .5f,
                            gy = v * plan.sampleExtent.height - .5f;
                const int bx = int(std::floor(gx)), by = int(std::floor(gy));
                const auto cell = [&](int dx, int dy) -> const CloudGpuSample& {
                    return raw[std::clamp(by + dy, 0, int(plan.sampleExtent.height) - 1)
                            * plan.sampleExtent.width
                        + std::clamp(bx + dx, 0, int(plan.sampleExtent.width) - 1)];
                };
                std::array<float, 4> lower{}, upper{};
                bool refine = false, first = true;
                for (int dy = -1; dy <= 2; ++dy)
                    for (int dx = -1; dx <= 2; ++dx) {
                        const auto& s = cell(dx, dy);
                        const auto value = components(s.radianceTransmittance);
                        for (unsigned c = 0; c < 4; ++c) {
                            lower[c] = first ? value[c] : std::min(lower[c], value[c]);
                            upper[c] = first ? value[c] : std::max(upper[c], value[c]);
                        }
                        first = false;
                        refine |= std::fabs(s.metadata.y - opaque) > p.temporal.y;
                    }
                for (unsigned c = 0; c < 4; ++c)
                    refine |= upper[c] - lower[c] > (c == 3 ? reconstruction.maxTransmittanceRange
                                                            : reconstruction.maxRadianceRange);
                if (refine) {
                    ++refined;
                    skipHistory[index] = 1u;
                    renderer::volume::MediumIntegral integral;
                    float moment = p.temporal.z;
                    assert(reference
                            .integrate(r,
                                p.sampling[0],
                                p.sampling[1],
                                {p.sunDirection.x, p.sunDirection.y, p.sunDirection.z},
                                {p.sunRadiance.x, p.sunRadiance.y, p.sunRadiance.z},
                                {p.ambientRadiance.x, p.ambientRadiance.y, p.ambientRadiance.z},
                                p.cameraTime.w,
                                integral,
                                p.jitter.z,
                                &moment)
                            .success);
                    result.radianceTransmittance = {integral.radiance.x,
                        integral.radiance.y,
                        integral.radiance.z,
                        integral.transmittance};
                    result.metadata.x = integral.transmittance < 1 ? moment : p.temporal.z;
                    continue;
                }
                std::array<float, 4> sum{};
                float total = 0, moment = 0, extinction = 0;
                const float fx = gx - bx, fy = gy - by;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        const auto& s = cell(dx, dy);
                        const auto value = components(s.radianceTransmittance);
                        const auto weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy)
                            / (1 + std::fabs(s.metadata.y - opaque) / p.temporal.y);
                        for (unsigned c = 0; c < 4; ++c)
                            sum[c] += weight * value[c];
                        total += weight;
                        const float absorbed = weight * (1 - value[3]);
                        moment += absorbed * s.metadata.x;
                        extinction += absorbed;
                    }
                result.radianceTransmittance = {
                    sum[0] / total, sum[1] / total, sum[2] / total, sum[3] / total};
                result.metadata.x = extinction > 0 ? moment / extinction : p.temporal.z;
            }
    }

    std::array<float, 4> integral(unsigned x, unsigned y) const
    {
        const auto& p = parameters;
        const float u = (x + .5f) / plan.extent.width, v = (y + .5f) / plan.extent.height;
        auto r = ray(u, v);
        const auto opaque = opaqueDistance(u, v);
        r.maxDistance = std::min(r.maxDistance, double(opaque));
        renderer::cloud::CloudRayIntervals spans;
        assert(renderer::cloud::CloudContract::intersect(layer, r, spans).success);
        if (!spans.count)
            return {0, 0, 0, 1};
        if (fullResolved)
            return components(resolved[y * plan.extent.width + x].radianceTransmittance);
        const float gx = u * plan.sampleExtent.width - .5f, gy = v * plan.sampleExtent.height - .5f;
        const auto bx = int(std::floor(gx)), by = int(std::floor(gy));
        const float fx = gx - bx, fy = gy - by;
        std::array<float, 4> integral{};
        float total = 0;
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                const auto nx = std::clamp(bx + dx, 0, int(plan.sampleExtent.width) - 1);
                const auto ny = std::clamp(by + dy, 0, int(plan.sampleExtent.height) - 1);
                const auto& sample = resolved[ny * plan.sampleExtent.width + nx];
                const auto value = components(sample.radianceTransmittance);
                const auto weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy)
                    / (1 + std::fabs(sample.metadata.y - opaque) / p.temporal.y);
                total += weight;
                for (unsigned c = 0; c < 4; ++c)
                    integral[c] += weight * value[c];
            }
        assert(total > 0);
        for (auto& value : integral)
            value /= total;
        return integral;
    }

    std::array<float, 4> hdr(unsigned x, unsigned y) const
    {
        const auto value = integral(x, y);
        const auto& scene =
            foreground.covers(x, y) ? foregroundColor : ForegroundFixture::background;
        return {value[0] + value[3] * scene[0],
            value[1] + value[3] * scene[1],
            value[2] + value[3] * scene[2],
            scene[3]};
    }

    static std::array<float, 4> components(core::Vec4 v) { return {v.x, v.y, v.z, v.w}; }
};
} // namespace arp::tests::cloud
