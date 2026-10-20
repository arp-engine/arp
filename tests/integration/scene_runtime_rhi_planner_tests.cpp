#include "2d/scene_2d.h"
#include "3d/scene_3d.h"
#include "renderer/pipeline/scene_runtime_pipeline_payload.h"
#include "renderer/pipeline/scene_runtime_pipeline_rhi_parity.h"
#include "renderer/pipeline/scene_runtime_render_pipeline.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "renderer/scene_runtime_frame.h"
#include "renderer/scene_runtime_frame_rhi_planner.h"
#include "rhi/backends/null/null_backend.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "ui/ui.h"

#include <array>
#include <cassert>
#include <cstring>
#include <limits>

bool legacySceneRuntimeReportAbiMatches(const arp::renderer::SceneRuntimePipelinePayload&,
    const arp::renderer::SceneRuntimeRenderPipelineSemanticRecord&,
    const arp::renderer::RendererSceneRuntimeFrameRhiPlannerDesc&,
    const arp::renderer::RendererRhiDrawPlanBridgeDesc&,
    bool,
    const arp::renderer::SceneRuntimeIndexedDrawMetadata* = nullptr);

namespace {

arp::scene::SceneTraversalSnapshot snapshotFor(arp::scene::Scene& scene)
{
    auto snapshot = scene.extractTraversalSnapshot({16});
    assert(snapshot.ok());
    return snapshot;
}

arp::scene3d::Scene3DRenderQueue make3DQueue(arp::scene::GameObjectId objectId)
{
    arp::scene3d::Scene3DRenderQueue queue;
    arp::scene3d::Scene3DRenderItem item;
    item.kind = arp::scene3d::Scene3DRenderItemKind::Mesh;
    item.objectId = objectId;
    item.mesh = arp::renderer::MeshHandle{11};
    item.material = arp::renderer::MaterialHandle{12};
    item.worldBounds =
        arp::scene3d::Bounds3D::explicitBounds({0.0f, 0.0f, 4.0f}, {1.0f, 1.0f, 1.0f});
    queue.addItem(item);
    return queue;
}

arp::scene2d::Scene2DRenderQueue make2DQueue(arp::scene::GameObjectId objectId)
{
    arp::scene2d::Scene2DRenderQueue queue;
    arp::scene2d::Scene2DRenderItem item;
    item.kind = arp::scene2d::Scene2DRenderItemKind::Sprite;
    item.objectId = objectId;
    item.sprite = arp::scene2d::SpriteHandle{21};
    item.material = arp::renderer::MaterialHandle{22};
    item.texture = arp::renderer::TextureHandle{23};
    item.worldBounds =
        arp::scene2d::Bounds2D::explicitBounds({1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f});
    queue.addSprite(item);
    return queue;
}

arp::ui::RenderQueue makeUiQueue()
{
    arp::ui::RenderQueue queue;
    arp::ui::UIRenderItem item;
    item.widgetId = arp::ui::WidgetId::fromValue(7);
    item.rect = arp::ui::LayoutRect::fromOriginSize(8.0f, 8.0f, 64.0f, 24.0f);
    item.clipRect = item.rect;
    item.material = arp::renderer::MaterialHandle{31};
    item.texture = arp::renderer::TextureHandle{32};
    queue.addItem(item);
    return queue;
}

arp::renderer::RendererSceneRuntimeFrame makeFrame()
{
    arp::scene::Scene scene("rhi_planner_scene");
    auto& meshObject = scene.createObject("mesh");
    auto& spriteObject = scene.createObject("sprite");
    auto& cameraObject = scene.createObject("camera");
    assert(scene.reparent(meshObject.id(), scene.rootId()).ok());
    assert(scene.reparent(spriteObject.id(), scene.rootId()).ok());
    assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());

    auto cameraDesc = arp::scene::CameraComponentDesc::orthographic(16.0f, 0.1f, 100.0f);
    cameraDesc.visibleQueues = arp::scene::RenderQueueMask::world2DAnd3D();
    cameraObject.addComponent<arp::scene::CameraComponent>(cameraDesc);

    const auto snapshot = snapshotFor(scene);
    const auto resolved = arp::scene::CameraResolver().resolve(scene, snapshot);
    assert(resolved.ok());

    const auto queue3d = make3DQueue(meshObject.id());
    const auto queue2d = make2DQueue(spriteObject.id());
    const auto uiQueue = makeUiQueue();

    arp::renderer::RendererSceneRuntimeFrameInput input;
    input.snapshot = &snapshot;
    input.resolvedCameras = &resolved.cameras();
    input.scene3DQueue = &queue3d;
    input.scene2DQueue = &queue2d;
    input.uiQueue = &uiQueue;
    input.oneCompatibleWorldUiPassIntent = true;

    arp::renderer::RendererSceneRuntimeFrameBuilder builder({3, 16, 16, 16});
    const auto result = builder.build(input);
    assert(result.ok());
    return result.frame();
}

struct SceneRuntimePipelineParityFixture {
    // Explicit argument fixtures: distinct index ranges and all five fields.
    const std::array<arp::render_graph::RhiDrawIndexedCommand, 3> drawArguments{
        {{3u, 1u, 0u, 0, 0u}, {6u, 2u, 3u, -1, 2u}, {12u, 1u, 9u, 4, 7u}}};

    arp::renderer::SceneRuntimeIndexedDrawMetadata drawMetadata() const
    {
        return {1u, drawArguments};
    }

    arp::scene::Scene scene;
    arp::scene::GameObjectId meshObjectId;
    arp::scene::GameObjectId spriteObjectId;
    arp::scene::SceneTraversalSnapshot snapshot;
    arp::core::pmr::Vector<arp::scene::ResolvedCamera> cameras;
    arp::scene3d::Scene3DRenderQueue queue3D;
    arp::scene2d::Scene2DRenderQueue queue2D;
    arp::ui::RenderQueue uiQueue;
    arp::renderer::RendererSceneRuntimeFrame frame;
    arp::renderer::SceneRuntimePipelinePayload payload;

    SceneRuntimePipelineParityFixture()
        : scene("rhi_parity_scene"), meshObjectId(arp::scene::GameObjectId::invalid()),
          spriteObjectId(arp::scene::GameObjectId::invalid()), snapshot(makeFixtureSnapshot()),
          cameras(makeFixtureCameras()), queue3D(make3DQueue(meshObjectId)),
          queue2D(make2DQueue(spriteObjectId)), uiQueue(makeUiQueue()), frame(makeFixtureFrame()),
          payload({
              &snapshot,
              &cameras,
              &queue3D,
              &queue2D,
              &uiQueue,
              &frame,
          })
    {}

private:
    arp::scene::SceneTraversalSnapshot makeFixtureSnapshot()
    {
        auto& meshObject = scene.createObject("mesh");
        auto& spriteObject = scene.createObject("sprite");
        auto& cameraObject = scene.createObject("camera");
        meshObjectId = meshObject.id();
        spriteObjectId = spriteObject.id();
        assert(scene.reparent(meshObject.id(), scene.rootId()).ok());
        assert(scene.reparent(spriteObject.id(), scene.rootId()).ok());
        assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());

        auto cameraDesc = arp::scene::CameraComponentDesc::orthographic(16.0f, 0.1f, 100.0f);
        cameraDesc.visibleQueues = arp::scene::RenderQueueMask::world2DAnd3D();
        cameraObject.addComponent<arp::scene::CameraComponent>(cameraDesc);
        return snapshotFor(scene);
    }

    arp::core::pmr::Vector<arp::scene::ResolvedCamera> makeFixtureCameras()
    {
        const auto resolved = arp::scene::CameraResolver().resolve(scene, snapshot);
        assert(resolved.ok());
        return resolved.cameras();
    }

    arp::renderer::RendererSceneRuntimeFrame makeFixtureFrame()
    {
        arp::renderer::RendererSceneRuntimeFrameInput input;
        input.snapshot = &snapshot;
        input.resolvedCameras = &cameras;
        input.scene3DQueue = &queue3D;
        input.scene2DQueue = &queue2D;
        input.uiQueue = &uiQueue;
        input.oneCompatibleWorldUiPassIntent = true;

        arp::renderer::RendererSceneRuntimeFrameBuilder builder({3, 16, 16, 16});
        const auto result = builder.build(input);
        assert(result.ok());
        return result.frame();
    }
};

arp::renderer::SceneRuntimeRenderPipelineSemanticRecord composeSceneRuntimeSemantic(
    const arp::renderer::SceneRuntimePipelinePayload& payload)
{
    auto device = arp::rhi::createNullDevice();
    assert(device);

    arp::render_graph::FrameGraph graph(*device);
    const auto backbuffer = graph.importTexture(device->createTexture({
        "SceneRuntimeParityBackbuffer",
        {320u, 180u, 1u},
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::RenderAttachment | arp::rhi::TextureUsage::Sampled,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::Present,
        1u,
    }));

    arp::renderer::SceneRuntimeRenderPipeline pipeline;
    const auto created = pipeline.create({
        *device,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureFormat::Unknown,
        {320, 180},
    });
    assert(created.success);

    arp::renderer::RenderFrameRequest request;
    request.view.extent = {320, 180};
    request.view.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    request.pipelinePayload = &payload;
    request.pipelinePayloadContractName = arp::renderer::kSceneRuntimePipelinePayloadContract;
    request.pipelinePayloadContractVersion = arp::renderer::kSceneRuntimePipelinePayloadVersion;

    arp::rhi::AcquiredFrame acquired;
    acquired.success = true;
    acquired.frameSerial = 1u;
    acquired.frameIndex = 0u;

    const auto composed = pipeline.composeGraph(graph, backbuffer, {acquired, request, true}, {});
    assert(composed.success);
    return pipeline.lastSemanticRecord();
}

void planner_lowers_scene_runtime_frame_to_one_scene_draw_plan()
{
    const auto frame = makeFrame();
    arp::renderer::RendererSceneRuntimeFrameRhiPlanner planner({
        1,
        2,
        2,
        8,
        128,
        256,
    });
    const auto result = planner.plan(frame);
    assert(result.ok());
    assert(result.statusLabel()
        == arp::renderer::RendererSceneRuntimeRhiStatusLabel::ContractComplete);
    assert(!result.nativeHandleExposedToUpperLayer());

    const auto& framePlan = result.framePlan();
    assert(framePlan.frameSlot == 1u);
    assert(framePlan.drawPlans.size() == 1u);
    const auto& drawPlan = framePlan.drawPlans[0];
    assert(drawPlan.name == std::pmr::string("SceneRuntimeWorldUiPass"));
    assert(drawPlan.workKind == arp::renderer::RendererRhiWorkKind::Scene);
    assert(drawPlan.frameSlot == 1u);
    assert(drawPlan.drawCommandCount == 3u);
    assert(drawPlan.pushConstantSizeBytes == 128u);
    assert(drawPlan.pipelinePushConstantCapacityBytes == 256u);
    assert(drawPlan.pipelineLayoutName == std::pmr::string("SceneRuntimeWorldUiPipelineLayout"));
    assert(drawPlan.expectedPipelineLayoutName
        == std::pmr::string("SceneRuntimeWorldUiPipelineLayout"));
    assert(drawPlan.descriptorLayoutNameCount == 2u);
    assert(drawPlan.descriptorLayoutNames[0] == std::pmr::string("SceneRuntimeFrameSet"));
    assert(drawPlan.descriptorLayoutNames[1] == std::pmr::string("SceneRuntimeMaterialSet"));
    assert(drawPlan.descriptorSetCount == 2u);
    assert(drawPlan.descriptorTableCount == 1u);
    assert(drawPlan.requiredMaterialResourceCount == 3u);
    assert(drawPlan.materialResourceCount == 3u);
    assert(drawPlan.allowSoftSecondaryCommands);
    assert(drawPlan.hasInputContract);
    assert(drawPlan.inputContract.kind == arp::rhi::RendererInputKind::ScenePass);
    assert(drawPlan.inputContract.drawCommandCount == 3u);
    assert(drawPlan.inputContract.meshCount == 1u);
    assert(drawPlan.inputContract.materialBindingCount == 3u);
    assert(!result.backendSpecificBranchingRequired());

    const auto bridged =
        arp::renderer::RendererRhiFramePlanBridge({2, 8, 4, 4}).buildFramePlan(framePlan);
    // Counts are requirements, never sufficient executable command metadata.
    assert(!bridged.success);
    assert(bridged.diagnostics.find("does not match explicit draw metadata")
        != std::pmr::string::npos);
    assert(!bridged.nativeHandleExposedToUpperLayer);
}

void planner_reports_deterministic_diagnostics_for_unsupported_lowering()
{
    const auto frame = makeFrame();
    arp::renderer::RendererSceneRuntimeFrameRhiPlanner planner({
        0,
        2,
        2,
        8,
        320,
        256,
    });
    const auto pushConstantFailure = planner.plan(frame);
    assert(!pushConstantFailure.ok());
    assert(pushConstantFailure.diagnostic().code
        == arp::renderer::RendererSceneRuntimeRhiDiagnosticCode::PushConstantCapacityExceeded);

    arp::renderer::RendererSceneRuntimeFrameRhiPlanner compatiblePlanner({
        0,
        2,
        2,
        8,
        128,
        256,
    });
    arp::renderer::RendererSceneRuntimeFrameRhiPlanRequest multiPassRequest;
    multiPassRequest.frame = &frame;
    multiPassRequest.requiresMultiplePhysicalPasses = true;
    const auto multiPassFailure = compatiblePlanner.plan(multiPassRequest);
    assert(!multiPassFailure.ok());
    assert(multiPassFailure.diagnostic().code
        == arp::renderer::RendererSceneRuntimeRhiDiagnosticCode::UnsupportedMultiPassRequirement);

    multiPassRequest.hasExplicitAttachmentContract = true;
    const auto explicitMultiPass = compatiblePlanner.plan(multiPassRequest);
    assert(explicitMultiPass.ok());
    assert(explicitMultiPass.framePlan().drawPlans.size() == 1u);
}

void custom_pipeline_semantics_match_rhi_planner_contract()
{
    SceneRuntimePipelineParityFixture fixture;
    const auto semantic = composeSceneRuntimeSemantic(fixture.payload);

    const auto result = arp::renderer::verifySceneRuntimePipelineRhiParityV2(fixture.payload,
        semantic,
        {
            1,
            2,
            2,
            8,
            128,
            256,
            16,
        },
        {2, 8, 4, 4},
        fixture.drawMetadata());

    assert(result.success);
    assert(result.diagnostics.empty());
    assert(result.planRequest.frame == &fixture.frame);
    assert(!result.planRequest.requiresMultiplePhysicalPasses);
    assert(result.framePlan.drawPlans.size() == 1u);
    const auto& drawPlan = result.framePlan.drawPlans.front();
    assert(drawPlan.name == std::pmr::string("SceneRuntimeWorldUiPass"));
    assert(drawPlan.workKind == arp::renderer::RendererRhiWorkKind::Scene);
    assert(drawPlan.drawCommandCount == fixture.frame.drawItemCount());
    assert(drawPlan.descriptorSetCount == 2u);
    assert(drawPlan.descriptorLayoutNames[0] == std::pmr::string("SceneRuntimeFrameSet"));
    assert(drawPlan.descriptorLayoutNames[1] == std::pmr::string("SceneRuntimeMaterialSet"));
    assert(drawPlan.requiredMaterialResourceCount == fixture.frame.materialResourceCount());
    assert(drawPlan.materialResourceCount == fixture.frame.materialResourceCount());
    assert(drawPlan.inputContract.sampledTextureCount == fixture.frame.textureResourceCount());
    assert(drawPlan.inputContract.backendOwnedStaging);
    assert(result.bridgedFramePlan.success);
    assert(result.backendReportCount == 5u);
    assert(result.backendReports[0].backend == arp::rhi::Backend::Null);
    assert(result.backendReports[0].supportLabel
        == arp::rhi::semantics::SceneRuntimeBackendSupportLabel::Supported);
    assert(result.backendReports[1].backend == arp::rhi::Backend::Vulkan);
    assert(result.backendReports[1].supportLabel
        == arp::rhi::semantics::SceneRuntimeBackendSupportLabel::DiagnosticFailure);
    assert(result.backendReports[2].backend == arp::rhi::Backend::Metal);
    assert(result.backendReports[2].supportLabel
        == arp::rhi::semantics::SceneRuntimeBackendSupportLabel::Supported);
    assert(result.backendReports[3].backend == arp::rhi::Backend::WebGPU);
    assert(result.backendReports[3].supportLabel
        == arp::rhi::semantics::SceneRuntimeBackendSupportLabel::DiagnosticFailure);
    // Vulkan needs queried device limits; WebGPU needs queried lowering limits.
    // Metal's existing specification contract declares setBytes Native.
    for (const std::size_t index : {1u, 3u})
        assert(result.backendReports[index].diagnostics.find("RHI-SCENE-PUSH-LOWERING-UNAVAILABLE")
            == 0u);
    assert(result.backendReports[4].backend == arp::rhi::Backend::WebGL);
    assert(result.backendReports[4].supportLabel
        == arp::rhi::semantics::SceneRuntimeBackendSupportLabel::DiagnosticFailure);
    assert(result.backendReports[4].diagnostics == "RHI-WEBGL1-SEMANTIC-REPORT-UNAVAILABLE");

    // Old declaration is compiled in another TU. No current V2 header can
    // accidentally make that consumer agree with a changed Legacy layout.
    const auto metadata = fixture.drawMetadata();
    assert(legacySceneRuntimeReportAbiMatches(
        fixture.payload, semantic, {1, 2, 2, 8, 128, 256, 16}, {2, 8, 4, 4}, true, &metadata));
    for (const auto pushBytes : {0u, 16u, 128u, 256u}) {
        const auto current = arp::renderer::verifySceneRuntimePipelineRhiParityV2(
            fixture.payload, semantic, {1, 2, 2, 8, pushBytes, 256, 16}, {2, 8, 4, 4}, metadata);
        assert(current.success && current.backendReportCount == 5u);
        for (std::size_t index = 0; index < 4u; ++index) {
            const bool structurallySupported = index == 0u || index == 2u || pushBytes == 0u;
            assert(current.backendReports[index].supportLabel
                == (structurallySupported
                        ? arp::rhi::semantics::SceneRuntimeBackendSupportLabel::Supported
                        : arp::rhi::semantics::SceneRuntimeBackendSupportLabel::DiagnosticFailure));
        }
        const auto legacy = arp::renderer::verifySceneRuntimePipelineRhiParity(
            fixture.payload, semantic, {1, 2, 2, 8, pushBytes, 256, 16}, {2, 8, 4, 4}, metadata);
        assert(legacy.success && legacy.backendReportCount == 4u);
        for (std::size_t index = 0; index < legacy.backendReportCount; ++index) {
            assert(legacy.backendReports[index].backend == current.backendReports[index].backend);
            assert(legacy.backendReports[index].supportLabel
                == current.backendReports[index].supportLabel);
            assert(legacy.backendReports[index].diagnostics
                == current.backendReports[index].diagnostics);
        }
        assert(current.backendReports[4].supportLabel
            == arp::rhi::semantics::SceneRuntimeBackendSupportLabel::DiagnosticFailure);
        assert(current.backendReports[4].diagnostics == "RHI-WEBGL1-SEMANTIC-REPORT-UNAVAILABLE");
    }
}

void custom_pipeline_multi_pass_payload_requires_explicit_metadata()
{
    SceneRuntimePipelineParityFixture fixture;
    const auto semantic = composeSceneRuntimeSemantic(fixture.payload);

    fixture.payload.requiresMultiplePhysicalPasses = true;
    auto result = arp::renderer::verifySceneRuntimePipelineRhiParityV2(fixture.payload,
        semantic,
        {
            1,
            2,
            2,
            8,
            128,
            256,
            16,
        },
        {2, 8, 4, 4});
    assert(!result.success);
    assert(result.backendReportCount == 0u);
    assert(legacySceneRuntimeReportAbiMatches(
        fixture.payload, semantic, {1, 2, 2, 8, 128, 256, 16}, {2, 8, 4, 4}, false));
    assert(result.planRequest.requiresMultiplePhysicalPasses);
    assert(result.diagnostics.find("multi-pass") != arp::core::pmr::String::npos);

    fixture.payload.hasExplicitAttachmentContract = true;
    result = arp::renderer::verifySceneRuntimePipelineRhiParityV2(fixture.payload,
        semantic,
        {
            1,
            2,
            2,
            8,
            128,
            256,
            16,
        },
        {2, 8, 4, 4},
        fixture.drawMetadata());
    assert(result.success);
}

void explicit_draw_metadata_is_required_bounded_and_snapshotted()
{
    using namespace arp;
    SceneRuntimePipelineParityFixture fixture;
    const auto semantic = composeSceneRuntimeSemantic(fixture.payload);
    const renderer::RendererSceneRuntimeFrameRhiPlannerDesc desc{1, 2, 2, 8, 128, 256, 3};
    const renderer::RendererRhiDrawPlanBridgeDesc bridge{2, 8, 4, 4};
    const auto request = renderer::sceneRuntimeRhiPlanRequestFromPayload(fixture.payload);
    const renderer::RendererSceneRuntimeFrameRhiPlanner planner(desc);
    const auto missing =
        renderer::verifySceneRuntimePipelineRhiParityV2(fixture.payload, semantic, desc, bridge);
    assert(!missing.success && missing.backendReportCount == 0u);
    assert(missing.diagnostics == "RHI-SCENE-DRAW-METADATA-MISSING");
    assert(legacySceneRuntimeReportAbiMatches(fixture.payload, semantic, desc, bridge, false));

    const auto refused = [&](const renderer::SceneRuntimeIndexedDrawMetadata& metadata,
                             const char* expected) {
        const auto result = renderer::verifySceneRuntimePipelineRhiParityV2(
            fixture.payload, semantic, desc, bridge, metadata);
        assert(!result.success && result.backendReportCount == 0u);
        assert(result.diagnostics == expected);
    };
    refused({0u, {}}, "RHI-SCENE-DRAW-METADATA-VERSION");
    refused({2u, fixture.drawArguments}, "RHI-SCENE-DRAW-METADATA-VERSION");
    refused({1u, {nullptr, 3u}}, "RHI-SCENE-DRAW-METADATA-STORAGE");
    refused({1u, {fixture.drawArguments.data(), 2u}}, "RHI-SCENE-DRAW-METADATA-COUNT");
    // Oversized view is refused before any read beyond the actual three elements.
    refused({1u, {fixture.drawArguments.data(), 4u}}, "RHI-SCENE-DRAW-METADATA-COUNT");
    refused({1u, {fixture.drawArguments.data(), std::numeric_limits<std::size_t>::max()}},
        "RHI-SCENE-DRAW-METADATA-COUNT");

    auto budget = desc;
    budget.maxDrawCommandCount = 2u;
    const auto exhausted =
        renderer::RendererSceneRuntimeFrameRhiPlanner(budget).plan(request, fixture.drawMetadata());
    assert(!exhausted.ok());
    assert(exhausted.diagnostic().code
        == renderer::RendererSceneRuntimeRhiDiagnosticCode::DrawItemBudgetExceeded);
    assert(exhausted.framePlan().drawPlans.empty());

    const auto snapshot = [&] {
        core::pmr::Vector<render_graph::RhiDrawIndexedCommand> source(
            fixture.drawArguments.begin(), fixture.drawArguments.end());
        const auto plan = planner.plan(request, {1u, source});
        assert(plan.ok());
        for (auto& command : source)
            command = {};
        return plan;
    }();
    const auto& copied = snapshot.framePlan().drawPlans.front().drawCommands;
    assert(copied.size() == fixture.drawArguments.size());
    for (std::size_t index = 0; index < copied.size(); ++index) {
        const auto& actual = copied[index];
        const auto& expected = fixture.drawArguments[index];
        assert(actual.indexCount == expected.indexCount
            && actual.instanceCount == expected.instanceCount);
        assert(actual.firstIndex == expected.firstIndex
            && actual.vertexOffset == expected.vertexOffset);
        assert(actual.firstInstance == expected.firstInstance);
    }
    const auto bridged =
        renderer::RendererRhiFramePlanBridge(bridge).buildFramePlan(snapshot.framePlan());
    assert(bridged.success && bridged.plan.passes.size() == 1u);
    assert(bridged.plan.passes.front().drawCommands.size() == copied.size());
    for (const bool invalidIndex : {false, true}) {
        auto invalid = fixture.drawArguments;
        if (invalidIndex)
            invalid[1].indexCount = 0u;
        else
            invalid[1].instanceCount = 0u;
        const auto result = renderer::verifySceneRuntimePipelineRhiParityV2(
            fixture.payload, semantic, desc, bridge, {1u, invalid});
        assert(!result.success && result.backendReportCount == 0u);
        assert(result.diagnostics.find("requires non-zero index and instance counts")
            != core::pmr::String::npos);
    }
}

} // namespace

int main()
{
    planner_lowers_scene_runtime_frame_to_one_scene_draw_plan();
    planner_reports_deterministic_diagnostics_for_unsupported_lowering();
    custom_pipeline_semantics_match_rhi_planner_contract();
    custom_pipeline_multi_pass_payload_requires_explicit_metadata();
    explicit_draw_metadata_is_required_bounded_and_snapshotted();
    return 0;
}
