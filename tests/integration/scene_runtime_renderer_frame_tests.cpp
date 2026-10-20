#include "2d/scene_2d.h"
#include "3d/scene_3d.h"
#include "renderer/scene_runtime_frame.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "ui/ui.h"

#include <cassert>

namespace {

using namespace arp::renderer;

arp::scene::SceneTraversalSnapshot snapshotFor(arp::scene::Scene& scene)
{
    arp::scene::SceneTraversalOptions options;
    options.maxNodeCount = 16;
    auto snapshot = scene.extractTraversalSnapshot(options);
    assert(snapshot.ok());
    return snapshot;
}

arp::scene3d::MeshComponentDesc meshDesc()
{
    arp::scene3d::MeshComponentDesc desc;
    desc.mesh = arp::renderer::MeshHandle{11};
    desc.material = arp::renderer::MaterialHandle{21};
    desc.localBounds =
        arp::scene3d::Bounds3D::explicitBounds({0.0f, 0.5f, 0.0f}, {0.5f, 1.0f, 0.5f});
    return desc;
}

arp::scene2d::Scene2DRenderQueue make2DQueue(arp::scene::GameObjectId objectId)
{
    arp::scene2d::Scene2DRenderQueue queue;
    arp::scene2d::Scene2DRenderItem item;
    item.kind = arp::scene2d::Scene2DRenderItemKind::Sprite;
    item.objectId = objectId;
    item.sprite = arp::scene2d::SpriteHandle{31};
    item.material = arp::renderer::MaterialHandle{32};
    item.texture = arp::renderer::TextureHandle{33};
    item.worldBounds =
        arp::scene2d::Bounds2D::explicitBounds({2.0f, 2.0f, 0.0f}, {1.0f, 1.0f, 0.0f});
    item.sortKey = 7;
    queue.addSprite(item);
    return queue;
}

arp::ui::RenderQueue makeUiQueue(arp::ui::WidgetId widgetId)
{
    arp::ui::RenderQueue queue;
    arp::ui::UIRenderItem item;
    item.widgetId = widgetId;
    item.rect = arp::ui::LayoutRect::fromOriginSize(8.0f, 8.0f, 64.0f, 24.0f);
    item.clipRect = item.rect;
    item.material = arp::renderer::MaterialHandle{41};
    item.texture = arp::renderer::TextureHandle{42};
    item.sortKey = 4;
    queue.addItem(item);
    return queue;
}

arp::ui::Canvas makeCanvas()
{
    arp::ui::CanvasDesc desc;
    desc.viewport = arp::ui::UiViewport::explicitPixels(320.0f, 180.0f);
    desc.safeArea = arp::ui::UiSafeArea::explicitInsets(0.0f, 0.0f, 0.0f, 0.0f);
    desc.scalePolicy = arp::ui::UiScalePolicy::fixed(1.0f);
    desc.maxWidgetCount = 4;
    return arp::ui::Canvas("renderer_frame_hud", desc);
}

void frame_builder_orders_3d_2d_and_ui_queues_without_backend_leaks()
{
    arp::scene::Scene scene("renderer_frame_scene");
    auto& meshObject = scene.createObject("mesh");
    auto& spriteObject = scene.createObject("sprite");
    auto& cameraObject = scene.createObject("camera");
    assert(scene.reparent(meshObject.id(), scene.rootId()).ok());
    assert(scene.reparent(spriteObject.id(), scene.rootId()).ok());
    assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());

    meshObject.transform().setLocalTranslation({0.0f, 0.0f, 4.0f});
    meshObject.addComponent<arp::scene3d::MeshComponent>(meshDesc());

    auto cameraDesc = arp::scene::CameraComponentDesc::orthographic(16.0f, 0.1f, 100.0f);
    cameraDesc.aspectPolicy = arp::scene::CameraAspectPolicy::fixed(1.0f);
    cameraDesc.visibleQueues = arp::scene::RenderQueueMask::world2DAnd3D();
    cameraObject.addComponent<arp::scene::CameraComponent>(cameraDesc);

    const auto snapshot = snapshotFor(scene);
    const auto resolved = arp::scene::CameraResolver().resolve(scene, snapshot);
    assert(resolved.ok());
    const auto extracted3d =
        arp::scene3d::Scene3DRenderExtractor().extract(scene, snapshot, resolved.cameras());
    assert(extracted3d.ok());
    const auto queue2d = make2DQueue(spriteObject.id());

    auto canvas = makeCanvas();
    arp::ui::WidgetDesc widgetDesc;
    widgetDesc.name = "hud";
    widgetDesc.localRect = arp::ui::LayoutRect::fromOriginSize(8.0f, 8.0f, 64.0f, 24.0f);
    widgetDesc.material = arp::renderer::MaterialHandle{41};
    widgetDesc.texture = arp::renderer::TextureHandle{42};
    auto& widget = canvas.createWidget(widgetDesc);
    assert(canvas.reparent(widget.id(), canvas.rootId()).ok());
    const auto layout = canvas.resolveLayout();
    assert(layout.ok());
    const auto extractedUi = arp::ui::RenderExtractor().extract(canvas, layout);
    assert(extractedUi.ok());

    RendererSceneRuntimeFrameInput input;
    input.snapshot = &snapshot;
    input.resolvedCameras = &resolved.cameras();
    input.scene3DQueue = &extracted3d.queue();
    input.scene2DQueue = &queue2d;
    input.uiQueue = &extractedUi.queue();
    input.oneCompatibleWorldUiPassIntent = true;

    RendererSceneRuntimeFrameBuilder builder({3, 16, 16, 16});
    const auto result = builder.build(input);
    assert(result.ok());
    assert(result.frame().statusLabel() == RendererSceneRuntimeStatusLabel::ContractComplete);
    assert(result.frame().queueRecords().size() == 3);
    assert(result.frame().queueRecords()[0].kind == RendererSceneRuntimeQueueKind::Scene3D);
    assert(result.frame().queueRecords()[1].kind == RendererSceneRuntimeQueueKind::Scene2D);
    assert(result.frame().queueRecords()[2].kind == RendererSceneRuntimeQueueKind::UI);
    assert(result.frame().queueRecords()[0].order < result.frame().queueRecords()[1].order);
    assert(result.frame().queueRecords()[1].order < result.frame().queueRecords()[2].order);
    assert(result.frame().drawItemCount() == 3);
    assert(result.frame().materialResourceCount() == 3);
    assert(result.frame().textureResourceCount() == 2);
    assert(result.frame().resolvedCameraCount() == 1);
    assert(result.frame().compatibleOnePassIntent());
    assert(!result.frame().exposesBackendHandles());
    assert(result.frame().backendHandleExposureCount() == 0);
    assert(!result.frame().copiesStaticGeometryPayloadInHotPath());
    assert(result.frame().staticGeometryPayloadCopyCount() == 0);
}

void diagnostics_cover_missing_camera_resources_bounds_and_one_pass_contract()
{
    arp::scene::Scene scene("renderer_frame_diagnostics");
    auto& meshObject = scene.createObject("mesh");
    assert(scene.reparent(meshObject.id(), scene.rootId()).ok());
    meshObject.addComponent<arp::scene3d::MeshComponent>(meshDesc());
    const auto snapshot = snapshotFor(scene);

    arp::scene3d::Scene3DRenderQueue queue3d;
    arp::scene3d::Scene3DRenderItem item;
    item.kind = arp::scene3d::Scene3DRenderItemKind::Mesh;
    item.objectId = meshObject.id();
    item.mesh = arp::renderer::MeshHandle{51};
    item.material = arp::renderer::MaterialHandle{52};
    item.worldBounds =
        arp::scene3d::Bounds3D::explicitBounds({0.0f, 0.0f, 2.0f}, {1.0f, 1.0f, 1.0f});
    queue3d.addItem(item);

    RendererSceneRuntimeFrameBuilder builder({3, 8, 8, 8});
    RendererSceneRuntimeFrameInput missingCameraInput;
    missingCameraInput.snapshot = &snapshot;
    missingCameraInput.scene3DQueue = &queue3d;
    const auto missingCamera = builder.build(missingCameraInput);
    assert(!missingCamera.ok());
    assert(missingCamera.diagnostic().code
        == RendererSceneRuntimeDiagnosticCode::MissingResolvedCamera);

    arp::core::pmr::Vector<arp::scene::ResolvedCamera> cameras;
    arp::scene::ResolvedCamera camera;
    camera.objectId = meshObject.id();
    camera.visibleQueues = arp::scene::RenderQueueMask::world2DAnd3D();
    cameras.push_back(camera);

    arp::scene3d::Scene3DRenderQueue missingHandleQueue;
    auto missingHandleItem = item;
    missingHandleItem.mesh = arp::renderer::MeshHandle{};
    missingHandleQueue.addItem(missingHandleItem);
    RendererSceneRuntimeFrameInput missingHandleInput;
    missingHandleInput.snapshot = &snapshot;
    missingHandleInput.resolvedCameras = &cameras;
    missingHandleInput.scene3DQueue = &missingHandleQueue;
    const auto missingHandle = builder.build(missingHandleInput);
    assert(!missingHandle.ok());
    assert(missingHandle.diagnostic().code
        == RendererSceneRuntimeDiagnosticCode::MissingRendererHandle);

    arp::scene3d::Scene3DRenderQueue missingBoundsQueue;
    auto missingBoundsItem = item;
    missingBoundsItem.worldBounds = arp::scene3d::Bounds3D{};
    missingBoundsQueue.addItem(missingBoundsItem);
    RendererSceneRuntimeFrameInput missingBoundsInput;
    missingBoundsInput.snapshot = &snapshot;
    missingBoundsInput.resolvedCameras = &cameras;
    missingBoundsInput.scene3DQueue = &missingBoundsQueue;
    const auto missingBounds = builder.build(missingBoundsInput);
    assert(!missingBounds.ok());
    assert(missingBounds.diagnostic().code == RendererSceneRuntimeDiagnosticCode::MissingBounds);

    RendererSceneRuntimeFrameInput incompatiblePassInput;
    incompatiblePassInput.snapshot = &snapshot;
    incompatiblePassInput.resolvedCameras = &cameras;
    incompatiblePassInput.scene3DQueue = &queue3d;
    incompatiblePassInput.oneCompatibleWorldUiPassIntent = false;
    const auto incompatiblePass = builder.build(incompatiblePassInput);
    assert(!incompatiblePass.ok());
    assert(incompatiblePass.diagnostic().code
        == RendererSceneRuntimeDiagnosticCode::IncompatibleOnePassIntent);
}

} // namespace

int main()
{
    frame_builder_orders_3d_2d_and_ui_queues_without_backend_leaks();
    diagnostics_cover_missing_camera_resources_bounds_and_one_pass_contract();
    return 0;
}
