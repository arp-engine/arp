#include "2d/scene_2d_render_queue.h"
#include "3d/scene_3d_render_queue.h"
#include "core/cpu_allocator.h"
#include "renderer/frame/render_frame_director.h"
#include "renderer/frame/render_frame_payload_adapter.h"
#include "renderer/frame/render_pass_provider.h"
#include "renderer/pipeline/scene_runtime_pipeline_payload.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/scene_runtime_frame.h"
#include "renderer/scene_runtime_frame_rhi_planner.h"
#include "rhi/backends/null/null_backend.h"
#include "scene/scene_traversal_snapshot.h"
#include "ui/ui.h"

#include <cassert>
#include <utility>

namespace {

constexpr const char* kSceneContract = "tests/DirectorScenePayload";
constexpr const char* kGuiContract = "tests/DirectorGuiPayload";

arp::renderer::CustomWorkloadPayloadRef makePayloadRef(
    arp::renderer::CustomWorkloadPayloadKind kind,
    const void* object,
    const char* contractName,
    std::uint32_t contractVersion)
{
    return {
        kind,
        object,
        contractName,
        contractVersion,
    };
}

arp::renderer::CustomRenderFrameWorkloadDesc makeBaseWorkloadDesc()
{
    arp::renderer::CustomRenderFrameWorkloadDesc desc;
    desc.extent = arp::rhi::Extent2D{320, 180};
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.frameSerial = 7u;
    desc.backbufferState = arp::rhi::ResourceState::Present;
    return desc;
}

void allowPrototypeLegacyFallback(arp::renderer::CustomRenderFrameWorkloadDesc& desc,
    const char* name = "tests/DirectorPrototypeLegacyFallback")
{
    desc.resourceContracts[0] = {
        name,
        arp::renderer::CustomResourceSemantic::PrototypeLegacyFallback,
        arp::renderer::CustomUploadMetadataSource::None,
        {},
        arp::rhi::TextureFormat::Unknown,
        0u,
        0u,
        0u,
        "test explicitly allows legacy fallback",
    };
    desc.resourceContractCount = 1u;
}

arp::render_graph::TextureHandle createBackbuffer(
    arp::render_graph::FrameGraph& graph, arp::rhi::GpuDevice& device)
{
    return graph.importTexture(device.createTexture({
        "DirectorBackbuffer",
        {320u, 180u, 1u},
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Present,
        1u,
    }));
}

arp::renderer::RenderPipelineGraphCompositionContext makeCompositionContext(
    const arp::renderer::CustomWorkloadPayloadRef& payload, bool sceneEnabled)
{
    arp::renderer::RenderFrameRequest request;
    request.view.extent = {320, 180};
    request.view.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    request.view.frameIndex = 1u;
    request.pipelinePayload = payload.object;
    request.pipelinePayloadContractName = payload.contractName;
    request.pipelinePayloadContractVersion = payload.contractVersion;

    arp::rhi::AcquiredFrame acquired;
    acquired.success = true;
    acquired.frameSerial = 7u;
    acquired.frameIndex = 1u;

    return {
        acquired,
        request,
        sceneEnabled,
    };
}

arp::renderer::RendererRhiFramePlanDesc makePreparedSceneRuntimeFramePlan(
    const arp::renderer::RendererSceneRuntimeFrame& frame)
{
    arp::renderer::RendererSceneRuntimeFrameRhiPlanner planner({
        1u,
        2u,
        2u,
        8u,
        128u,
        256u,
        16u,
    });
    const auto planned = planner.plan(frame);
    assert(planned.ok());
    return planned.framePlan();
}

class CountingAdapter final : public arp::renderer::IRenderFramePayloadAdapter {
public:
    CountingAdapter(arp::renderer::CustomWorkloadPayloadKind kind,
        const char* contractName,
        const char* intentName)
        : intentName_(intentName)
    {
        key_.kind = kind;
        key_.contractName = contractName;
        key_.contractVersion = 1u;
    }

    arp::renderer::RenderFramePayloadAdapterKey key() const override { return key_; }

    arp::renderer::RenderFramePayloadAdapterResult adapt(
        const arp::renderer::CustomWorkloadPayloadRef&) const override
    {
        ++callCount_;
        arp::renderer::RenderFramePayloadAdapterResult result;
        result.success = true;
        result.intent.contractName = intentName_;
        result.intent.contractVersion = 1u;
        return result;
    }

    int callCount() const { return callCount_; }

private:
    arp::renderer::RenderFramePayloadAdapterKey key_;
    const char* intentName_ = nullptr;
    mutable int callCount_ = 0;
};

class CountingProvider final : public arp::renderer::IRenderPassProvider {
public:
    const char* providerId() const override { return "tests/CountingProvider"; }

    arp::renderer::RenderFrameStage stage() const override
    {
        return arp::renderer::RenderFrameStage::WorldOpaque;
    }

    const char* capabilityLabel() const override { return "tests/director-provider"; }

    bool supports(const arp::renderer::RenderFrameIntent&) const override { return true; }

    arp::renderer::RenderPassProviderResult provide(
        const arp::renderer::RenderFrameIntent& intent) const override
    {
        ++callCount_;
        arp::renderer::RenderPassProviderResult result;
        result.success = true;

        arp::renderer::RenderPassDeclaration pass;
        pass.id = intent.contractName == arp::core::pmr::String("tests/SceneIntent")
            ? "DirectorScenePass"
            : "DirectorGuiPass";
        pass.stage = arp::renderer::RenderFrameStage::WorldOpaque;
        pass.declaredOrder = result.passes.size();
        pass.providerId = providerId();
        pass.capabilityLabel = capabilityLabel();
        arp::renderer::RenderFrameAttachmentUse backbufferWrite;
        backbufferWrite.attachmentId = "Backbuffer";
        backbufferWrite.state = arp::rhi::ResourceState::ColorAttachment;
        backbufferWrite.access = arp::renderer::RenderFrameAttachmentAccess::Write;
        pass.attachmentUses.push_back(backbufferWrite);
        result.passes.push_back(pass);
        return result;
    }

    int callCount() const { return callCount_; }

private:
    mutable int callCount_ = 0;
};

class LegacyPipeline final : public arp::renderer::RenderPipeline {
public:
    const arp::renderer::RenderPipelineDesc& desc() const override { return desc_; }

    arp::renderer::RendererResult create(const arp::renderer::RenderPipelineCreateContext&) override
    {
        return {true, {}};
    }

    arp::renderer::RendererResult resize(arp::rhi::Extent2D) override { return {true, {}}; }

    arp::renderer::RendererResult render(arp::renderer::RenderPipelineContext&) override
    {
        return {false, "LegacyPipeline render is not used by director tests."};
    }

    arp::renderer::RendererResult composeGraph(arp::render_graph::FrameGraph& graph,
        arp::render_graph::TextureHandle backbuffer,
        const arp::renderer::RenderPipelineGraphCompositionContext& context,
        arp::renderer::RenderPipelineFrameFeatures) override
    {
        ++composeCalls_;
        lastPayload_ = context.request.pipelinePayload;
        lastPayloadContractName_ = context.request.pipelinePayloadContractName;
        lastPayloadContractVersion_ = context.request.pipelinePayloadContractVersion;
        graph.addPass("LegacyFallbackPass")
            .write(backbuffer, arp::rhi::ResourceState::ColorAttachment)
            .execute([](arp::render_graph::RenderContext&) {});
        return {true, {}};
    }

    int composeCalls() const { return composeCalls_; }

    const void* lastPayload() const { return lastPayload_; }

    const char* lastPayloadContractName() const { return lastPayloadContractName_; }

    std::uint32_t lastPayloadContractVersion() const { return lastPayloadContractVersion_; }

private:
    arp::renderer::RenderPipelineDesc desc_;
    int composeCalls_ = 0;
    const void* lastPayload_ = nullptr;
    const char* lastPayloadContractName_ = nullptr;
    std::uint32_t lastPayloadContractVersion_ = 0u;
};

arp::scene::SceneTraversalSnapshot makeSnapshot()
{
    arp::core::pmr::Vector<arp::scene::SceneTraversalNode> nodes;
    arp::scene::SceneTraversalNode node;
    node.objectId = arp::scene::GameObjectId::fromValue(1);
    node.parentId = arp::scene::GameObjectId::invalid();
    node.active = true;
    node.activeInHierarchy = true;
    nodes.push_back(node);
    return arp::scene::SceneTraversalSnapshot::success(std::move(nodes));
}

arp::core::pmr::Vector<arp::scene::ResolvedCamera> makeCameras()
{
    arp::core::pmr::Vector<arp::scene::ResolvedCamera> cameras;
    arp::scene::ResolvedCamera camera;
    camera.objectId = arp::scene::GameObjectId::fromValue(2);
    camera.componentId = arp::scene::ComponentId::fromValue(3);
    camera.visibleQueues = arp::scene::RenderQueueMask::world2DAnd3D();
    cameras.push_back(camera);
    return cameras;
}

arp::scene3d::Scene3DRenderQueue make3DQueue()
{
    arp::scene3d::Scene3DRenderQueue queue;
    arp::scene3d::Scene3DRenderItem item;
    item.kind = arp::scene3d::Scene3DRenderItemKind::Mesh;
    item.objectId = arp::scene::GameObjectId::fromValue(4);
    item.mesh = arp::renderer::MeshHandle{5};
    item.material = arp::renderer::MaterialHandle{6};
    item.worldBounds =
        arp::scene3d::Bounds3D::explicitBounds({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
    queue.addItem(item);
    return queue;
}

arp::scene2d::Scene2DRenderQueue make2DQueue()
{
    arp::scene2d::Scene2DRenderQueue queue;
    arp::scene2d::Scene2DRenderItem item;
    item.kind = arp::scene2d::Scene2DRenderItemKind::Sprite;
    item.objectId = arp::scene::GameObjectId::fromValue(7);
    item.sprite = arp::scene2d::SpriteHandle{8};
    item.material = arp::renderer::MaterialHandle{9};
    item.texture = arp::renderer::TextureHandle{10};
    item.worldBounds =
        arp::scene2d::Bounds2D::explicitBounds({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f});
    queue.addSprite(item);
    return queue;
}

arp::ui::RenderQueue makeUiQueue()
{
    arp::ui::RenderQueue queue;
    arp::ui::UIRenderItem item;
    item.widgetId = arp::ui::WidgetId::fromValue(11);
    item.rect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 32.0f, 16.0f);
    item.clipRect = item.rect;
    item.material = arp::renderer::MaterialHandle{12};
    item.texture = arp::renderer::TextureHandle{13};
    queue.addItem(item);
    return queue;
}

struct SceneRuntimeFixture {
    arp::scene::SceneTraversalSnapshot snapshot;
    arp::core::pmr::Vector<arp::scene::ResolvedCamera> cameras;
    arp::scene3d::Scene3DRenderQueue queue3D;
    arp::scene2d::Scene2DRenderQueue queue2D;
    arp::ui::RenderQueue uiQueue;
    arp::renderer::RendererSceneRuntimeFrame frame;
    arp::renderer::SceneRuntimePipelinePayload payload;

    SceneRuntimeFixture()
        : snapshot(makeSnapshot()), cameras(makeCameras()), queue3D(make3DQueue()),
          queue2D(make2DQueue()), uiQueue(makeUiQueue()), frame(makeFrame()), payload({
                                                                                  &snapshot,
                                                                                  &cameras,
                                                                                  &queue3D,
                                                                                  &queue2D,
                                                                                  &uiQueue,
                                                                                  &frame,
                                                                              })
    {}

    arp::renderer::RendererSceneRuntimeFrame makeFrame() const
    {
        arp::renderer::RendererSceneRuntimeFrameInput input;
        input.snapshot = &snapshot;
        input.resolvedCameras = &cameras;
        input.scene3DQueue = &queue3D;
        input.scene2DQueue = &queue2D;
        input.uiQueue = &uiQueue;
        input.oneCompatibleWorldUiPassIntent = true;
        arp::renderer::RendererSceneRuntimeFrameBuilder builder({3, 8, 8, 8});
        const auto result = builder.build(input);
        assert(result.ok());
        return result.frame();
    }
};

void director_adapts_all_enabled_payloads_and_invokes_providers()
{
    static int scenePayload = 0;
    static int guiPayload = 0;
    auto desc = makeBaseWorkloadDesc();
    desc.scene = arp::renderer::CustomWorkloadSection::enabled(
        makePayloadRef(arp::renderer::CustomWorkloadPayloadKind::SceneDrawPlan,
            &scenePayload,
            kSceneContract,
            1u));
    desc.gui = arp::renderer::CustomWorkloadSection::enabled(makePayloadRef(
        arp::renderer::CustomWorkloadPayloadKind::GuiDrawData, &guiPayload, kGuiContract, 1u));
    const arp::renderer::CustomRenderFrameWorkload workload(desc);

    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    const auto backbuffer = createBackbuffer(graph, *device);

    CountingAdapter sceneAdapter(arp::renderer::CustomWorkloadPayloadKind::SceneDrawPlan,
        kSceneContract,
        "tests/SceneIntent");
    CountingAdapter guiAdapter(
        arp::renderer::CustomWorkloadPayloadKind::GuiDrawData, kGuiContract, "tests/GuiIntent");
    CountingProvider provider;

    arp::renderer::RenderFrameDirector director({false});
    assert(director.registerPayloadAdapter(sceneAdapter).success);
    assert(director.registerPayloadAdapter(guiAdapter).success);
    assert(director.registerPassProvider(provider).success);

    const auto result = director.compose({
        workload,
        graph,
        backbuffer,
        makeCompositionContext(desc.scene.payload(), true),
        nullptr,
        {},
    });
    assert(result.success);
    assert(result.decision.mode == arp::renderer::RenderFrameExecutionMode::ComposeGraph);
    assert(sceneAdapter.callCount() == 1);
    assert(guiAdapter.callCount() == 1);
    assert(provider.callCount() == 2);

    graph.execute();
    assert(graph.executionTrace().size() == 2u);
    assert(graph.executionTrace()[0] == arp::core::pmr::String("DirectorScenePass"));
    assert(graph.executionTrace()[1] == arp::core::pmr::String("DirectorGuiPass"));
}

void director_rejects_legacy_pipeline_without_explicit_fallback_contract()
{
    static int scenePayload = 0;
    auto desc = makeBaseWorkloadDesc();
    desc.scene = arp::renderer::CustomWorkloadSection::enabled(
        makePayloadRef(arp::renderer::CustomWorkloadPayloadKind::SceneDrawPlan,
            &scenePayload,
            "tests/LegacyOnlyPayload",
            1u));
    const arp::renderer::CustomRenderFrameWorkload workload(desc);

    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    const auto backbuffer = createBackbuffer(graph, *device);
    LegacyPipeline legacy;

    arp::renderer::RenderFrameDirector director({false});
    const auto result = director.compose({
        workload,
        graph,
        backbuffer,
        makeCompositionContext(desc.scene.payload(), true),
        &legacy,
        {},
    });
    assert(!result.success);
    assert(result.decision.mode == arp::renderer::RenderFrameExecutionMode::DiagnosticFailure);
    assert(!result.diagnostics.empty());
    assert(result.diagnostics.front().code
        == arp::renderer::RenderFrameDirectorDiagnosticCode::LegacyPipelineFallbackUsed);
    assert(result.diagnostics.front().message.find(
               "without an explicit Prototype/Legacy fallback resource contract")
        != arp::core::pmr::String::npos);
    assert(legacy.composeCalls() == 0);
}

void director_uses_legacy_pipeline_for_explicit_prototype_fallback_contracts()
{
    static int scenePayload = 0;
    auto desc = makeBaseWorkloadDesc();
    desc.scene = arp::renderer::CustomWorkloadSection::enabled(
        makePayloadRef(arp::renderer::CustomWorkloadPayloadKind::SceneDrawPlan,
            &scenePayload,
            "tests/LegacyOnlyPayload",
            1u));
    allowPrototypeLegacyFallback(desc);
    const arp::renderer::CustomRenderFrameWorkload workload(desc);

    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    const auto backbuffer = createBackbuffer(graph, *device);
    LegacyPipeline legacy;

    arp::renderer::RenderFrameDirector director({false});
    const auto result = director.compose({
        workload,
        graph,
        backbuffer,
        makeCompositionContext(desc.scene.payload(), true),
        &legacy,
        {},
    });
    assert(result.success);
    assert(result.decision.mode == arp::renderer::RenderFrameExecutionMode::LegacyPipelineFallback);
    assert(!result.diagnostics.empty());
    assert(result.diagnostics.front().code
        == arp::renderer::RenderFrameDirectorDiagnosticCode::LegacyPipelineFallbackUsed);
    assert(result.diagnostics.front().message.find("Prototype/Legacy fallback")
        != arp::core::pmr::String::npos);
    assert(legacy.composeCalls() == 1);

    graph.execute();
    assert(graph.executionTrace()[0] == arp::core::pmr::String("LegacyFallbackPass"));
}

void director_routes_legacy_scene_fallback_payload_only_for_fallback()
{
    SceneRuntimeFixture fixture;
    static int legacyScenePayload = 0;
    static int uncoveredParticlePayload = 0;
    auto desc = makeBaseWorkloadDesc();
    desc.scene = arp::renderer::CustomWorkloadSection::enabled(
        makePayloadRef(arp::renderer::CustomWorkloadPayloadKind::SceneDrawPlan,
            &fixture.payload,
            arp::renderer::kSceneRuntimePipelinePayloadContract,
            arp::renderer::kSceneRuntimePipelinePayloadVersion));
    desc.legacySceneFallback = arp::renderer::CustomWorkloadSection::enabled(
        makePayloadRef(arp::renderer::CustomWorkloadPayloadKind::SceneDrawPlan,
            &legacyScenePayload,
            "tests/LegacySceneFallback",
            7u));
    desc.particles = arp::renderer::CustomWorkloadSection::enabled(
        makePayloadRef(arp::renderer::CustomWorkloadPayloadKind::ParticleDrawPlan,
            &uncoveredParticlePayload,
            "tests/UncoveredParticlePayload",
            1u));
    allowPrototypeLegacyFallback(desc, "tests/DirectorExplicitLegacySceneFallback");
    const arp::renderer::CustomRenderFrameWorkload workload(desc);
    const auto workloadValidation = workload.validate();
    assert(workloadValidation.success);
    assert(workload.scenePayloadContractName()
        == arp::core::pmr::String(arp::renderer::kSceneRuntimePipelinePayloadContract));

    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    const auto backbuffer = createBackbuffer(graph, *device);
    LegacyPipeline legacy;

    arp::renderer::RenderFrameDirector director;
    const auto result = director.compose({
        workload,
        graph,
        backbuffer,
        makeCompositionContext(desc.scene.payload(), true),
        &legacy,
        {},
    });
    assert(result.success);
    assert(result.decision.mode == arp::renderer::RenderFrameExecutionMode::LegacyPipelineFallback);
    assert(legacy.composeCalls() == 1);
    assert(legacy.lastPayload() == &legacyScenePayload);
    assert(arp::core::pmr::String(legacy.lastPayloadContractName()) == "tests/LegacySceneFallback");
    assert(legacy.lastPayloadContractVersion() == 7u);
    assert(result.diagnostics.front().message.find("Prototype/Legacy fallback")
        != arp::core::pmr::String::npos);
}

void director_uses_default_scene_runtime_adapter_and_provider_before_legacy_pipeline()
{
    SceneRuntimeFixture fixture;
    auto desc = makeBaseWorkloadDesc();
    desc.scene = arp::renderer::CustomWorkloadSection::enabled(
        makePayloadRef(arp::renderer::CustomWorkloadPayloadKind::SceneDrawPlan,
            &fixture.payload,
            arp::renderer::kSceneRuntimePipelinePayloadContract,
            arp::renderer::kSceneRuntimePipelinePayloadVersion));
    const arp::renderer::CustomRenderFrameWorkload workload(desc);

    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    const auto backbuffer = createBackbuffer(graph, *device);
    LegacyPipeline legacy;
    auto preparedPlan = makePreparedSceneRuntimeFramePlan(fixture.frame);
    arp::renderer::RenderFramePreparedRendererFrameConfig preparedConfig;
    preparedConfig.preparedFramePlan = &preparedPlan;
    preparedConfig.framePlanBridge = {2u, 8u, 4u, 2u};
    preparedConfig.maxDrawCommandsPerPass = 16u;
    preparedConfig.shaderArtifactRootPolicy = "tests/explicit-scene-runtime-artifacts";

    arp::renderer::RenderFrameDirector director;
    const auto result = director.compose({
        workload,
        graph,
        backbuffer,
        makeCompositionContext(desc.scene.payload(), true),
        &legacy,
        {},
        preparedConfig,
    });
    assert(result.success);
    assert(result.decision.mode == arp::renderer::RenderFrameExecutionMode::PreparedSubmit);
    assert(legacy.composeCalls() == 0);
    assert(result.decision.preparedSubmitRequirements.size() == 1u);
    const auto& prepared = result.decision.preparedSubmitRequirements.front();
    assert(prepared.required);
    assert(prepared.plannerContractName
        == arp::core::pmr::String("renderer/SceneRuntimeRhiFramePlan"));
    assert(prepared.expectedDrawItemCount == fixture.frame.drawItemCount());
    assert(prepared.framePlan.drawPlans.size() == 1u);
    assert(prepared.framePlan.drawPlans.front().drawCommandCount == fixture.frame.drawItemCount());
    assert(prepared.framePlanBridge.maxPasses == 2u);
    assert(prepared.maxDrawCommandsPerPass == 16u);
    assert(prepared.shaderArtifactRootPolicy
        == arp::core::pmr::String("tests/explicit-scene-runtime-artifacts"));
    assert(!prepared.nativeHandleExposedToUpperLayer);

    graph.execute();
    assert(graph.executionTrace()[0] == arp::core::pmr::String("SceneRuntimeWorldUiPass"));
}

void director_rejects_prepared_submit_when_draw_contract_differs_from_prepared_plan()
{
    SceneRuntimeFixture fixture;
    auto desc = makeBaseWorkloadDesc();
    desc.scene = arp::renderer::CustomWorkloadSection::enabled(
        makePayloadRef(arp::renderer::CustomWorkloadPayloadKind::SceneDrawPlan,
            &fixture.payload,
            arp::renderer::kSceneRuntimePipelinePayloadContract,
            arp::renderer::kSceneRuntimePipelinePayloadVersion));
    const arp::renderer::CustomRenderFrameWorkload workload(desc);

    auto device = arp::rhi::createNullDevice();
    arp::render_graph::FrameGraph graph(*device);
    const auto backbuffer = createBackbuffer(graph, *device);
    auto mismatchedPreparedPlan = makePreparedSceneRuntimeFramePlan(fixture.frame);
    ++mismatchedPreparedPlan.drawPlans.front().drawCommandCount;

    arp::renderer::RenderFramePreparedRendererFrameConfig preparedConfig;
    preparedConfig.preparedFramePlan = &mismatchedPreparedPlan;

    arp::renderer::RenderFrameDirector director;
    const auto result = director.compose({
        workload,
        graph,
        backbuffer,
        makeCompositionContext(desc.scene.payload(), true),
        nullptr,
        {},
        preparedConfig,
    });

    assert(!result.success);
    assert(result.decision.mode == arp::renderer::RenderFrameExecutionMode::DiagnosticFailure);
    assert(!result.diagnostics.empty());
    assert(result.diagnostics.front().message.find(
               "draw contract differs from the prepared frame plan")
        != arp::core::pmr::String::npos);
}

} // namespace

int main()
{
    director_adapts_all_enabled_payloads_and_invokes_providers();
    director_rejects_legacy_pipeline_without_explicit_fallback_contract();
    director_uses_legacy_pipeline_for_explicit_prototype_fallback_contracts();
    director_routes_legacy_scene_fallback_payload_only_for_fallback();
    director_uses_default_scene_runtime_adapter_and_provider_before_legacy_pipeline();
    director_rejects_prepared_submit_when_draw_contract_differs_from_prepared_plan();
    return 0;
}
