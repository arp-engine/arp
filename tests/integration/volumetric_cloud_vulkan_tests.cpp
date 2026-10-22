#include "core/math/half_float.h"
#include "renderer/volumetric_cloud/cloud_reference.h"
#include "renderer/volumetric_cloud/volumetric_cloud_feature.h"
#include "rhi/semantics/render_target_coordinates.h"
#include "tests/fixtures/cloud/cloud_fixture.h"
#include "tests/fixtures/renderdoc_capture.h"
#include "tests/fixtures/vulkan_heap_owner_fixture.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace arp;
using namespace renderer::cloud;

int main()
{
    tests::VulkanHeadlessRuntime native;
    if (!native.runtime() || !native.runtime()->valid())
        return 77;
    try {
        auto config = tests::cloud::config();
        config.temporal = false;
        const rhi::semantics::DirectObjectBudget directBudget{
            rhi::semantics::kDirectObjectBudgetContractVersion,
            "Cloud.ReferenceDirectObjects/1",
            kCloudDirectPipelineEntries,
            config.framesInFlight * kCloudDescriptorElementsPerSlot};
        auto device = tests::VulkanHeapOwnerFixture::makeDevice(
            native.runtime(), true, 128u * 1024u * 1024u, &directBudget);
        assert(device);
        // Compare the L2 publication with the real device query, not fixture
        // defaults or a capability snapshot with a rewritten backend label.
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(native.runtime()->physicalDevice(), &properties);
        const auto limits = device->capabilities().computeResources;
        assert(limits.maxComputeWorkgroupSizeZ == properties.limits.maxComputeWorkGroupSize[2]);
        assert(limits.maxComputeWorkgroupCountX == properties.limits.maxComputeWorkGroupCount[0]);
        assert(limits.maxComputeWorkgroupCountY == properties.limits.maxComputeWorkGroupCount[1]);
        assert(limits.maxComputeWorkgroupCountZ == properties.limits.maxComputeWorkGroupCount[2]);
        assert(limits.maxUniformBufferBindingsPerStage
            == properties.limits.maxPerStageDescriptorUniformBuffers);
        assert(limits.maxUniformBufferBindingSizeBytes == properties.limits.maxUniformBufferRange);
        assert(limits.maxSampledTextureBindingsPerStage
            == properties.limits.maxPerStageDescriptorSampledImages);
        assert(limits.maxTexture2DDimension == properties.limits.maxImageDimension2D);
        auto asset = tests::cloud::asset();
        const auto base = assets::cloud::kCloudHeightProfileCount;
        const auto shape = base + asset.source.weatherDimension * asset.source.weatherDimension;
        const auto detail = shape
            + asset.source.shapeDimension * asset.source.shapeDimension
                * asset.source.shapeDimension;
        // Explicit deterministic homogeneous-density fixture: ramp edges are
        // narrow enough for every untruncated midpoint to be in the flat part.
        for (unsigned i = 0; i < base; ++i) {
            asset.cells[i] = {0, .001f, .999f, 1};
            asset.source.heightProfiles[i] = asset.cells[i];
        }
        for (unsigned i = base; i < shape; ++i)
            asset.cells[i] = {1, 0, 1, 0};
        for (unsigned i = shape; i < detail; ++i)
            asset.cells[i] = {1, 0, 0, 0};
        for (std::size_t i = detail; i < asset.cells.size(); ++i)
            asset.cells[i] = {};
        assert(assets::cloud::CloudAssetCooker::validate(asset).success);
        constexpr rhi::Extent2D extent{32, 24};
        VolumetricCloudFeature feature;
        auto result = feature.create({*device, extent, config, asset, ARP_CLOUD_SHADER_DIR});
        if (!result.success)
            std::cerr << result.diagnostic << '\n';
        assert(result.success);
        const auto verifyDirectBudget = [&] {
            const auto ledger = device->physicalBudgetLedger();
            assert(ledger.valid());
            const auto& pipelines =
                ledger.category(rhi::semantics::PhysicalBudgetCategory::DirectPipelineObjects);
            const auto& descriptors =
                ledger.category(rhi::semantics::PhysicalBudgetCategory::DirectDescriptorArenas);
            assert(pipelines.observed() && pipelines.current == 6u && pipelines.peak == 6u
                && !pipelines.rejectionCount);
            assert(descriptors.observed() && descriptors.current == directBudget.descriptorCapacity
                && descriptors.peak == directBudget.descriptorCapacity
                && !descriptors.rejectionCount);
        };
        verifyDirectBudget();
        const auto bytes = feature.plan().sampleBufferBytes;
        auto staging = core::shareUnique(device->createBuffer({"Cloud.TestReadback",
            bytes,
            rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::GpuToCpu}));
        assert(staging);
        const auto colorBytes = std::uint64_t(extent.width) * extent.height * 8u;
        auto colorStaging = core::shareUnique(device->createBuffer({"Cloud.TestColorReadback",
            colorBytes,
            rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::GpuToCpu}));
        assert(colorStaging);
        const auto layer = tests::cloud::layer();
        CloudReference reference;
        assert(reference.configure(asset, layer).success);
        tests::RenderDocCaptureScope capture;
        float worst = 0;
        for (unsigned frame = 0; frame < 19u; ++frame) {
            if (frame == 6u) {
                feature.release();
                asset = tests::cloud::asset();
                assert(
                    feature.create({*device, extent, config, asset, ARP_CLOUD_SHADER_DIR}).success);
            }
            render_graph::FrameGraph graph(*device);
            const auto color = graph.createTransientTexture({"Cloud.TestBackground",
                {extent.width, extent.height, 1},
                rhi::TextureFormat::Rgba16Float,
                rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled});
            const auto depth = graph.createTransientTexture({"Cloud.TestDepth",
                {extent.width, extent.height, 1},
                rhi::TextureFormat::D32Float,
                rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled});
            graph.addPass("Cloud.TestClear")
                .writeAttachment(color,
                    rhi::ResourceState::ColorAttachment,
                    rhi::LoadOp::Clear,
                    rhi::StoreOp::Store,
                    {},
                    {.3f, .5f, .8f, 1})
                .writeAttachment(depth,
                    rhi::ResourceState::DepthAttachment,
                    rhi::LoadOp::Clear,
                    rhi::StoreOp::Store)
                .executeRenderPass([](render_graph::RenderPassContext&) {});
            // An explicitly authored opaque plane at constant raster depth.
            // This exercises depth clipping before, within and beyond the shell
            // without introducing a native-API test path or depth fallback.
            const float depthValue = frame < 13u ? 1.0f : frame < 16u ? .99f : .9995f;
            if (frame >= 13u)
                graph.addPass("Cloud.TestOpaqueDepth")
                    .write(depth, rhi::ResourceState::DepthAttachment)
                    .execute([&, depthValue](render_graph::RenderContext& rc) {
                        rhi::RenderPassDesc pass;
                        pass.label = "Cloud.TestOpaqueDepth";
                        pass.hasDepthStencilAttachment = true;
                        pass.depthStencilAttachment = {"OpaqueDepth",
                            graph.texture(depth).id(),
                            rhi::TextureFormat::D32Float,
                            rhi::LoadOp::Clear,
                            rhi::StoreOp::Store};
                        pass.depthStencilAttachment.depthClearValue = depthValue;
                        auto encoder = rc.commands().beginRenderPass(pass);
                        assert(encoder && encoder->end().success);
                    });
            CloudGraphContext c{graph, color, depth};
            c.layer = layer;
            if (frame == 12u)
                c.layer.coverage = 0;
            assert(reference.configure(asset, c.layer).success);
            // Asymmetric cameras exercise actual uploaded matrix multiplication,
            // Earth-radius shell precision and below/inside/above intervals.
            c.cameraPosition = {float(13u * frame),
                frame % 3u == 0u       ? 10.0f
                    : frame % 3u == 1u ? 1600.0f
                                       : 4000.0f,
                -57};
            c.view = core::lookAt(c.cameraPosition,
                c.cameraPosition + core::Vec3{.25f, frame % 3u == 2u ? -.6f : .6f, 1});
            c.projection = core::perspective(57, float(extent.width) / extent.height, 1, 20000);
            if (frame >= 9u && frame < 12u) {
                c.orthographic = true;
                c.projection = core::orthographic(200, 150, 1, 20000);
                if (frame == 9u) {
                    c.cameraPosition.y = 300;
                    c.view =
                        core::lookAt(c.cameraPosition, c.cameraPosition + core::Vec3{.25f, .6f, 1});
                }
            }
            rhi::semantics::applyProjectionYConvention(c.projection.m.data(), device->backend());
            c.sunDirection = core::normalize(core::Vec3{1, 1, -.2f});
            c.ambientRadiance = {.1f, .2f, .3f};
            // Ambient-only analytic fixtures first; then real self-shadow and
            // deterministic cooked noise exercise the same reference equations.
            c.sunRadiance = frame >= 3u ? core::Vec3{2, 1.8f, 1.6f} : core::Vec3{};
            CloudOutputs output;
            result = feature.contributeGraph(c, output);
            if (!result.success)
                std::cerr << result.diagnostic << '\n';
            assert(result.success && !output.historyUsed);
            const auto readback = graph.importBufferRef(staging);
            graph.addPass("Cloud.TestCopySamples")
                .read(output.samples, rhi::ResourceState::CopySrc)
                .write(readback, rhi::ResourceState::CopyDst)
                .externalSideEffects()
                .execute([&](render_graph::RenderContext& rc) {
                    auto transfer = rc.commands().beginTransferPass({"Cloud.TestCopySamples"});
                    assert(transfer
                        && transfer
                            ->copyBufferToBuffer(
                                {graph.buffer(output.samples).id(), staging->id(), 0, 0, bytes})
                            .success);
                    assert(transfer->end().success);
                });
            graph.addPass("Cloud.TestKeepColor")
                .read(output.color, rhi::ResourceState::CopySrc)
                .write(graph.importBufferRef(colorStaging), rhi::ResourceState::CopyDst)
                .externalSideEffects()
                .execute([&](render_graph::RenderContext& rc) {
                    auto transfer = rc.commands().beginTransferPass({"Cloud.TestCopyColor"});
                    rhi::TextureToBufferCopyDesc copy;
                    copy.sourceTexture = graph.texture(output.color).id();
                    copy.destinationBuffer = colorStaging->id();
                    copy.bytesPerRow = extent.width * 8u;
                    copy.rowsPerImage = extent.height;
                    copy.extent = {extent.width, extent.height, 1u};
                    assert(transfer && transfer->copyTextureToBuffer(copy).success
                        && transfer->end().success);
                });
            graph.execute();
            device->waitIdle(); // Test-only readback synchronization.
            verifyDirectBudget();
            const auto payload = rhi_vulkan::vulkanRhiNativePayload(*staging);
            core::pmr::String diagnostic;
            assert(payload && payload->backendInvalidateMappedRead(0, bytes, diagnostic));
            core::pmr::Vector<CloudGpuSample> actual(feature.plan().sampleCount);
            assert(payload->backendMappedData());
            std::memcpy(actual.data(), payload->backendMappedData(), bytes);
            const auto colorPayload = rhi_vulkan::vulkanRhiNativePayload(*colorStaging);
            assert(colorPayload
                && colorPayload->backendInvalidateMappedRead(0, colorBytes, diagnostic));
            core::pmr::Vector<std::uint16_t> colors(colorBytes / 2u);
            std::memcpy(colors.data(), colorPayload->backendMappedData(), colorBytes);
            auto vp = core::multiply(c.projection, c.view);
            rhi::semantics::applyRenderTargetSamplingYConvention(vp.m.data(), device->backend());
            const auto inverse = core::inverse(vp);
            unsigned cloudy = 0, referenceCloudy = 0;
            for (unsigned y = 0; y < extent.height; ++y)
                for (unsigned x = 0; x < extent.width; ++x) {
                    const float u = (x + .5f) / extent.width, v = (y + .5f) / extent.height;
                    const auto nearPoint = core::transformPoint(inverse, {u * 2 - 1, v * 2 - 1, 0});
                    const auto farPoint = core::transformPoint(inverse, {u * 2 - 1, v * 2 - 1, 1});
                    const auto origin = c.orthographic ? nearPoint : c.cameraPosition;
                    const auto direction = core::normalize(farPoint - nearPoint);
                    const auto opaquePoint =
                        core::transformPoint(inverse, {u * 2 - 1, v * 2 - 1, depthValue});
                    const auto distance = depthValue >= 1
                        ? config.maxDistance
                        : std::max(0.0f, core::dot(opaquePoint - origin, direction));
                    renderer::volume::MediumIntegral expected;
                    const auto referenceResult = reference.integrate(
                        {origin, direction, std::min(config.maxDistance, distance)},
                        config.viewSteps,
                        config.lightSteps,
                        c.sunDirection,
                        c.sunRadiance,
                        c.ambientRadiance,
                        0,
                        expected);
                    if (!referenceResult.success) {
                        std::cerr << "reference frame=" << frame << " pixel=" << x << ',' << y
                                  << ' ' << referenceResult.diagnostic << '\n';
                        return 1;
                    }
                    const auto value = actual[y * extent.width + x].radianceTransmittance;
                    assert(std::isfinite(value.x) && std::isfinite(value.y)
                        && std::isfinite(value.z) && std::isfinite(value.w) && value.x >= 0
                        && value.y >= 0 && value.z >= 0 && value.w >= 0 && value.w <= 1);
                    const auto error = std::max({std::fabs(value.x - expected.radiance.x),
                        std::fabs(value.y - expected.radiance.y),
                        std::fabs(value.z - expected.radiance.z),
                        std::fabs(value.w - expected.transmittance)});
                    if (error > .002f) {
                        std::cerr << "frame=" << frame << " pixel=" << x << ',' << y
                                  << " error=" << error << " actualT=" << value.w
                                  << " expectedT=" << expected.transmittance << '\n';
                        return 1;
                    }
                    worst = std::max(worst, error);
                    const auto expectedColor =
                        renderer::volume::composite(expected, {.3f, .5f, .8f});
                    const auto offset = (y * extent.width + x) * 4u;
                    const auto colorError =
                        std::max({std::fabs(core::halfToFloat(colors[offset]) - expectedColor.x),
                            std::fabs(core::halfToFloat(colors[offset + 1u]) - expectedColor.y),
                            std::fabs(core::halfToFloat(colors[offset + 2u]) - expectedColor.z)});
                    assert(colorError < .002f);
                    cloudy += value.w < .999f;
                    referenceCloudy += expected.transmittance < .999f;
                }
            // Pixel classifications near T=.999 can differ despite bounded
            // floating-point error; numeric per-pixel L/T above is the oracle.
            if (frame < 12u)
                assert(cloudy > 0u);
            if (frame == 12u || frame == 15u)
                assert(cloudy == 0u);
            std::cout << "Cloud Vulkan frame=" << frame << " cloudy=" << cloudy
                      << " referenceCloudy=" << referenceCloudy << " maxError=" << worst << '\n';
        }
        capture.end();
        feature.release();
        const auto releasedBudget = device->physicalBudgetLedger();
        assert(
            !releasedBudget.category(rhi::semantics::PhysicalBudgetCategory::DirectPipelineObjects)
                .current);
        assert(
            !releasedBudget.category(rhi::semantics::PhysicalBudgetCategory::DirectDescriptorArenas)
                .current);
        const auto stats = rhi_vulkan::inspectVulkanRhiNativeResourceStats(*device);
        std::cout << "Cloud Vulkan numeric maxError=" << worst
                  << " native dispatches=" << stats.nativeExecutableDispatchRecordCount << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
