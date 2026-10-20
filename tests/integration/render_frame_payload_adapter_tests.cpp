#include "2d/scene_2d_render_queue.h"
#include "3d/scene_3d_render_queue.h"
#include "core/cpu_allocator.h"
#include "renderer/frame/render_frame_payload_adapter.h"
#include "renderer/frame/scene_runtime_payload_adapter.h"
#include "renderer/pipeline/scene_runtime_pipeline_payload.h"
#include "renderer/scene_runtime_frame.h"
#include "scene/scene_traversal_snapshot.h"
#include "ui/ui.h"

#include <cassert>
#include <utility>

namespace {

arp::renderer::CustomWorkloadPayloadRef makePayloadRef(
    arp::renderer::CustomWorkloadPayloadKind kind,
    const char* contractName,
    std::uint32_t contractVersion)
{
    static int payloadToken = 0;
    return {
        kind,
        &payloadToken,
        contractName,
        contractVersion,
    };
}

void assertContains(const arp::core::pmr::String& haystack, const char* needle)
{
    assert(haystack.find(needle) != arp::core::pmr::String::npos);
}

void assertHasDiagnostic(const arp::renderer::RenderFramePayloadAdapterResult& result,
    arp::renderer::RenderFramePayloadAdapterDiagnosticCode code,
    const char* message)
{
    assert(!result.success);
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == code) {
            assertContains(diagnostic.message, message);
            return;
        }
    }
    assert(false && "expected payload adapter diagnostic was not reported");
}

class StaticAdapter final : public arp::renderer::IRenderFramePayloadAdapter {
public:
    StaticAdapter(arp::renderer::CustomWorkloadPayloadKind kind,
        const char* contractName,
        std::uint32_t contractVersion)
    {
        key_.kind = kind;
        key_.contractName = contractName;
        key_.contractVersion = contractVersion;
    }

    arp::renderer::RenderFramePayloadAdapterKey key() const override { return key_; }

    arp::renderer::RenderFramePayloadAdapterResult adapt(
        const arp::renderer::CustomWorkloadPayloadRef&) const override
    {
        arp::renderer::RenderFramePayloadAdapterResult result;
        result.success = true;
        result.intent.contractName = key_.contractName;
        result.intent.contractVersion = key_.contractVersion;
        return result;
    }

private:
    arp::renderer::RenderFramePayloadAdapterKey key_;
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
    item.clipRect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 32.0f, 16.0f);
    item.material = arp::renderer::MaterialHandle{12};
    item.texture = arp::renderer::TextureHandle{13};
    queue.addItem(item);
    return queue;
}

arp::renderer::RendererSceneRuntimeFrame makeRendererFrame(
    const arp::scene::SceneTraversalSnapshot& snapshot,
    const arp::core::pmr::Vector<arp::scene::ResolvedCamera>& cameras,
    const arp::scene3d::Scene3DRenderQueue& queue3D,
    const arp::scene2d::Scene2DRenderQueue& queue2D,
    const arp::ui::RenderQueue& uiQueue)
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

arp::renderer::SceneRuntimePipelinePayload makeSceneRuntimePayload(
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

arp::renderer::CustomWorkloadPayloadRef makeSceneRuntimePayloadRef(
    const arp::renderer::SceneRuntimePipelinePayload& payload)
{
    return {
        arp::renderer::CustomWorkloadPayloadKind::SceneDrawPlan,
        &payload,
        arp::renderer::kSceneRuntimePipelinePayloadContract,
        arp::renderer::kSceneRuntimePipelinePayloadVersion,
    };
}

void registry_looks_up_adapter_by_contract_key()
{
    StaticAdapter adapter(
        arp::renderer::CustomWorkloadPayloadKind::GuiDrawData, "tests/GuiPayload", 3u);

    arp::renderer::RenderFramePayloadAdapterRegistry registry;
    const auto registration = registry.registerAdapter(adapter);
    assert(registration.success);

    const auto result = registry.adaptPayload(makePayloadRef(
        arp::renderer::CustomWorkloadPayloadKind::GuiDrawData, "tests/GuiPayload", 3u));
    assert(result.success);
    assert(result.intent.contractName == arp::core::pmr::String("tests/GuiPayload"));
    assert(result.intent.contractVersion == 3u);
}

void registry_reports_deterministic_failures()
{
    StaticAdapter adapter(
        arp::renderer::CustomWorkloadPayloadKind::SceneDrawPlan, "tests/ScenePayload", 1u);

    arp::renderer::RenderFramePayloadAdapterRegistry registry;

    auto result = registry.adaptPayload(makePayloadRef(
        arp::renderer::CustomWorkloadPayloadKind::SceneDrawPlan, "tests/MissingPayload", 1u));
    assertHasDiagnostic(result,
        arp::renderer::RenderFramePayloadAdapterDiagnosticCode::UnknownAdapter,
        "unknown payload adapter");

    result = registry.adaptPayload(
        makePayloadRef(arp::renderer::CustomWorkloadPayloadKind::SceneDrawPlan, nullptr, 1u));
    assertHasDiagnostic(result,
        arp::renderer::RenderFramePayloadAdapterDiagnosticCode::MissingContractName,
        "contract name");

    auto registration = registry.registerAdapter(adapter);
    assert(registration.success);
    registration = registry.registerAdapter(adapter);
    assert(!registration.success);
    assert(registration.diagnostics[0].code
        == arp::renderer::RenderFramePayloadAdapterDiagnosticCode::DuplicateAdapter);

    result = registry.adaptPayload(makePayloadRef(
        arp::renderer::CustomWorkloadPayloadKind::SceneDrawPlan, "tests/ScenePayload", 2u));
    assertHasDiagnostic(result,
        arp::renderer::RenderFramePayloadAdapterDiagnosticCode::VersionMismatch,
        "version mismatch");
}

void scene_runtime_adapter_validates_before_casting_payload_object()
{
    arp::renderer::SceneRuntimePayloadAdapter adapter;
    arp::renderer::CustomWorkloadPayloadRef wrongVersion;
    wrongVersion.kind = arp::renderer::CustomWorkloadPayloadKind::SceneDrawPlan;
    wrongVersion.object = reinterpret_cast<const void*>(0x1);
    wrongVersion.contractName = arp::renderer::kSceneRuntimePipelinePayloadContract;
    wrongVersion.contractVersion = 99u;

    const auto result = adapter.adapt(wrongVersion);
    assertHasDiagnostic(result,
        arp::renderer::RenderFramePayloadAdapterDiagnosticCode::PayloadValidationFailed,
        "contract version mismatch");
}

void scene_runtime_adapter_emits_intent_from_valid_payload()
{
    auto snapshot = makeSnapshot();
    auto cameras = makeCameras();
    auto queue3D = make3DQueue();
    auto queue2D = make2DQueue();
    auto uiQueue = makeUiQueue();
    const auto frame = makeRendererFrame(snapshot, cameras, queue3D, queue2D, uiQueue);
    auto payload = makeSceneRuntimePayload(snapshot, cameras, queue3D, queue2D, uiQueue, frame);
    payload.requiresMultiplePhysicalPasses = true;
    payload.hasExplicitAttachmentContract = true;
    payload.hasExplicitPostprocessContract = false;
    payload.hasExplicitOffscreenContract = true;

    const arp::renderer::SceneRuntimePayloadAdapter adapter;
    const auto result = adapter.adapt(makeSceneRuntimePayloadRef(payload));
    assert(result.success);
    assert(result.diagnostics.empty());

    const auto& intent = result.intent;
    assert(intent.contractName
        == arp::core::pmr::String(arp::renderer::kSceneRuntimePipelinePayloadContract));
    assert(intent.contractVersion == arp::renderer::kSceneRuntimePipelinePayloadVersion);
    assert(intent.requiresMultiplePhysicalPasses);
    assert(intent.hasExplicitAttachmentContract);
    assert(!intent.hasExplicitPostprocessContract);
    assert(intent.hasExplicitOffscreenContract);

    assert(intent.sceneRequirements.size() == 1u);
    assert(intent.sceneRequirements[0].traversalSnapshot == &snapshot);
    assert(intent.sceneRequirements[0].rendererFrame == &frame);
    assert(intent.sceneRequirements[0].resolvedCameraCount == 1u);
    assert(intent.sceneRequirements[0].drawItemCount == 3u);
    assert(intent.sceneRequirements[0].materialResourceCount == 3u);
    assert(intent.sceneRequirements[0].textureResourceCount == 2u);
    assert(intent.sceneRequirements[0].compatibleOnePassIntent);

    assert(intent.queueRequirements.size() == 3u);
    assert(intent.queueRequirements[0].kind == arp::renderer::RenderFrameQueueKind::Scene3D);
    assert(intent.queueRequirements[0].order == 0u);
    assert(intent.queueRequirements[0].drawItemCount == 1u);
    assert(intent.queueRequirements[0].materialResourceCount == 1u);
    assert(intent.queueRequirements[0].textureResourceCount == 0u);
    assert(intent.queueRequirements[1].kind == arp::renderer::RenderFrameQueueKind::Scene2D);
    assert(intent.queueRequirements[1].order == 1u);
    assert(intent.queueRequirements[1].drawItemCount == 1u);
    assert(intent.queueRequirements[1].materialResourceCount == 1u);
    assert(intent.queueRequirements[1].textureResourceCount == 1u);
    assert(intent.queueRequirements[2].kind == arp::renderer::RenderFrameQueueKind::UI);
    assert(intent.queueRequirements[2].order == 2u);
    assert(intent.queueRequirements[2].drawItemCount == 1u);
    assert(intent.queueRequirements[2].materialResourceCount == 1u);
    assert(intent.queueRequirements[2].textureResourceCount == 1u);
}

} // namespace

int main()
{
    registry_looks_up_adapter_by_contract_key();
    registry_reports_deterministic_failures();
    scene_runtime_adapter_validates_before_casting_payload_object();
    scene_runtime_adapter_emits_intent_from_valid_payload();
    return 0;
}
