#include "core/cpu_allocator.h"
#include "fixtures/capability_override_device.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi_shader_package_test_helpers.h"

#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void enableTransientAliasingForCompilerContract(arp::rhi::DeviceCapabilities& capabilities)
{
    capabilities.memory.transientAliasing = true;
    capabilities.features.transientAliasing = true;
}

arp::rhi::TextureDesc textureDesc(std::pmr::string label,
    arp::rhi::Extent3D extent,
    arp::rhi::TextureFormat format,
    arp::rhi::TextureUsage usage,
    arp::rhi::ResourceState initialState = arp::rhi::ResourceState::Undefined)
{
    return arp::rhi::TextureDesc{
        std::move(label),
        extent,
        format,
        usage,
        arp::rhi::MemoryUsage::GpuOnly,
        initialState,
        1,
    };
}

arp::rhi::BufferDesc bufferDesc(std::pmr::string label,
    std::uint64_t sizeBytes,
    arp::rhi::ResourceUsage usage,
    arp::rhi::ResourceState initialState = arp::rhi::ResourceState::Undefined)
{
    return arp::rhi::BufferDesc{
        std::move(label),
        sizeBytes,
        usage,
        arp::rhi::MemoryUsage::GpuOnly,
        initialState,
    };
}

void test_texture_pass_order_and_transitions()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    auto color = graph.createTexture(textureDesc("HdrColor",
        {1920, 1080, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    bool clearExecuted = false;
    bool presentExecuted = false;

    graph.addPass("Clear")
        .writeAttachment(color,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([&](arp::render_graph::RenderContext&) {
            clearExecuted = true;
        });

    graph.addPass("Present")
        .read(color, arp::rhi::ResourceState::Present)
        .execute([&](arp::render_graph::RenderContext&) {
            presentExecuted = true;
        });

    const auto compiled = graph.compile();
    assert(compiled.passCount() == 2);
    assert(compiled.executablePassCount() == 2);
    assert(compiled.executionOrder()[0] == 0);
    assert(compiled.executionOrder()[1] == 1);
    assert(compiled.loadStorePlan().size() == 1);
    assert(compiled.loadStorePlan()[0].passName == std::pmr::string("Clear"));
    assert(compiled.loadStorePlan()[0].loadOp == arp::rhi::LoadOp::Clear);
    assert(compiled.loadStorePlan()[0].storeOp == arp::rhi::StoreOp::Store);

    graph.execute();

    assert(clearExecuted);
    assert(presentExecuted);
    assert(graph.executionTrace().size() == 2);
    assert(graph.executionTrace()[0] == std::pmr::string("Clear"));
    assert(graph.executionTrace()[1] == std::pmr::string("Present"));
    assert(graph.texture(color).state() == arp::rhi::ResourceState::Present);
}

void test_compile_orders_passes_by_resource_dependencies()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    const auto color = graph.createTexture(textureDesc("DependencyColor",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    graph.addPass("ToneMap")
        .read(color, arp::rhi::ResourceState::ShaderRead)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("SceneColor")
        .writeAttachment(color,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();
    assert(compiled.executablePassCount() == 2);
    assert(compiled.passes()[compiled.executionOrder()[0]].name == std::pmr::string("SceneColor"));
    assert(compiled.passes()[compiled.executionOrder()[1]].name == std::pmr::string("ToneMap"));
    assert(compiled.resourceLifetimes().size() == 1);
    assert(compiled.resourceLifetimes()[0].resourceName == std::pmr::string("DependencyColor"));
    assert(compiled.resourceLifetimes()[0].producerPassName == std::pmr::string("SceneColor"));
    assert(compiled.resourceLifetimes()[0].consumerPassNames.size() == 1);
    assert(compiled.resourceLifetimes()[0].consumerPassNames[0] == std::pmr::string("ToneMap"));

    graph.execute();
    assert(graph.executionTrace()[0] == std::pmr::string("SceneColor"));
    assert(graph.executionTrace()[1] == std::pmr::string("ToneMap"));
}

void test_imported_read_before_write_preserves_external_contents()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    auto importedTexture = device->createTexture(textureDesc("ImportedHistory",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::Sampled | arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::ResourceState::ShaderRead));
    assert(importedTexture);
    const auto history = graph.importTexture(std::move(importedTexture));

    graph.addPass("ReadExternalHistory")
        .read(history, arp::rhi::ResourceState::ShaderRead)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("OverwriteExternalHistory")
        .write(history, arp::rhi::ResourceState::ColorAttachment)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();
    assert(compiled.executablePassCount() == 2);
    assert(compiled.passes()[compiled.executionOrder()[0]].name
        == std::pmr::string("ReadExternalHistory"));
    assert(compiled.passes()[compiled.executionOrder()[1]].name
        == std::pmr::string("OverwriteExternalHistory"));
    assert(compiled.dependencies().size() == 1);
    assert(compiled.dependencies()[0].dependency == arp::render_graph::TopologyDependencyKind::War);
}

void test_compile_rejects_cyclic_resource_dependencies()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    const auto a = graph.createTexture(textureDesc("A",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));
    const auto b = graph.createTexture(textureDesc("B",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    graph.addPass("PassA")
        .read(b, arp::rhi::ResourceState::ShaderRead)
        .write(a, arp::rhi::ResourceState::ColorAttachment)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("PassB")
        .read(a, arp::rhi::ResourceState::ShaderRead)
        .write(b, arp::rhi::ResourceState::ColorAttachment)
        .execute([](arp::render_graph::RenderContext&) {});

    bool rejected = false;
    try {
        (void)graph.compile();
    } catch (const std::runtime_error& error) {
        rejected = std::pmr::string(error.what()).find("cycle") != std::pmr::string::npos;
    }
    assert(rejected);
}

void test_buffer_pass_order_and_transitions()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    auto lightList = graph.createBuffer(
        bufferDesc("ClusterLightList", 4096, arp::rhi::ResourceUsage::StorageBuffer));

    bool buildExecuted = false;
    bool sceneExecuted = false;

    graph.addPass("BuildClusters")
        .write(lightList, arp::rhi::ResourceState::ShaderWrite)
        .execute([&](arp::render_graph::RenderContext&) {
            buildExecuted = true;
        });

    graph.addPass("SceneOpaque")
        .read(lightList, arp::rhi::ResourceState::ShaderRead)
        .execute([&](arp::render_graph::RenderContext&) {
            sceneExecuted = true;
        });

    graph.execute();

    assert(buildExecuted);
    assert(sceneExecuted);
    assert(graph.executionTrace().size() == 2);
    assert(graph.executionTrace()[0] == std::pmr::string("BuildClusters"));
    assert(graph.executionTrace()[1] == std::pmr::string("SceneOpaque"));
    assert(graph.buffer(lightList).state() == arp::rhi::ResourceState::ShaderRead);
}

void test_depth_only_shadow_render_pass_records_explicit_attachment_contract()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    auto shader = arp::rhi::test::makePushContractShaderPackage();
    arp::rhi::PipelineLayoutDesc layoutDesc;
    layoutDesc.pushConstantRanges.push_back(
        {arp::rhi::ShaderStage::Vertex | arp::rhi::ShaderStage::Fragment, 0u, 128u});
    auto layout = device->createPipelineLayout(layoutDesc);
    assert(layout);
    arp::rhi::RenderPipelineDesc pipelineDesc;
    pipelineDesc.layout = layout.get();
    pipelineDesc.shaderPackage = &shader;
    pipelineDesc.vertexShader = shader.entrypoints[0];
    pipelineDesc.fragmentShader = shader.entrypoints[1];
    pipelineDesc.colorFormat = arp::rhi::TextureFormat::Unknown;
    pipelineDesc.depthStencil.format = arp::rhi::TextureFormat::D32Float;
    pipelineDesc.depthStencil.depthTestEnabled = true;
    pipelineDesc.depthStencil.depthWriteEnabled = true;
    pipelineDesc.depthStencil.depthCompare = arp::rhi::CompareOp::LessEqual;
    auto pipeline = device->createRenderPipeline(pipelineDesc);
    assert(pipeline);

    const auto shadowAtlas = graph.createTransientTexture(arp::rhi::TextureDesc{
        "DepthOnlyShadowAtlas",
        {2048, 2048, 1},
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::Transient,
        arp::rhi::ResourceState::Undefined,
        1,
    });

    bool renderPassExecuted = false;
    std::uint32_t recordedCasterCommandCount = 0;

    auto shadowPass = graph.addPass("DepthOnlyShadowContract")
                          .kind(arp::render_graph::PassKind::Graphics)
                          .queue(arp::render_graph::QueueRole::Graphics);
    shadowPass
        .writeAttachment(shadowAtlas,
            arp::rhi::ResourceState::DepthAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store,
            arp::render_graph::MobileTileHints{
                false,
                false,
                false,
                true,
            })
        .executeRenderPass([&](arp::render_graph::RenderPassContext& context) {
            renderPassExecuted = true;
            auto secondary = context.device().createSecondaryCommandEncoder(
                arp::rhi::SecondaryCommandEncoderDesc{
                    "DepthOnlyShadowSyntheticCaster",
                    true,
                });
            assert(secondary);
            const std::uint32_t cascadeIndex = 0;
            auto push = secondary->pushConstants(
                arp::rhi::ShaderStage::Vertex, 0, sizeof(cascadeIndex), &cascadeIndex);
            // Soft secondary command buffers cannot establish the complete
            // pipeline push-constant contract, so this path must reject the
            // write deterministically rather than recording guessed bytes.
            assert(!push.success);
            assert(push.diagnostics.find("[PC-E014]") != std::pmr::string::npos);
            auto draw = secondary->drawIndexed(3, 1, 0, 0, 0);
            assert(draw.success);
            auto commands = secondary->finish();
            assert(commands);
            recordedCasterCommandCount = commands->commandCount();
            assert(recordedCasterCommandCount > 0u);
            const auto unbound = context.renderPass().executeSecondary(*commands);
            assert(!unbound.success);
            assert(unbound.diagnostics == "render pipeline must be bound before executeSecondary");
            assert(context.renderPass().setPipeline(*pipeline).success);
            const auto executed = context.renderPass().executeSecondary(*commands);
            if (!executed.success)
                std::fprintf(stderr, "%s\n", executed.diagnostics.c_str());
            assert(executed.success);
        });

    const auto compiled = graph.compile();
    assert(compiled.executablePassCount() == 1);
    assert(compiled.queuePlan()[0].passName == std::pmr::string("DepthOnlyShadowContract"));
    assert(compiled.loadStorePlan().size() == 1);
    assert(compiled.loadStorePlan()[0].resourceName == std::pmr::string("DepthOnlyShadowAtlas"));
    assert(compiled.loadStorePlan()[0].state == arp::rhi::ResourceState::DepthAttachment);
    assert(compiled.loadStorePlan()[0].loadOp == arp::rhi::LoadOp::Clear);
    assert(compiled.loadStorePlan()[0].storeOp == arp::rhi::StoreOp::Store);
    assert(compiled.loadStorePlan()[0].tileHints.preserveOnlyWhenSampledLater);

    graph.execute();

    assert(renderPassExecuted);
    assert(recordedCasterCommandCount == 1u);
    assert(graph.executionTrace().size() == 1);
    assert(graph.executionTrace()[0] == std::pmr::string("DepthOnlyShadowContract"));
    assert(graph.texture(shadowAtlas).state() == arp::rhi::ResourceState::DepthAttachment);
    assert(graph.attachmentTrace().size() == 1);
    assert(graph.attachmentTrace()[0].passName == std::pmr::string("DepthOnlyShadowContract"));
    assert(graph.attachmentTrace()[0].resourceName == std::pmr::string("DepthOnlyShadowAtlas"));
    assert(graph.attachmentTrace()[0].loadOp == arp::rhi::LoadOp::Clear);
    assert(graph.attachmentTrace()[0].storeOp == arp::rhi::StoreOp::Store);
    assert(graph.barrierTrace().size() == 1);
    assert(graph.barrierTrace()[0].after == arp::rhi::ResourceState::DepthAttachment);
    assert(graph.barrierTrace()[0].aspect == arp::rhi::TextureAspect::Depth);
}

void test_mobile_pass_metadata_culling_aliasing_and_imported_backbuffer_protection()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    graph.setPassCullingEnabled(true);

    auto shadowAtlas = graph.createTransientTexture(arp::rhi::TextureDesc{
        "TransientShadowAtlas",
        {1024, 1024, 1},
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::Transient,
        arp::rhi::ResourceState::Undefined,
        1,
    });
    auto unusedBloom = graph.createTransientTexture(arp::rhi::TextureDesc{
        "UnusedBloom",
        {512, 512, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::Transient,
        arp::rhi::ResourceState::Undefined,
        1,
    });
    auto halfResA = graph.createTransientTexture(arp::rhi::TextureDesc{
        "HalfResA",
        {512, 512, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::Transient,
        arp::rhi::ResourceState::Undefined,
        1,
    });
    auto halfResB = graph.createTransientTexture(arp::rhi::TextureDesc{
        "HalfResB",
        {512, 512, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::Transient,
        arp::rhi::ResourceState::Undefined,
        1,
    });
    auto backbuffer = graph.importTexture(device->createTexture(textureDesc("ImportedBackbuffer",
        {1024, 768, 1},
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::ResourceState::Present)));

    bool shadowExecuted = false;
    bool unusedExecuted = false;
    bool postAExecuted = false;
    bool postBExecuted = false;
    bool presentExecuted = false;

    graph.addPass("ShadowAtlas")
        .kind(arp::render_graph::PassKind::Graphics)
        .queue(arp::render_graph::QueueRole::Graphics)
        .writeAttachment(shadowAtlas,
            arp::rhi::ResourceState::DepthAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::DontCare,
            arp::render_graph::MobileTileHints{
                true,
                true,
                true,
                true,
            })
        .execute([&](arp::render_graph::RenderContext&) {
            shadowExecuted = true;
        });

    graph.addPass("UnusedBloom")
        .kind(arp::render_graph::PassKind::Graphics)
        .writeAttachment(unusedBloom,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::DontCare,
            arp::render_graph::MobileTileHints{true, true, true, false})
        .execute([&](arp::render_graph::RenderContext&) {
            unusedExecuted = true;
        });

    graph.addPass("PostA")
        .kind(arp::render_graph::PassKind::Compute)
        .queue(arp::render_graph::QueueRole::Compute)
        .asyncComputeEligible(true)
        .read(shadowAtlas, arp::rhi::ResourceState::ShaderRead)
        .write(halfResA, arp::rhi::ResourceState::ShaderWrite)
        .execute([&](arp::render_graph::RenderContext&) {
            postAExecuted = true;
        });

    graph.addPass("PostB")
        .kind(arp::render_graph::PassKind::Copy)
        .queue(arp::render_graph::QueueRole::Transfer)
        .read(halfResA, arp::rhi::ResourceState::ShaderRead)
        .write(halfResB, arp::rhi::ResourceState::ShaderWrite)
        .execute([&](arp::render_graph::RenderContext&) {
            postBExecuted = true;
        });

    graph.addPass("Present")
        .kind(arp::render_graph::PassKind::Present)
        .queue(arp::render_graph::QueueRole::Present)
        .read(halfResB, arp::rhi::ResourceState::ShaderRead)
        .writeAttachment(backbuffer,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::DontCare,
            arp::rhi::StoreOp::Store,
            arp::render_graph::MobileTileHints{})
        .execute([&](arp::render_graph::RenderContext&) {
            presentExecuted = true;
        });

    const auto compiled = graph.compile();
    const auto& queuePlan = compiled.queuePlan();
    assert(queuePlan.size() == 4);
    assert(queuePlan[0].passName == std::pmr::string("ShadowAtlas"));
    assert(queuePlan[0].queue == arp::render_graph::QueueRole::Graphics);
    assert(queuePlan[1].passName == std::pmr::string("PostA"));
    assert(queuePlan[1].queue == arp::render_graph::QueueRole::Compute);
    assert(queuePlan[1].asyncComputeEligible);
    assert(queuePlan[2].queue == arp::render_graph::QueueRole::Transfer);
    assert(queuePlan[3].queue == arp::render_graph::QueueRole::Present);

    const auto& externalResources = compiled.externalResources();
    assert(externalResources.size() == 1);
    assert(externalResources[0].resourceName == std::pmr::string("ImportedBackbuffer"));
    assert(externalResources[0].externalProtected);

    const auto& loadStorePlan = compiled.loadStorePlan();
    assert(loadStorePlan.size() == 2);
    assert(loadStorePlan[0].passName == std::pmr::string("ShadowAtlas"));
    assert(loadStorePlan[0].loadOp == arp::rhi::LoadOp::Clear);
    assert(loadStorePlan[0].storeOp == arp::rhi::StoreOp::DontCare);
    assert(loadStorePlan[0].tileHints.memorylessAttachment);
    assert(loadStorePlan[0].tileHints.avoidStore);
    assert(loadStorePlan[1].passName == std::pmr::string("Present"));
    assert(loadStorePlan[1].resourceName == std::pmr::string("ImportedBackbuffer"));
    assert(loadStorePlan[1].externalProtected);

    graph.execute();

    assert(shadowExecuted);
    assert(!unusedExecuted);
    assert(postAExecuted);
    assert(postBExecuted);
    assert(presentExecuted);
    assert(graph.passTrace().size() == 5);
    assert(graph.passTrace()[1].name == std::pmr::string("UnusedBloom"));
    assert(graph.passTrace()[1].culled);
    assert(graph.attachmentTrace().size() == 2);
    assert(graph.attachmentTrace()[0].passName == std::pmr::string("ShadowAtlas"));
    assert(graph.attachmentTrace()[0].transient);
    assert(graph.attachmentTrace()[0].tileHints.preserveOnlyWhenSampledLater);
    assert(graph.attachmentTrace()[1].resourceName == std::pmr::string("ImportedBackbuffer"));
    assert(graph.attachmentTrace()[1].imported);
    assert(graph.attachmentTrace()[1].externalProtected);

    std::uint32_t halfResAGroup = 0;
    std::uint32_t halfResBGroup = 0;
    for (const auto& alias : graph.aliasTrace()) {
        if (alias.resourceName == std::pmr::string("HalfResA")) {
            halfResAGroup = alias.aliasGroup;
        }
        if (alias.resourceName == std::pmr::string("HalfResB")) {
            halfResBGroup = alias.aliasGroup;
        }
        assert(alias.resourceName != std::pmr::string("ImportedBackbuffer"));
    }
    // Null advertises no native transient aliasing payload, and these two
    // resources also cross compute/transfer queue roles. Neither condition
    // may be represented by synthetic alias groups.
    assert(!device->capabilities().features.transientAliasing);
    assert(halfResAGroup == 0);
    assert(halfResBGroup == 0);
}

// ---------------------------------------------------------------------------
// Phase 1 - Single source of truth: executor replays compiled barrier plan
// ---------------------------------------------------------------------------

void test_executor_replays_compiled_barrier_plan()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    auto color = graph.createTexture(textureDesc("ReplayColor",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    graph.addPass("Produce")
        .writeAttachment(color,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("Consume")
        .read(color, arp::rhi::ResourceState::ShaderRead)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();
    graph.execute();

    const auto& barriers = graph.barrierTrace();
    const auto& plan = compiled.barrierPlan();
    assert(barriers.size() == plan.size());
    for (size_t i = 0; i < barriers.size(); ++i) {
        assert(barriers[i].resourceName == plan[i].resourceName);
        assert(barriers[i].before == plan[i].before);
        assert(barriers[i].after == plan[i].after);
        assert(barriers[i].kind == plan[i].kind);
    }
}

void test_executor_does_not_recompute_from_live_state()
{
    auto device = arp::rhi::createNullDevice();

    auto buildAndExecute = [&]() {
        arp::render_graph::FrameGraph graph(*device);
        auto color = graph.createTexture(textureDesc("StableColor",
            {64, 64, 1},
            arp::rhi::TextureFormat::Rgba8Unorm,
            arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

        graph.addPass("Write")
            .write(color, arp::rhi::ResourceState::ColorAttachment)
            .execute([](arp::render_graph::RenderContext&) {});
        graph.addPass("Read")
            .read(color, arp::rhi::ResourceState::ShaderRead)
            .execute([](arp::render_graph::RenderContext&) {});

        graph.execute();
        return graph.barrierTrace();
    };

    const auto firstBarriers = buildAndExecute();
    const auto secondBarriers = buildAndExecute();

    assert(firstBarriers.size() == secondBarriers.size());
    for (size_t i = 0; i < firstBarriers.size(); ++i) {
        assert(firstBarriers[i].before == secondBarriers[i].before);
        assert(firstBarriers[i].after == secondBarriers[i].after);
        assert(firstBarriers[i].resourceName == secondBarriers[i].resourceName);
    }
}

void test_transient_resources_reused_with_device_owned_pool()
{
    auto device = arp::rhi::createNullDevice();
    auto pool = arp::core::makeShared<arp::render_graph::FrameGraphResourcePool>(*device);
    const auto desc = textureDesc("PooledTransient",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled);

    const auto executeFrame = [&] {
        arp::render_graph::FrameGraph graph(*device, pool);
        const auto color = graph.createTransientTexture(desc);
        graph.addPass("Write")
            .write(color, arp::rhi::ResourceState::ColorAttachment)
            .execute([](arp::render_graph::RenderContext&) {});
        graph.execute();
    };

    executeFrame();
    assert(pool->missCount() == 1u);
    assert(pool->poolSize() == 0u);
    assert(pool->report().pendingResourceCount == 1u);
    pool->recycleCompleted();
    assert(pool->poolSize() == 1u);
    assert(pool->report().pendingResourceCount == 0u);
    executeFrame();
    assert(pool->missCount() == 1u);
    assert(pool->hitCount() == 1u);
    assert(pool->poolSize() == 0u);
    assert(pool->report().pendingResourceCount == 1u);
    pool->recycleCompleted();
    assert(pool->poolSize() == 1u);
    assert(pool->report().pendingResourceCount == 0u);
}

void test_imported_resource_final_state_restored()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    auto backbuffer = graph.importTexture(device->createTexture(textureDesc("SwapchainBackbuffer",
        {256, 256, 1},
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::ResourceState::Present)));

    graph.addPass("Render")
        .writeAttachment(backbuffer,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("Present")
        .read(backbuffer, arp::rhi::ResourceState::Present)
        .execute([](arp::render_graph::RenderContext&) {});

    graph.execute();
    assert(graph.texture(backbuffer).state() == arp::rhi::ResourceState::Present);
}

void test_alias_group_single_source_of_truth()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    auto texA = graph.createTransientTexture(textureDesc("TexA",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));
    auto texB = graph.createTransientTexture(textureDesc("TexB",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    graph.addPass("WriteA")
        .writeAttachment(texA,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("ReadA")
        .read(texA, arp::rhi::ResourceState::ShaderRead)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("WriteB")
        .writeAttachment(texB,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("ReadB")
        .read(texB, arp::rhi::ResourceState::ShaderRead)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();
    graph.execute();

    const auto& trace = graph.aliasTrace();
    const auto& plan = compiled.aliasPlan();
    assert(trace.size() == plan.size());
    for (size_t i = 0; i < trace.size(); ++i) {
        assert(trace[i].resourceName == plan[i].resourceName);
        assert(trace[i].aliasGroup == plan[i].aliasGroup);
        assert(trace[i].handle.id == plan[i].handle.id);
    }
}

void test_alias_key_includes_sample_count()
{
    auto nullDevice = arp::rhi::createNullDevice();
    arp::tests::CapabilityOverrideDevice device(
        *nullDevice, enableTransientAliasingForCompilerContract);
    arp::render_graph::FrameGraph graph(device);

    auto desc1x = textureDesc("Msaa1x",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled);
    desc1x.sampleCount = 1;
    auto tex1x = graph.createTransientTexture(desc1x);

    auto desc4x = textureDesc("Msaa4x",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled);
    desc4x.sampleCount = 4;
    auto tex4x = graph.createTransientTexture(desc4x);

    graph.addPass("Write1x")
        .writeAttachment(tex1x,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("Read1x")
        .read(tex1x, arp::rhi::ResourceState::ShaderRead)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("Write4x")
        .writeAttachment(tex4x,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("Read4x")
        .read(tex4x, arp::rhi::ResourceState::ShaderRead)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();

    std::uint32_t group1x = 0;
    std::uint32_t group4x = 0;
    for (const auto& alias : compiled.aliasPlan()) {
        if (alias.resourceName == std::pmr::string("Msaa1x")) {
            group1x = alias.aliasGroup;
        }
        if (alias.resourceName == std::pmr::string("Msaa4x")) {
            group4x = alias.aliasGroup;
        }
    }
    assert(group1x != 0);
    assert(group4x != 0);
    assert(group1x != group4x);
}

void test_pass_barrier_batch_ordered_before_pass()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    auto color = graph.createTexture(textureDesc("OrderColor",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    graph.addPass("Produce")
        .write(color, arp::rhi::ResourceState::ColorAttachment)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("Consume")
        .read(color, arp::rhi::ResourceState::ShaderRead)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();
    graph.execute();

    const auto& batches = compiled.passBarrierBatches();
    assert(batches.size() >= 2);
    assert(batches[0].passOriginalIndex == 0);
    assert(batches[1].passOriginalIndex == 1);

    const auto& trace = graph.executionTrace();
    assert(trace.size() == 2);
    assert(trace[0] == std::pmr::string("Produce"));
    assert(trace[1] == std::pmr::string("Consume"));
}

void test_frame_end_final_transition_batch()
{
    auto device = arp::rhi::createNullDevice();
    auto swapchain = device->createSwapchain({
        "FrameEndSwapchain",
        {256, 256},
        arp::rhi::TextureFormat::Bgra8Unorm,
        2,
    });
    assert(swapchain);
    const auto frame = swapchain->acquireNextFrame();
    assert(frame.success);
    auto image = swapchain->acquiredImage(frame);
    assert(image.success && image.texture);
    arp::render_graph::FrameGraph graph(*device);

    auto backbuffer = graph.importSwapchainTexture(std::move(image.texture), image.ref);

    graph.addPass("Render")
        .writeAttachment(backbuffer,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();
    graph.execute();

    bool hasFrameEndBatch = false;
    bool frameEndHasPresent = false;
    for (const auto& batch : compiled.passBarrierBatches()) {
        if (batch.passOriginalIndex == UINT32_MAX) {
            hasFrameEndBatch = true;
            for (const auto& barrier : batch.prePassBarriers) {
                if (barrier.after == arp::rhi::ResourceState::Present) {
                    frameEndHasPresent = true;
                }
            }
        }
    }
    assert(hasFrameEndBatch);
    assert(frameEndHasPresent);
    assert(graph.texture(backbuffer).state() == arp::rhi::ResourceState::Present);
    assert(swapchain->present(frame).success);
}

// ---------------------------------------------------------------------------
// Phase 2 - Compile-time validation + culling fixed-point
// ---------------------------------------------------------------------------

void test_cull_fixed_point_cascades()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    graph.setPassCullingEnabled(true);

    auto color = graph.createTexture(textureDesc("CullColor",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));
    auto temp = graph.createTransientTexture(textureDesc("CullTemp",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    graph.addPass("ProduceTemp")
        .write(temp, arp::rhi::ResourceState::ColorAttachment)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("ConsumeTemp")
        .read(temp, arp::rhi::ResourceState::ShaderRead)
        .write(color, arp::rhi::ResourceState::ShaderWrite)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();
    bool produceCulled = false;
    bool consumeCulled = false;
    for (const auto& pass : compiled.passes()) {
        if (pass.name == std::pmr::string("ProduceTemp")) {
            produceCulled = pass.culled;
        }
        if (pass.name == std::pmr::string("ConsumeTemp")) {
            consumeCulled = pass.culled;
        }
    }
    assert(consumeCulled);
    assert(produceCulled);
}

void test_cull_iterative_converges()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    graph.setPassCullingEnabled(true);

    auto t1 = graph.createTransientTexture(textureDesc("Chain1",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));
    auto t2 = graph.createTransientTexture(textureDesc("Chain2",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));
    auto t3 = graph.createTransientTexture(textureDesc("Chain3",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    graph.addPass("Write1")
        .write(t1, arp::rhi::ResourceState::ColorAttachment)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("Read1Write2")
        .read(t1, arp::rhi::ResourceState::ShaderRead)
        .write(t2, arp::rhi::ResourceState::ShaderWrite)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("Read2Write3")
        .read(t2, arp::rhi::ResourceState::ShaderRead)
        .write(t3, arp::rhi::ResourceState::ShaderWrite)
        .execute([](arp::render_graph::RenderContext&) {});

    const auto compiled = graph.compile();
    for (const auto& pass : compiled.passes()) {
        assert(pass.culled);
    }
    assert(compiled.executablePassCount() == 0);
}

void test_read_before_write_throws()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    auto color = graph.createTexture(textureDesc("UnwrittenColor",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    graph.addPass("ReadOnly")
        .read(color, arp::rhi::ResourceState::ShaderRead)
        .execute([](arp::render_graph::RenderContext&) {});

    bool threw = false;
    try {
        graph.compile();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_load_on_undefined_throws()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    auto color = graph.createTransientTexture(textureDesc("LoadUndef",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    graph.addPass("LoadOnUndefined")
        .writeAttachment(color,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Load,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});

    bool threw = false;
    try {
        graph.compile();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_transient_color_load_without_producer_throws()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    // Transient color target with initial state ColorAttachment (the way
    // render_pipeline.cpp::createTransientColor declares it). This is NOT
    // Undefined, so the old Load-on-Undefined check skipped it — but the
    // texture has no real producer, so LoadOp::Load reads uninitialized GPU
    // memory. Content validity must reject it regardless of declared state.
    auto color = graph.createTransientTexture(textureDesc("TransientColorLoad",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::ResourceState::ColorAttachment));

    graph.addPass("LoadNoProducer")
        .writeAttachment(color,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Load,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});

    bool threw = false;
    try {
        graph.compile();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_transient_mip_texture_estimates_mip_chain()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    // Mipmapped transient texture. The old w*h*d*bpp formula would
    // under-allocate the alias heap (it ignored the mip chain); the improved
    // estimate sums mip levels so the heap covers the full mip pyramid.
    auto tex = graph.createTransientTexture(arp::rhi::TextureDesc{
        "MipTex",
        {128, 128, 1},
        arp::rhi::TextureFormat::Rgba16Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::Transient,
        arp::rhi::ResourceState::Undefined,
        4u,    // mipLevelCount
        1u,    // arrayLayerCount
        1u,    // sampleCount
        false, // cubeCompatible
    });

    graph.addPass("WriteMip")
        .write(tex, arp::rhi::ResourceState::ColorAttachment)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("ReadMip")
        .read(tex, arp::rhi::ResourceState::ShaderRead)
        .execute([](arp::render_graph::RenderContext&) {});

    // compile + execute drive allocateTransientTextures (render_graph.cpp:1097)
    // which now uses the mip-aware size estimate. Must not throw.
    graph.compile();
    graph.execute();
    assert(!graph.barrierTrace().empty());
}

void test_attachment_state_format_mismatch_throws()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    auto depthTex = graph.createTransientTexture(textureDesc("DepthFmt",
        {64, 64, 1},
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    graph.addPass("ColorOnDepth")
        .writeAttachment(depthTex,
            arp::rhi::ResourceState::ColorAttachment,
            arp::rhi::LoadOp::Clear,
            arp::rhi::StoreOp::Store)
        .execute([](arp::render_graph::RenderContext&) {});

    bool threw = false;
    try {
        graph.compile();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_pass_kind_state_mismatch_throws()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    auto color = graph.createTexture(textureDesc("StorageColor",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    graph.addPass("Produce")
        .write(color, arp::rhi::ResourceState::ColorAttachment)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("GraphicsStorageRead")
        .kind(arp::render_graph::PassKind::Graphics)
        .read(color, arp::rhi::ResourceState::ShaderWrite)
        .execute([](arp::render_graph::RenderContext&) {});

    bool threw = false;
    try {
        graph.compile();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void test_cycle_diagnosis_lists_passes()
{
    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);

    auto a = graph.createTexture(textureDesc("CycleA",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));
    auto b = graph.createTexture(textureDesc("CycleB",
        {64, 64, 1},
        arp::rhi::TextureFormat::Rgba8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled));

    graph.addPass("PassA")
        .read(b, arp::rhi::ResourceState::ShaderRead)
        .write(a, arp::rhi::ResourceState::ColorAttachment)
        .execute([](arp::render_graph::RenderContext&) {});
    graph.addPass("PassB")
        .read(a, arp::rhi::ResourceState::ShaderRead)
        .write(b, arp::rhi::ResourceState::ColorAttachment)
        .execute([](arp::render_graph::RenderContext&) {});

    std::string message;
    bool threw = false;
    try {
        graph.compile();
    } catch (const std::runtime_error& e) {
        threw = true;
        message = e.what();
    }
    assert(threw);
    assert(message.find("cycle") != std::string::npos);
    assert(
        message.find("PassA") != std::string::npos || message.find("PassB") != std::string::npos);
}

} // namespace

int main()
{
    test_texture_pass_order_and_transitions();
    test_compile_orders_passes_by_resource_dependencies();
    test_imported_read_before_write_preserves_external_contents();
    test_compile_rejects_cyclic_resource_dependencies();
    test_buffer_pass_order_and_transitions();
    test_depth_only_shadow_render_pass_records_explicit_attachment_contract();
    test_mobile_pass_metadata_culling_aliasing_and_imported_backbuffer_protection();
    test_executor_replays_compiled_barrier_plan();
    test_executor_does_not_recompute_from_live_state();
    test_transient_resources_reused_with_device_owned_pool();
    test_imported_resource_final_state_restored();
    test_alias_group_single_source_of_truth();
    test_alias_key_includes_sample_count();
    test_pass_barrier_batch_ordered_before_pass();
    test_frame_end_final_transition_batch();
    test_cull_fixed_point_cascades();
    test_cull_iterative_converges();
    test_read_before_write_throws();
    test_load_on_undefined_throws();
    test_transient_color_load_without_producer_throws();
    test_transient_mip_texture_estimates_mip_chain();
    test_attachment_state_format_mismatch_throws();
    test_pass_kind_state_mismatch_throws();
    test_cycle_diagnosis_lists_passes();

    return 0;
}
