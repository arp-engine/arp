#include "3d/scene_3d_render_queue.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/pipeline/custom_render_frame_workload.h"
#include "renderer/pipeline/render_pipeline.h"
#include "renderer/pipeline/scene_runtime_pipeline_payload.h"
#include "renderer/scene_runtime_frame.h"
#include "scene/scene_traversal_snapshot.h"
#include "ui/ui.h"

#include <cassert>

namespace {

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

arp::renderer::RendererSceneRuntimeFrame makeRendererFrame(
    const arp::scene::SceneTraversalSnapshot& snapshot,
    const arp::core::pmr::Vector<arp::scene::ResolvedCamera>& cameras,
    const arp::scene3d::Scene3DRenderQueue& queue3D)
{
    arp::scene2d::Scene2DRenderQueue queue2D;
    arp::ui::RenderQueue uiQueue;

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

arp::renderer::SceneRuntimePipelinePayload makePayload(
    const arp::scene::SceneTraversalSnapshot& snapshot,
    const arp::core::pmr::Vector<arp::scene::ResolvedCamera>& cameras,
    const arp::scene3d::Scene3DRenderQueue& queue3D,
    const arp::scene2d::Scene2DRenderQueue& queue2D,
    const arp::ui::RenderQueue& uiQueue,
    const arp::renderer::RendererSceneRuntimeFrame& frame)
{
    return {
        &snapshot,
        &cameras,
        &queue3D,
        &queue2D,
        &uiQueue,
        &frame,
    };
}

arp::renderer::CustomWorkloadPayloadRef makePayloadRef(
    const arp::renderer::SceneRuntimePipelinePayload& payload)
{
    return {
        arp::renderer::CustomWorkloadPayloadKind::SceneDrawPlan,
        &payload,
        arp::renderer::kSceneRuntimePipelinePayloadContract,
        arp::renderer::kSceneRuntimePipelinePayloadVersion,
    };
}

void assertContains(const arp::core::pmr::String& haystack, const char* needle)
{
    assert(haystack.find(needle) != arp::core::pmr::String::npos);
}

void render_frame_request_exposes_scene_payload_contract_metadata()
{
    static int scenePayloadToken = 0;

    const auto sceneSection = arp::renderer::CustomWorkloadSection::enabled({
        arp::renderer::CustomWorkloadPayloadKind::SceneDrawPlan,
        &scenePayloadToken,
        arp::renderer::kSceneRuntimePipelinePayloadContract,
        arp::renderer::kSceneRuntimePipelinePayloadVersion,
    });

    arp::renderer::RenderFrameRequest request;
    request.pipelinePayload = sceneSection.payload().object;
    request.pipelinePayloadContractName = sceneSection.payload().contractName;
    request.pipelinePayloadContractVersion = sceneSection.payload().contractVersion;

    assert(request.pipelinePayload == &scenePayloadToken);
    assert(request.pipelinePayloadContractName != nullptr);
    assert(request.pipelinePayloadContractVersion == 1u);
}

void workload_exposes_scene_payload_contract_metadata()
{
    auto snapshot = makeSnapshot();
    auto cameras = makeCameras();
    auto queue3D = make3DQueue();
    arp::scene2d::Scene2DRenderQueue queue2D;
    arp::ui::RenderQueue uiQueue;
    const auto frame = makeRendererFrame(snapshot, cameras, queue3D);
    const auto payload = makePayload(snapshot, cameras, queue3D, queue2D, uiQueue, frame);

    arp::renderer::CustomRenderFrameWorkloadDesc desc;
    desc.extent = arp::rhi::Extent2D{320, 180};
    desc.colorFormat = arp::rhi::TextureFormat::Bgra8Unorm;
    desc.frameSerial = 9u;
    desc.backbufferState = arp::rhi::ResourceState::Present;
    desc.scene = arp::renderer::CustomWorkloadSection::enabled(makePayloadRef(payload));

    const arp::renderer::CustomRenderFrameWorkload workload(desc);
    const auto validated = workload.validate();
    assert(validated.success);
    assert(
        workload.scenePayloadContractName() == arp::renderer::kSceneRuntimePipelinePayloadContract);
    assert(workload.scenePayloadContractVersion()
        == arp::renderer::kSceneRuntimePipelinePayloadVersion);
}

void scene_payload_validation_accepts_complete_contract()
{
    auto snapshot = makeSnapshot();
    auto cameras = makeCameras();
    auto queue3D = make3DQueue();
    arp::scene2d::Scene2DRenderQueue queue2D;
    arp::ui::RenderQueue uiQueue;
    const auto frame = makeRendererFrame(snapshot, cameras, queue3D);
    const auto payload = makePayload(snapshot, cameras, queue3D, queue2D, uiQueue, frame);

    const auto validation =
        arp::renderer::validateSceneRuntimePipelinePayload(makePayloadRef(payload));
    assert(validation.success);
    assert(validation.diagnostics.empty());

    const auto section = arp::renderer::CustomWorkloadSection::enabled(makePayloadRef(payload));
    assert(arp::renderer::sceneRuntimePipelinePayloadFrom(section) == &payload);
}

void scene_payload_validation_reports_deterministic_failures()
{
    auto snapshot = makeSnapshot();
    auto cameras = makeCameras();
    auto queue3D = make3DQueue();
    arp::scene2d::Scene2DRenderQueue queue2D;
    arp::ui::RenderQueue uiQueue;
    const auto frame = makeRendererFrame(snapshot, cameras, queue3D);
    auto payload = makePayload(snapshot, cameras, queue3D, queue2D, uiQueue, frame);

    auto missingObject = makePayloadRef(payload);
    missingObject.object = nullptr;
    auto validation = arp::renderer::validateSceneRuntimePipelinePayload(missingObject);
    assert(!validation.success);
    assertContains(validation.diagnostics, "payload object");

    auto missingContractName = makePayloadRef(payload);
    missingContractName.contractName = nullptr;
    validation = arp::renderer::validateSceneRuntimePipelinePayload(missingContractName);
    assert(!validation.success);
    assertContains(validation.diagnostics, "contract name");

    auto missingVersion = makePayloadRef(payload);
    missingVersion.contractVersion = 0u;
    validation = arp::renderer::validateSceneRuntimePipelinePayload(missingVersion);
    assert(!validation.success);
    assertContains(validation.diagnostics, "contract version");

    auto wrongName = makePayloadRef(payload);
    wrongName.contractName = "renderer/WrongPayload";
    validation = arp::renderer::validateSceneRuntimePipelinePayload(wrongName);
    assert(!validation.success);
    assertContains(validation.diagnostics, "contract name mismatch");

    auto wrongVersion = makePayloadRef(payload);
    wrongVersion.contractVersion = 99u;
    validation = arp::renderer::validateSceneRuntimePipelinePayload(wrongVersion);
    assert(!validation.success);
    assertContains(validation.diagnostics, "contract version mismatch");

    arp::renderer::RendererSceneRuntimeFrame invalidFrame;
    auto invalidFramePayload =
        makePayload(snapshot, cameras, queue3D, queue2D, uiQueue, invalidFrame);
    validation =
        arp::renderer::validateSceneRuntimePipelinePayload(makePayloadRef(invalidFramePayload));
    assert(!validation.success);
    assertContains(validation.diagnostics, "RendererSceneRuntimeFrame status");

    payload.resolvedCameras = nullptr;
    validation = arp::renderer::validateSceneRuntimePipelinePayload(makePayloadRef(payload));
    assert(!validation.success);
    assertContains(validation.diagnostics, "resolved cameras");

    payload = makePayload(snapshot, cameras, queue3D, queue2D, uiQueue, frame);
    payload.scene2DQueue = nullptr;
    validation = arp::renderer::validateSceneRuntimePipelinePayload(makePayloadRef(payload));
    assert(!validation.success);
    assertContains(validation.diagnostics, "2D queue");
}

} // namespace

int main()
{
    render_frame_request_exposes_scene_payload_contract_metadata();
    workload_exposes_scene_payload_contract_metadata();
    scene_payload_validation_accepts_complete_contract();
    scene_payload_validation_reports_deterministic_failures();
    return 0;
}
