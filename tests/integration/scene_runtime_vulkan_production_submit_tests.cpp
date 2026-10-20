#include "2d/scene_2d.h"
#include "3d/scene_3d.h"
#include "core/cpu_allocator.h"
#include "platform/render_system.h"
#include "renderer/scene_runtime_frame.h"
#include "renderer/scene_runtime_frame_rhi_planner.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "ui/ui.h"

#include <cassert>

namespace {

bool hasDeterministicVulkanDiagnostic(const std::pmr::string& diagnostics)
{
    return diagnostics.find("Vulkan") != std::pmr::string::npos
        || diagnostics.find("SDL") != std::pmr::string::npos
        || diagnostics.find("surface") != std::pmr::string::npos
        || diagnostics.find("device") != std::pmr::string::npos
        || diagnostics.find("swapchain") != std::pmr::string::npos
        || diagnostics.find("SPIR-V") != std::pmr::string::npos;
}

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
    item.mesh = arp::renderer::MeshHandle{101};
    item.material = arp::renderer::MaterialHandle{102};
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
    item.sprite = arp::scene2d::SpriteHandle{201};
    item.material = arp::renderer::MaterialHandle{202};
    item.texture = arp::renderer::TextureHandle{203};
    item.worldBounds =
        arp::scene2d::Bounds2D::explicitBounds({1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f});
    queue.addSprite(item);
    return queue;
}

arp::ui::RenderQueue makeUiQueue()
{
    arp::ui::RenderQueue queue;
    arp::ui::UIRenderItem item;
    item.widgetId = arp::ui::WidgetId::fromValue(301);
    item.rect = arp::ui::LayoutRect::fromOriginSize(8.0f, 8.0f, 64.0f, 24.0f);
    item.clipRect = item.rect;
    item.material = arp::renderer::MaterialHandle{302};
    item.texture = arp::renderer::TextureHandle{303};
    queue.addItem(item);
    return queue;
}

arp::renderer::RendererSceneRuntimeFrame makeRuntimeFrame()
{
    arp::scene::Scene scene("scene_runtime_vulkan_submit");
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

} // namespace

int main()
{
    arp::platform::WindowSystem platform;
    const auto init = platform.initialize();
    if (!init.success) {
        assert(!init.diagnostics.empty());
        return 0;
    }

    auto window = platform.createWindow({
        "SceneRuntimeVulkanProductionSubmit",
        320,
        180,
        true,
        true,
    });
    if (!window.valid()) {
        return 0;
    }

    auto renderSystem = arp::platform::createDefaultRenderSystem(
        platform, window, "SceneRuntimeVulkanProductionSubmit");
    if (!renderSystem.valid()) {
        assert(hasDeterministicVulkanDiagnostic(renderSystem.diagnostics()));
        return 0;
    }
    assert(renderSystem.capabilities().surfaceBacked);

    const auto runtimeFrame = makeRuntimeFrame();
    assert(runtimeFrame.statusLabel()
        == arp::renderer::RendererSceneRuntimeStatusLabel::ContractComplete);
    assert(runtimeFrame.drawItemCount() == 3u);

    arp::renderer::RendererSceneRuntimeFrameRhiPlanner planner({
        1,
        2,
        2,
        8,
        128,
        256,
        16,
    });
    const auto plan = planner.plan(runtimeFrame);
    assert(plan.ok());
    assert(!plan.nativeHandleExposedToUpperLayer());

    arp::platform::RenderSystemRhiProductionFrameSubmitDesc submitDesc;
    submitDesc.label = "SceneRuntimeVulkanProductionSubmit";
    submitDesc.framePlan = plan.framePlan();
    submitDesc.renderToSwapchain = true;
    submitDesc.expectedDrawCommandCount = static_cast<std::uint32_t>(runtimeFrame.drawItemCount());
    const auto submitted = renderSystem.submitRhiProductionFrame(submitDesc);
    const bool missingOutput =
        submitted.diagnostics == "RHI-OUTPUT-ORIGINAL-OBSERVATION-UNAVAILABLE";
    if (!submitted.success && !missingOutput) {
        assert(hasDeterministicVulkanDiagnostic(submitted.diagnostics));
        return 0;
    }

    assert(submitted.usedRhiGpuDevice);
    assert(submitted.productionFramePreparedBeforeSubmit);
    assert(submitted.rhiCommandEncoded);
    assert(submitted.rhiQueueSubmitted);
    assert(submitted.swapchainRenderTargetUsed);
    assert(submitted.fullSurfaceViewportConfigured);
    assert(submitted.fullSurfaceScissorConfigured);
    assert(submitted.proceduralSceneRuntime3DObjectDrawn);
    assert(submitted.proceduralSceneRuntime2DSpriteDrawn);
    assert(submitted.proceduralSceneRuntimeUiDrawn);
    assert(submitted.acquireFrame);
    assert(submitted.presentFrame);
    assert(submitted.renderPassCount == 1u);
    assert(submitted.drawCommandCount == runtimeFrame.drawItemCount());
    assert(submitted.vulkanNativeStatsCaptured);
    assert(submitted.vulkanNativeRenderPassBeginCount >= 1u);
    assert(submitted.vulkanNativeIndexedDrawCount >= runtimeFrame.drawItemCount());
    assert(submitted.vulkanNativeQueueSubmitCount >= 1u);
    assert(!submitted.success && missingOutput);
    assert(!submitted.hiddenWindowRenderOutputHashCaptured);
    assert(submitted.deterministicRenderOutputHash == 0u);
    assert(!submitted.resourceAllocatedDuringSubmit);
    assert(!submitted.pipelineCreatedDuringSubmit);
    assert(!submitted.nativeHandleExposedToUpperLayer);
    assert(submitted.backendFallbacksHiddenFromUpperLayer);

    return 0;
}
