#include "renderer/lighting/forward_plus.h"
#include "renderer/lighting/forward_plus_feature.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "rhi/backends/null/null_backend.h"

#include <cassert>
#include <cstdio>
#include <filesystem>

using namespace arp::renderer::lighting;

namespace {

ForwardPlusDeviceLimits testLimits()
{
    return {
        ForwardPlusCapabilityMode::Native,
        true,
        true,
        true,
        256u,
        256u,
        256u,
        32768u,
        8u,
        128u * 1024u * 1024u,
    };
}

} // namespace

int main()
{
    ForwardPlusConfig config;
    assert(config.tileSize == 16u);
    assert(config.maxDirectionalLights == 4u);
    assert(config.maxLocalLights == 512u);
    assert(config.maxLocalLightsPerTile == 64u);
    assert(forwardPlusTileCount(33u, 16u) == 3u);

    assert(validateForwardPlusConfig(config, {1920u, 1080u}, testLimits()).success);
    auto insufficient = testLimits();
    insufficient.maxComputeWorkgroupInvocations = 128u;
    const auto rejected = validateForwardPlusConfig(config, {1920u, 1080u}, insufficient);
    assert(!rejected.success);
    assert(rejected.diagnostic.code == ForwardPlusDiagnosticCode::WorkgroupLimitExceeded);

    arp::core::pmr::Vector<RenderLight> lights;
    RenderLight sun;
    sun.type = RenderLightType::Directional;
    sun.castsShadow = true;
    lights.push_back(sun);
    for (std::uint32_t index = 0u; index < 12u; ++index) {
        RenderLight point;
        point.type = RenderLightType::Point;
        point.position = {static_cast<float>(index), 1.0f, 4.0f};
        point.range = 5.0f;
        lights.push_back(point);
    }
    RenderLightPlan plan;
    assert(buildRenderLightPlan(LightingPath::ForwardPlus, config, lights, plan).success);
    assert(plan.directionalLights.size() == 1u);
    assert(plan.localLights.size() == 12u);
    assert(plan.primaryDirectionalCastsShadow);
    const auto legacyTruncated =
        buildRenderLightPlan(LightingPath::LegacyForward8, config, lights, plan);
    assert(legacyTruncated.success);
    assert(plan.path == LightingPath::LegacyForward8);
    assert(plan.directionalLights.size() == 1u);
    assert(plan.localLights.size() == 7u);
    assert(buildRenderLightPlan(LightingPath::ForwardPlus, config, lights, plan).success);

    ForwardPlusConfig tinyConfig;
    tinyConfig.maxLocalLights = 8u;
    tinyConfig.maxLocalLightsPerTile = 2u;
    arp::core::pmr::Vector<ForwardPlusTileDepthRange> depths{
        {0.2f, 0.5f},
        {0.2f, 0.5f},
    };
    arp::core::pmr::Vector<ForwardPlusProjectedLight> projected{
        {0u, 0u, 0u, 15u, 15u, 0.25f, 0.30f},
        {1u, 0u, 0u, 31u, 15u, 0.70f, 0.80f},
    };
    ForwardPlusReferenceCuller culler;
    auto culled = culler.cull(tinyConfig, {32u, 16u}, depths, projected);
    assert(culled.success);
    assert(culled.opaqueHeaders[0].count == 1u);
    assert(culled.opaqueHeaders[1].count == 0u);
    assert(culled.conservativeHeaders[0].count == 2u);
    assert(culled.conservativeHeaders[1].count == 1u);
    assert(culled.opaqueIndices[0] == 0u);

    projected.push_back({2u, 0u, 0u, 15u, 15u, 0.25f, 0.30f});
    auto overflow = culler.cull(tinyConfig, {32u, 16u}, depths, projected);
    assert(!overflow.success);
    assert(overflow.overflow.overflowed);
    assert(overflow.overflow.tileX == 0u && overflow.overflow.tileY == 0u);
    assert(overflow.overflow.requiredCount == 3u);
    assert(overflow.overflow.capacity == 2u);
    assert(overflow.diagnostic.code == ForwardPlusDiagnosticCode::TileLightOverflow);

    ForwardPlusCullView view;
    view.viewRows[0] = 1.0f;
    view.viewRows[5] = 1.0f;
    view.viewRows[10] = 1.0f;
    view.viewRows[15] = 1.0f;
    view.projectionScale[0] = 1.0f;
    view.projectionScale[1] = 1.0f;
    arp::core::pmr::Vector<GpuLocalLight> preflightLights;
    for (std::uint32_t index = 0u; index < 3u; ++index) {
        GpuLocalLight light;
        light.positionAndRange = {0.0f, 0.0f, 2.0f, 0.25f};
        preflightLights.push_back(light);
    }
    std::uint16_t tileCounts[1]{};
    const auto preflight = preflightForwardPlusConservativeTileBudget(
        tinyConfig, {16u, 16u}, preflightLights, view, tileCounts, std::size(tileCounts));
    assert(!preflight.success);
    assert(preflight.overflow.overflowed);
    assert(preflight.overflow.tileX == 0u && preflight.overflow.tileY == 0u);
    assert(preflight.overflow.requiredCount == 3u);
    assert(preflight.overflow.conservativeList);

    auto device = arp::rhi::createNullDevice();
    assert(device);
    auto deviceLimits = forwardPlusDeviceLimits(device->capabilities());
    assert(deviceLimits.mode == ForwardPlusCapabilityMode::Equivalent);
    arp::render_graph::FrameGraph graph(*device);
    arp::rhi::TextureDesc depthDesc;
    depthDesc.label = "FeatureLab.ExactDepth";
    depthDesc.extent = {32u, 16u, 1u};
    depthDesc.format = arp::rhi::TextureFormat::D32Float;
    depthDesc.usage = arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled;
    depthDesc.memory = arp::rhi::MemoryUsage::Transient;
    const auto depth = graph.createTransientTexture(depthDesc);
    graph.addPass("FeatureLab.DepthPrepass")
        .writeAttachment(depth,
            arp::rhi::ResourceState::DepthAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});

    ForwardPlusFeature feature;
    tinyConfig.maxDirectionalLights = 1u;
    const auto shaderPath = std::filesystem::path(ARP_SHADER_DIR) / "forward_plus_cull.slang";
    const auto featureCreated = feature.create({
        *device,
        {32u, 16u},
        tinyConfig,
        shaderPath.string().c_str(),
    });
    if (!featureCreated.success) {
        std::fprintf(stderr,
            "%s: %s\n",
            featureCreated.diagnostic.id.c_str(),
            featureCreated.diagnostic.message.c_str());
    }
    assert(featureCreated.success);
    ForwardPlusFeatureOutputs outputs;
    ForwardPlusCullView cullView;
    cullView.viewRows[0] = 1.0f;
    cullView.viewRows[5] = 1.0f;
    cullView.viewRows[10] = 1.0f;
    cullView.viewRows[15] = 1.0f;
    cullView.projectionScale[0] = 1.0f;
    cullView.projectionScale[1] = -1.0f;
    cullView.depthScale = 1.01f;
    cullView.depthBias = -0.1f;
    arp::core::pmr::Vector<RenderLight> graphLights;
    graphLights.push_back(sun);
    graphLights.push_back(lights[1]);
    graphLights.push_back(lights[2]);
    RenderLightPlan graphPlan;
    assert(buildRenderLightPlan(LightingPath::ForwardPlus, tinyConfig, graphLights, graphPlan)
            .success);
    assert(feature
            .contributeGraph(
                {
                    graph,
                    depth,
                    graphPlan,
                    cullView,
                    0u,
                },
                outputs)
            .success);
    for (const auto handle : {outputs.directionalLights,
             outputs.localLights,
             outputs.opaqueHeaders,
             outputs.opaqueIndices,
             outputs.conservativeHeaders,
             outputs.conservativeIndices,
             outputs.overflowTelemetry}) {
        assert(graph.isTransient(handle));
        assert(graph.buffer(handle).desc().memory == arp::rhi::MemoryUsage::GpuOnly);
    }
    graph.addPass("FeatureLab.OpaqueConsumer")
        .read(outputs.opaqueHeaders, arp::rhi::ResourceState::ShaderRead)
        .read(outputs.opaqueIndices, arp::rhi::ResourceState::ShaderRead)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("FeatureLab.TransparentConsumer")
        .read(outputs.conservativeHeaders, arp::rhi::ResourceState::ShaderRead)
        .read(outputs.conservativeIndices, arp::rhi::ResourceState::ShaderRead)
        .execute([](arp::render_graph::RenderContext&) {});
    const auto compiled = graph.compile();
    bool depthToCull = false;
    bool uploadToCull = false;
    for (const auto& dependency : compiled.dependencies()) {
        const auto& from = compiled.passes()[dependency.fromPass].name;
        const auto& to = compiled.passes()[dependency.toPass].name;
        depthToCull =
            depthToCull || (from == "FeatureLab.DepthPrepass" && to == "ForwardPlus.Cull");
        uploadToCull =
            uploadToCull || (from == "ForwardPlus.LightUpload" && to == "ForwardPlus.Cull");
    }
    assert(depthToCull);
    assert(uploadToCull);
    graph.execute();
    const auto resized = feature.resize({64u, 32u});
    assert(resized.success);
    assert(feature.extent().width == 64u && feature.extent().height == 32u);
    const auto invalidResize = feature.resize({0u, 32u});
    assert(!invalidResize.success);
    assert(invalidResize.diagnostic.code == ForwardPlusDiagnosticCode::InvalidExtent);
    feature.release();
    assert(!feature.created());

    std::printf("forward_plus_tests: all assertions passed\n");
    return 0;
}
