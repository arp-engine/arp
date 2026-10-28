#include "3d/scene_3d.h"
#include "renderer/volumetric_cloud/volumetric_cloud_feature.h"
#include "rhi/backends/null/null_backend.h"
#include "tests/fixtures/capability_override_device.h"
#include "tests/fixtures/cloud/cloud_fixture.h"
#include "tests/fixtures/terrain_vegetation_delayed_queue.h"

#include <cassert>
#include <iostream>
#include <limits>

using namespace arp;
using namespace renderer::cloud;

namespace {
scene3d::Scene3DRenderQueue sceneCloudQueue()
{
    scene::Scene scene;
    auto& cloud = scene.createObject("cloud").addComponent<scene3d::VolumetricCloudComponent>();
    scene3d::CloudDesc desc;
    desc.layer = tests::cloud::layer();
    desc.densityResource = "cloud/reference";
    assert(cloud.set_cloud(desc));
    const auto extracted =
        scene3d::Scene3DRenderExtractor{}.extract(scene, scene.extractTraversalSnapshot({}));
    assert(extracted.ok() && extracted.queue().hasCloud());
    const auto visible = scene3d::Scene3DVisibilitySystem{}.filterVisible(extracted.queue(), {});
    assert(visible.ok() && visible.queue().hasCloud());
    assert(visible.queue().cloud().densityResource == desc.densityResource);
    return visible.queue();
}

// Replay real Null validator commands immediately but delay the observer fence.
// The existing ownership-only delayed queue intentionally does not replay, so
// it alone cannot publish cross-frame logical resource states for this test.
class CloudDelayedQueue final : public rhi::GpuQueue {
public:
    rhi::GpuQueue* replay = nullptr;
    tests::DelayedValidatorQueue delayed;

    rhi::GpuQueueRole role() const override { return delayed.role(); }

    std::uint32_t submitCount() const override { return delayed.submitCount(); }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& c) override
    {
        return submit(c, nullptr);
    }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& c, rhi::GpuFence* fence) override
    {
        assert(replay && replay->submit(c).success);
        return delayed.submit(c, fence);
    }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& c,
        rhi::GpuTimelineSemaphore*,
        std::uint64_t,
        rhi::GpuTimelineSemaphore*,
        std::uint64_t,
        rhi::GpuFence* fence) override
    {
        return submit(c, fence);
    }

    void complete() { delayed.complete(); }
};

CloudGraphContext makeFrame(render_graph::FrameGraph& graph, float time)
{
    const auto color = graph.createTransientTexture({"Cloud.TestColor",
        {64, 48, 1},
        rhi::TextureFormat::Rgba16Float,
        rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled});
    const auto depth = graph.createTransientTexture({"Cloud.TestDepth",
        {64, 48, 1},
        rhi::TextureFormat::D32Float,
        rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled});
    graph.addPass("Cloud.TestClear")
        .writeAttachment(
            color, rhi::ResourceState::ColorAttachment, rhi::LoadOp::Clear, rhi::StoreOp::Store)
        .writeAttachment(
            depth, rhi::ResourceState::DepthAttachment, rhi::LoadOp::Clear, rhi::StoreOp::Store)
        .executeRenderPass([](render_graph::RenderPassContext&) {});
    CloudGraphContext c{graph, color, depth};
    c.layer = tests::cloud::layer();
    c.cameraPosition = {0, 10, -8};
    c.view = core::lookAt(c.cameraPosition, {0, 500, 800});
    c.projection = core::perspective(60, 64.0f / 48, .1f, 20000);
    c.sunDirection = core::normalize(core::Vec3{1, 1, -1});
    c.sunRadiance = {10, 10, 10};
    c.ambientRadiance = {.1f, .15f, .2f};
    c.time = time;
    return c;
}

void admission(rhi::GpuDevice& inner)
{
    const std::array<tests::CapabilityOverrideDevice::Mutator, 4> missing = {
        [](rhi::DeviceCapabilities& c) {
            c.computeResources.maxComputeWorkgroupCountX = 0;
        },
        [](rhi::DeviceCapabilities& c) {
            c.computeResources.maxUniformBufferBindingSizeBytes = 0;
        },
        [](rhi::DeviceCapabilities& c) {
            c.computeResources.maxSampledTextureBindingsPerStage = 0;
        },
        [](rhi::DeviceCapabilities& c) {
            c.computeResources.maxTexture2DDimension = 0;
        }};
    const auto asset = tests::cloud::asset();
    const auto config = tests::cloud::config();
    for (auto mutate : missing) {
        tests::CapabilityOverrideDevice device(inner, mutate);
        const auto before = inner.memoryStats();
        VolumetricCloudFeature feature;
        const auto result = feature.create({device, {64, 48}, config, asset, ARP_CLOUD_SHADER_DIR});
        assert(!result.success && !feature.created() && !feature.frameReady());
        assert(!device.bufferCreateAttempts());
        const auto after = inner.memoryStats();
        assert(before.allocatedBytes == after.allocatedBytes
            && before.liveResourceCount == after.liveResourceCount);
    }
}

void lifecycle(rhi::GpuDevice& inner)
{
    // Logical Null queue completion only; this does not prove native ordering.
    tests::CapabilityOverrideDevice device(inner, nullptr);
    std::array<CloudDelayedQueue, 3> queues;
    for (auto& queue : queues)
        queue.replay = &inner.queue();
    auto asset = tests::cloud::asset();
    const auto config = tests::cloud::config();
    VolumetricCloudFeature feature;
    assert(feature.create({device, {64, 48}, config, asset, ARP_CLOUD_SHADER_DIR}).success);
    std::array<core::UniquePtr<render_graph::FrameGraph>, 3> graphs;
    for (unsigned i = 0; i < graphs.size(); ++i) {
        graphs[i] = core::makeUnique<render_graph::FrameGraph>(device);
        auto c = makeFrame(*graphs[i], float(i) / 60);
        CloudOutputs output;
        assert(feature.frameReady() && feature.contributeGraph(c, output).success);
        assert(output.historyUsed == (i != 0));
        graphs[i]
            ->addPass("Cloud.TestDelayedConsume")
            .read(output.color, rhi::ResourceState::ShaderRead)
            .externalSideEffects()
            .execute([](render_graph::RenderContext&) {});
        device.overrideQueue(&queues[i]);
        graphs[i]->execute();
        assert(queues[i].delayed.pending);
    }
    assert(!feature.frameReady() && !feature.resize({32, 32}).success);
    queues[0].complete();
    // The newer reader still owns the first slot, despite its writer finishing.
    assert(!feature.frameReady());
    queues[1].complete();
    assert(feature.frameReady() && !feature.resize({32, 32}).success);
    queues[2].complete();
    assert(feature.resize({32, 32}).success);
    for (auto& graph : graphs)
        graph.reset();
    feature.release();
    device.overrideQueue(nullptr);

    for (unsigned failure = 0; failure < 3; ++failure) {
        tests::CapabilityOverrideDevice rejected(inner, nullptr);
        tests::DelayedValidatorQueue queue;
        queue.rejected = failure == 0;
        rejected.overrideQueue(&queue);
        rejected.failFenceCreation(failure == 1);
        if (failure == 2)
            rejected.failComputeRecording(
                "cloud_raymarch", tests::ComputeRecordFailure::Point::Dispatch);
        VolumetricCloudFeature failed;
        assert(failed.create({rejected, {64, 48}, config, asset, ARP_CLOUD_SHADER_DIR}).success);
        render_graph::FrameGraph graph(rejected);
        auto c = makeFrame(graph, 0);
        CloudOutputs output;
        assert(failed.contributeGraph(c, output).success && !output.historyUsed);
        graph.addPass("Cloud.TestRejectedConsume")
            .read(output.color, rhi::ResourceState::ShaderRead)
            .externalSideEffects()
            .execute([](render_graph::RenderContext&) {});
        bool threw = false;
        try {
            graph.execute();
        } catch (const std::exception&) {
            threw = true;
        }
        assert(threw && !graph.lastExecutionSubmitted() && !queue.pending && !failed.frameReady());
        assert(failed.cancelPendingFrame().diagnostic
            == std::string_view("CLOUD-CANCEL-AFTER-EXECUTION-VALIDATION"));
        assert(!failed.frameReady());
        failed.release();
    }
}
} // namespace

int main()
{
    try {
        auto device = rhi::createNullDevice();
        admission(*device);
        auto asset = tests::cloud::asset();
        auto config = tests::cloud::config();
        VolumetricCloudFeature feature;
        assert(!feature.frameReady());
        const auto visibleCloud = sceneCloudQueue();
        auto result = feature.create({*device, {64u, 48u}, config, asset, ARP_CLOUD_SHADER_DIR});
        if (!result.success)
            std::cerr << result.diagnostic << '\n';
        assert(result.success && feature.frameReady());
        for (unsigned frame = 0; frame != 12u; ++frame) {
            render_graph::FrameGraph graph(*device);
            const auto color = graph.createTransientTexture({"Cloud.TestColor",
                {64, 48, 1},
                rhi::TextureFormat::Rgba16Float,
                rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled});
            const auto depth = graph.createTransientTexture({"Cloud.TestDepth",
                {64, 48, 1},
                rhi::TextureFormat::D32Float,
                rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled});
            graph.addPass("Cloud.TestClear")
                .writeAttachment(color,
                    rhi::ResourceState::ColorAttachment,
                    rhi::LoadOp::Clear,
                    rhi::StoreOp::Store)
                .writeAttachment(depth,
                    rhi::ResourceState::DepthAttachment,
                    rhi::LoadOp::Clear,
                    rhi::StoreOp::Store)
                .executeRenderPass([](render_graph::RenderPassContext&) {});
            CloudGraphContext c{graph, color, depth};
            c.layer = visibleCloud.cloud().layer;
            c.cameraPosition = {0, 10, -8};
            c.view = core::lookAt(c.cameraPosition, {0, 500, 800});
            c.projection = core::perspective(60, 64.0f / 48, .1f, 20000);
            c.sunDirection = core::normalize(core::Vec3{1, 1, -1});
            c.sunRadiance = {10, 10, 10};
            c.ambientRadiance = {.1f, .15f, .2f};
            c.time = float(frame) / 60;
            c.cameraCut = frame == 2;
            c.sceneRevision = frame >= 4u ? 1u : 0u;
            if (frame >= 6u)
                c.layer.coverage = .8f;
            CloudOutputs output;
            auto bad = c;
            bad.sunDirection = {};
            assert(!feature.contributeGraph(bad, output).success);
            auto badCamera = c;
            badCamera.cameraId = 9;
            assert(feature.contributeGraph(badCamera, output).diagnostic
                == std::string_view("CLOUD-CAMERA-IDENTITY"));
            auto badMatrix = c;
            badMatrix.projection = core::Mat4::scale({0, 0, 0});
            assert(!feature.contributeGraph(badMatrix, output).success);
            auto badRadiance = c;
            badRadiance.ambientRadiance.x = std::numeric_limits<float>::quiet_NaN();
            assert(!feature.contributeGraph(badRadiance, output).success);
            result = feature.contributeGraph(c, output);
            if (!result.success)
                std::cerr << result.diagnostic << '\n';
            assert(result.success && !feature.frameReady());
            assert(output.historyUsed
                == (frame == 1u || frame == 3u || frame == 5u || frame == 7u || frame == 10u
                    || frame == 11u));
            assert(!feature.resize({32, 32}).success);
            if (frame == 8u) {
                feature.cancelPendingFrame();
                assert(feature.frameReady());
                assert(feature.validateSubmission({1u, 0u}));
                continue;
            }
            graph.addPass("Cloud.TestConsume")
                .read(output.color, rhi::ResourceState::ShaderRead)
                .externalSideEffects()
                .execute([](render_graph::RenderContext&) {});
            graph.execute();
            if (frame == 7u)
                feature.resetHistory();
        }
        assert(feature.resize({32u, 32u}).success);
        feature.release();
        assert(!feature.frameReady());
        lifecycle(*device);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Cloud Feature Null graph, descriptors, history/reset/cancel/resize passed\n";
}
