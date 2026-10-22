#include "renderer/volumetric_cloud/cloud_reference.h"
#include "rhi/semantics/render_target_coordinates.h"
#include "tests/fixtures/cloud/cloud_fixture.h"
#include "tests/fixtures/cloud/cloud_graphics_probe.h"
#include "tests/fixtures/renderdoc_capture.h"

#include <cmath>
#include <iostream>

using namespace arp;
using namespace renderer::cloud;

namespace {
void density(rhi::GpuDevice& device,
    bool retained,
    const std::shared_ptr<rhi_vulkan::VulkanRhiRuntime>& runtime)
{
    const auto asset = tests::cloud::asset();
    const auto layer = tests::cloud::layer();
    constexpr rhi::Extent2D extent{128, 128};
    tests::cloud::ComputeProbe probe(device,
        "cloud_density_slice",
        extent,
        asset.cells.size() * sizeof(core::Vec4),
        false,
        retained);
    CloudGpuConstants p{};
    p.cameraTime = {0, 20, 0, 10};
    const double radius = layer.planetRadius;
    const double radial = radius + p.cameraTime.y;
    p.cameraRadial = {0, float(radial), 0, float(radial * radial - radius * radius)};
    p.shell = {layer.baseAltitude, layer.thickness, layer.planetRadius, layer.extinction};
    p.windErosion = {
        layer.windVelocity.x, layer.windVelocity.y, layer.windVelocity.z, layer.erosion};
    p.periodsCoverage = {
        layer.weatherPeriod, layer.shapePeriod, layer.detailPeriod, layer.coverage};
    p.dimensions = {extent.width, extent.height, extent.width, extent.height};
    p.sampling = {24, 4, asset.source.weatherDimension, asset.source.shapeDimension};
    const auto shape = assets::cloud::kCloudHeightProfileCount
        + asset.source.weatherDimension * asset.source.weatherDimension;
    p.assetLayout = {asset.source.detailDimension,
        0,
        shape,
        shape
            + asset.source.shapeDimension * asset.source.shapeDimension
                * asset.source.shapeDimension};
    const auto actual = probe.run(p, asset.cells.data());
    CloudReference reference;
    assert(reference.configure(asset, layer).success);
    float worst = 0, minimum = 1, maximum = 0;
    for (unsigned y = 0; y < extent.height; ++y)
        for (unsigned x = 0; x < extent.width; ++x) {
            const core::Vec3 world{(x + .5f) / extent.width * layer.weatherPeriod,
                layer.baseAltitude + layer.thickness * .5f,
                (y + .5f) / extent.height * layer.weatherPeriod};
            const auto expected = reference.density(world, p.cameraTime.w);
            const auto value = actual[y * extent.width + x].radianceTransmittance.x;
            assert(std::isfinite(value) && value >= 0 && value <= 1);
            worst = std::max(worst, std::fabs(value - expected));
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
    assert(worst < .0001f && maximum - minimum > .01f);
    std::cout << "Cloud density retained=" << retained << " GPU/CPU maxError=" << worst
              << " range=[" << minimum << ',' << maximum << "]\n";
    if (retained)
        probe.verifyRetainedRetirement(runtime, false);
}

void temporal(rhi::GpuDevice& device,
    bool retained,
    const std::shared_ptr<rhi_vulkan::VulkanRhiRuntime>& runtime)
{
    constexpr rhi::Extent2D extent{8, 8};
    constexpr auto bytes = extent.width * extent.height * sizeof(CloudGpuSample);
    tests::cloud::ComputeProbe probe(device, "cloud_temporal", extent, bytes, true, retained);
    core::pmr::Vector<CloudGpuSample> raw(64), old(64);
    const auto direction = core::normalize(core::Vec3{.2f, .3f, 1});
    const core::Vec3 camera{300, 1700, -800};
    for (unsigned fixture = 0; fixture < 6; ++fixture) {
        const bool ortho = fixture != 0;
        const float dolly = fixture >= 2 ? 50.0f : 0.0f;
        const auto current = camera + core::scale(direction, dolly);
        auto projection = ortho ? core::orthographic(1200, 900, 50, 20000)
                                : core::perspective(60, 4.0f / 3, 50, 20000);
        rhi::semantics::applyProjectionYConvention(projection.m.data(), device.backend());
        auto previousVp = core::multiply(projection, core::lookAt(camera, camera + direction));
        auto vp = core::multiply(projection, core::lookAt(current, current + direction));
        rhi::semantics::applyRenderTargetSamplingYConvention(previousVp.m.data(), device.backend());
        rhi::semantics::applyRenderTargetSamplingYConvention(vp.m.data(), device.backend());
        CloudGpuConstants p{};
        p.inverseViewProjection = core::inverse(vp);
        p.previousViewProjection = previousVp;
        p.cameraTime = {current.x, current.y, current.z, 1};
        p.previousCamera = {camera.x, camera.y, camera.z, fixture == 5 ? 0.0f : 1.0f};
        p.dimensions = {8, 8, 8, 8};
        p.temporal = {.9f, 1, 10000, 1};
        p.jitter = {.5f, .5f, .5f, ortho ? 1.0f : 0.0f};
        for (unsigned i = 0; i < 64; ++i) {
            const float value = ((i % 8 + i / 8) % 2) ? 1.0f : 0.0f;
            raw[i] = {{value, value, value, .7f}, {500, 2000, 0, 0}};
            old[i] = {{.5f, .5f, .5f, .7f}, {500 + dolly, 2000 + dolly, 0, 0}};
            if (fixture == 3)
                old[i].metadata.x += 100; // Disoccluded cloud rejects history.
            if (fixture == 4)
                old[i].metadata.y += 100; // Changed opaque depth rejects history.
        }
        constexpr unsigned index = 4 * 8 + 4;
        raw[index].radianceTransmittance = {.2f, .2f, .2f, .7f};
        const auto result = probe.run(p, raw.data(), old.data());
        const float expected = fixture < 3 ? .47f : .2f;
        const auto error = std::fabs(result[index].radianceTransmittance.x - expected);
        if (error >= .0001f)
            std::cerr << "Cloud temporal fixture=" << fixture
                      << " value=" << result[index].radianceTransmittance.x
                      << " expected=" << expected << '\n';
        assert(error < .0001f);
    }
    std::cout << "Cloud temporal retained=" << retained
              << " GPU perspective/ortho/dolly/depth-rejection/reset passed\n";
    if (retained)
        probe.verifyRetainedRetirement(runtime, true);
}

void raymarch(rhi::GpuDevice& device,
    const std::shared_ptr<rhi_vulkan::VulkanRhiRuntime>& runtime,
    bool dropWrapper,
    bool graphics = false)
{
    const auto asset = tests::cloud::asset();
    const auto layer = tests::cloud::layer();
    const auto config = tests::cloud::config();
    constexpr rhi::Extent2D extent{16, 12};
    tests::cloud::ComputeProbe probe(device,
        "cloud_raymarch",
        extent,
        asset.cells.size() * sizeof(core::Vec4),
        false,
        true,
        true);
    CloudReference reference;
    assert(reference.configure(asset, layer).success);
    float worst = 0;
    unsigned visible = 0, identity = 0;
    for (unsigned fixture = 0; fixture < 18u; ++fixture) {
        const unsigned altitude = fixture % 3u;
        const bool ortho = (fixture / 3u) % 2u != 0u;
        const float depth = fixture < 6u ? 1.0f : fixture < 12u ? .99f : .9995f;
        // Keep the entire orthographic near plane above the opaque planet.
        const core::Vec3 camera{float(13u * fixture),
            altitude == 0u       ? (ortho ? 300.0f : 10.0f)
                : altitude == 1u ? 1600.0f
                                 : 4000.0f,
            -57};
        const auto view =
            core::lookAt(camera, camera + core::Vec3{.25f, altitude == 2u ? -.6f : .6f, 1});
        auto projection = ortho
            ? core::orthographic(200, 150, 1, 20000)
            : core::perspective(57, float(extent.width) / extent.height, 1, 20000);
        rhi::semantics::applyProjectionYConvention(projection.m.data(), device.backend());
        auto vp = core::multiply(projection, view);
        rhi::semantics::applyRenderTargetSamplingYConvention(vp.m.data(), device.backend());
        CloudGpuConstants p{};
        p.inverseViewProjection = core::inverse(vp);
        p.cameraTime = {camera.x, camera.y, camera.z, 10};
        const double rx = double(camera.x) - layer.planetCenter.x;
        const double ry = double(camera.y) - layer.planetCenter.y;
        const double rz = double(camera.z) - layer.planetCenter.z;
        const double radius = layer.planetRadius;
        p.cameraRadial = {
            float(rx), float(ry), float(rz), float(rx * rx + ry * ry + rz * rz - radius * radius)};
        p.shell = {layer.baseAltitude, layer.thickness, layer.planetRadius, layer.extinction};
        p.albedoAnisotropy = {layer.scatteringAlbedo.x,
            layer.scatteringAlbedo.y,
            layer.scatteringAlbedo.z,
            layer.anisotropy};
        p.periodsCoverage = {
            layer.weatherPeriod, layer.shapePeriod, layer.detailPeriod, layer.coverage};
        p.windErosion = {
            layer.windVelocity.x, layer.windVelocity.y, layer.windVelocity.z, layer.erosion};
        const auto sun = core::normalize(core::Vec3{1, 1, -.2f});
        const core::Vec3 sunlight{2, 1.8f, 1.6f}, ambient{.1f, .2f, .3f};
        p.sunDirection = {sun.x, sun.y, sun.z, 0};
        p.sunRadiance = {sunlight.x, sunlight.y, sunlight.z, 0};
        p.ambientRadiance = {ambient.x, ambient.y, ambient.z, 0};
        p.dimensions = {extent.width, extent.height, extent.width, extent.height};
        p.sampling = {config.viewSteps,
            config.lightSteps,
            asset.source.weatherDimension,
            asset.source.shapeDimension};
        const auto shape = assets::cloud::kCloudHeightProfileCount
            + asset.source.weatherDimension * asset.source.weatherDimension;
        p.assetLayout = {asset.source.detailDimension,
            0u,
            shape,
            shape
                + asset.source.shapeDimension * asset.source.shapeDimension
                    * asset.source.shapeDimension};
        p.temporal = {config.historyWeight, config.historyDepthTolerance, config.maxDistance, 10};
        p.jitter = {.5f, .5f, .5f, ortho ? 1.0f : 0.0f};
        const auto actual = probe.run(p, asset.cells.data(), nullptr, depth);
        unsigned cloudy = 0;
        for (unsigned y = 0; y < extent.height; ++y)
            for (unsigned x = 0; x < extent.width; ++x) {
                const float u = (x + .5f) / extent.width, v = (y + .5f) / extent.height;
                const auto nearPoint =
                    core::transformPoint(p.inverseViewProjection, {u * 2 - 1, v * 2 - 1, 0});
                const auto farPoint =
                    core::transformPoint(p.inverseViewProjection, {u * 2 - 1, v * 2 - 1, 1});
                const auto origin = ortho ? nearPoint : camera;
                const auto direction = core::normalize(farPoint - nearPoint);
                const auto opaque =
                    core::transformPoint(p.inverseViewProjection, {u * 2 - 1, v * 2 - 1, depth});
                const float distance = depth >= 1
                    ? config.maxDistance
                    : std::max(0.0f, core::dot(opaque - origin, direction));
                renderer::volume::MediumIntegral expected;
                const auto referenceResult =
                    reference.integrate({origin, direction, std::min(config.maxDistance, distance)},
                        config.viewSteps,
                        config.lightSteps,
                        sun,
                        sunlight,
                        ambient,
                        p.cameraTime.w,
                        expected);
                if (!referenceResult.success)
                    std::cerr << "retained raymarch reference fixture=" << fixture << " pixel=" << x
                              << ',' << y << ' ' << referenceResult.diagnostic << '\n';
                assert(referenceResult.success);
                const auto value = actual[y * extent.width + x].radianceTransmittance;
                assert(std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z)
                    && std::isfinite(value.w) && value.x >= 0 && value.y >= 0 && value.z >= 0
                    && value.w >= 0 && value.w <= 1);
                const auto error = std::max({std::fabs(value.x - expected.radiance.x),
                    std::fabs(value.y - expected.radiance.y),
                    std::fabs(value.z - expected.radiance.z),
                    std::fabs(value.w - expected.transmittance)});
                if (error > .002f)
                    std::cerr << "retained raymarch fixture=" << fixture << " pixel=" << x << ','
                              << y << " error=" << error << " actualT=" << value.w
                              << " expectedT=" << expected.transmittance << '\n';
                assert(error <= .002f);
                worst = std::max(worst, error);
                cloudy += value.w < .999f;
            }
        visible += cloudy;
        identity += extent.width * extent.height - cloudy;
        if (fixture == 0u || fixture == 2u)
            assert(cloudy > 0u);
        if (fixture == 6u || fixture == 8u)
            assert(cloudy == 0u);
        std::cout << "Cloud retained depth raymarch fixture=" << fixture << " ortho=" << ortho
                  << " depth=" << depth << " cloudy=" << cloudy << " maxError=" << worst << '\n';
        if (graphics) {
            tests::cloud::GraphicsProbe::verify(
                probe, actual, runtime, dropWrapper, fixture == 17u);
            if (fixture == 17u) {
                tests::cloud::ComputeProbe quarantined(device,
                    "cloud_raymarch",
                    extent,
                    asset.cells.size() * sizeof(core::Vec4),
                    false,
                    true,
                    true);
                const auto duplicate = quarantined.run(p, asset.cells.data(), nullptr, depth);
                tests::cloud::GraphicsProbe::verify(
                    quarantined, duplicate, runtime, dropWrapper, true, true);
            }
        } else if (fixture == 17u)
            probe.verifyRetainedTemporalChain(p, actual, runtime, dropWrapper);
    }
    assert(visible && identity);
    if (!graphics)
        probe.verifyRetainedRetirement(runtime, dropWrapper);
}
} // namespace

int main(int argc, char** argv)
{
    tests::VulkanHeadlessRuntime native(false, tests::cloud::GraphicsProbe::resolve);
    if (!native.runtime() || !native.runtime()->valid())
        return 77;
    try {
        auto device =
            tests::VulkanHeapOwnerFixture::makeDevice(native.runtime(), true, 128u * 1024u * 1024u);
        assert(device);
        tests::RenderDocCaptureScope capture;
        const bool retained = argc == 2 && std::string_view(argv[1]) == "--retained";
        const bool graphics = argc == 2 && std::string_view(argv[1]) == "--retained-graphics";
        if (graphics) {
            raymarch(*device, native.runtime(), false, true);
            raymarch(*device, native.runtime(), true, true);
            capture.end();
            return 0;
        }
        density(*device, retained, native.runtime());
        temporal(*device, retained, native.runtime());
        if (retained) {
            raymarch(*device, native.runtime(), false);
            raymarch(*device, native.runtime(), true);
        }
        capture.end();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
