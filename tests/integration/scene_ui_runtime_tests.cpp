#include "input/input.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "ui/ui.h"

#include <cassert>
#include <cmath>

namespace {

using namespace arp::input;
using namespace arp::scene;
using namespace arp::ui;

bool nearly(float lhs, float rhs)
{
    return std::fabs(lhs - rhs) < 0.0001f;
}

CanvasDesc hudCanvasDesc()
{
    CanvasDesc desc;
    desc.viewport = UiViewport::explicitPixels(320.0f, 180.0f);
    desc.safeArea = UiSafeArea::explicitInsets(8.0f, 4.0f, 8.0f, 4.0f);
    desc.scalePolicy = UiScalePolicy::fixed(1.0f);
    desc.maxWidgetCount = 16;
    return desc;
}

WidgetDesc widgetDesc(
    const char* name, LayoutRect localRect, uint64_t order, bool focusable = false)
{
    WidgetDesc desc;
    desc.name = name;
    desc.localRect = localRect;
    desc.material = arp::renderer::MaterialHandle{static_cast<uint32_t>(100 + order)};
    desc.texture = arp::renderer::TextureHandle{static_cast<uint32_t>(200 + order)};
    desc.order = order;
    desc.acceptsFocus = focusable;
    return desc;
}

void canvas_widgets_layout_hit_test_focus_and_render_queue_are_deterministic()
{
    Runtime runtime("hud", hudCanvasDesc());
    auto& canvas = runtime.canvas();
    assert(canvas.rootId().valid());

    ControlDesc panelDesc;
    panelDesc.type = UiControlType::Panel;
    panelDesc.widget =
        widgetDesc("panel", LayoutRect::fromOriginSize(10.0f, 12.0f, 120.0f, 64.0f), 10);
    const auto panelId = runtime.createControl(panelDesc, runtime.rootId()).widgetId();
    auto& panel = *canvas.findWidget(panelId);
    ControlDesc buttonDesc;
    buttonDesc.type = UiControlType::TextInput;
    buttonDesc.widget =
        widgetDesc("button", LayoutRect::fromOriginSize(16.0f, 14.0f, 48.0f, 18.0f), 20, true);
    const auto buttonId = runtime.createControl(buttonDesc, runtime.rootId()).widgetId();
    auto& button = *canvas.findWidget(buttonId);
    ControlDesc overlayDesc;
    overlayDesc.type = UiControlType::Panel;
    overlayDesc.widget =
        widgetDesc("overlay", LayoutRect::fromOriginSize(12.0f, 16.0f, 32.0f, 12.0f), 30);
    const auto overlayId = runtime.createControl(overlayDesc, runtime.rootId()).widgetId();
    auto& overlay = *canvas.findWidget(overlayId);

    assert(panel.id().valid());
    assert(button.id().valid());
    assert(overlay.id().valid());
    assert(panel.id() != button.id());
    assert(canvas.reparent(panel.id(), canvas.rootId()).ok());
    assert(canvas.reparent(button.id(), panel.id()).ok());
    assert(canvas.reparent(overlay.id(), panel.id()).ok());

    const auto traversal = canvas.traversal();
    assert(traversal.size() == 4);
    assert(traversal[0] == canvas.rootId());
    assert(traversal[1] == panel.id());
    assert(traversal[2] == button.id());
    assert(traversal[3] == overlay.id());
    assert(!canvas.usesGameObjectHierarchyForScreenSpaceUi());
    assert(!canvas.requiresCameraForScreenSpaceLayout());

    const auto layout = canvas.resolveLayout();
    assert(layout.ok());
    assert(layout.resolvedWidgets().size() == 4);
    const auto panelRect = layout.requireWidget(panel.id()).rect;
    const auto buttonRect = layout.requireWidget(button.id()).rect;
    assert(nearly(panelRect.x, 18.0f));
    assert(nearly(panelRect.y, 16.0f));
    assert(nearly(buttonRect.x, 34.0f));
    assert(nearly(buttonRect.y, 30.0f));
    assert(nearly(buttonRect.width, 48.0f));
    assert(nearly(buttonRect.height, 18.0f));

    const auto hit = canvas.hitTest(layout, {40.0f, 34.0f});
    assert(hit.ok());
    assert(hit.widgetId() == overlay.id());
    assert(hit.order() == 30);

    InputSystem inputSystem(InputSystemDesc{1});
    InputFrame frame;
    frame.add(InputEvent::pointerMoved(68.0f, 32.0f));
    frame.add(InputEvent::mouseButtonPressed(InputMouseButton::Left));
    frame.add(InputEvent::textInput("A"));

    InputRouter router;
    const auto routed = router.dispatchFrame(runtime, frame, {0, 16});
    assert(inputSystem.beginFrame(frame, routed.eventConsumption).ok());
    assert(runtime.findControl(button.id())->value().text == "A");
    assert(routed.ok());
    assert(routed.eventConsumption[1].pointerConsumed());
    assert(routed.eventConsumption[2].textConsumed());
    assert(routed.eventConsumption[1].anyConsumed());
    assert(runtime.focusedControl() == button.id());
    assert(runtime.capturedControl() == button.id());
    assert(router.focusedWidget() == button.id());
    assert(router.capturedWidget() == button.id());

    const auto extracted = RenderExtractor().extract(canvas, layout);
    assert(extracted.ok());
    assert(extracted.queue().items().size() == 3);
    assert(extracted.queue().items()[0].widgetId == panel.id());
    assert(extracted.queue().items()[1].widgetId == button.id());
    assert(extracted.queue().items()[2].widgetId == overlay.id());
    assert(extracted.queue().items()[1].clipRect.x == buttonRect.x);
    assert(extracted.queue().items()[1].sortKey < extracted.queue().items()[2].sortKey);
    assert(!extracted.queue().exposesNativeBackendHandles());
    assert(!extracted.queue().generatesRawStaticVertexDataInSceneHotPath());
    assert(extracted.queue().rawStaticVertexDataCount() == 0);
}

void diagnostics_are_deterministic_for_invalid_ui_contracts()
{
    Canvas canvas("diagnostics", hudCanvasDesc());
    auto& parent = canvas.createWidget(
        widgetDesc("parent", LayoutRect::fromOriginSize(0.0f, 0.0f, 60.0f, 40.0f), 1));
    auto& child = canvas.createWidget(
        widgetDesc("child", LayoutRect::fromOriginSize(4.0f, 4.0f, 20.0f, 20.0f), 2, true));
    assert(canvas.reparent(parent.id(), canvas.rootId()).ok());
    assert(canvas.reparent(child.id(), parent.id()).ok());

    const auto invalidWidget = canvas.reparent(WidgetId::fromValue(999), canvas.rootId());
    assert(!invalidWidget.ok());
    assert(invalidWidget.diagnostic().code == UiDiagnosticCode::InvalidWidgetId);

    const auto invalidParent = canvas.reparent(child.id(), WidgetId::fromValue(998));
    assert(!invalidParent.ok());
    assert(invalidParent.diagnostic().code == UiDiagnosticCode::InvalidParentId);

    const auto cycle = canvas.reparent(parent.id(), child.id());
    assert(!cycle.ok());
    assert(cycle.diagnostic().code == UiDiagnosticCode::LayoutCycle);

    CanvasDesc missingViewportDesc = hudCanvasDesc();
    missingViewportDesc.viewport = {};
    Canvas missingViewport("missing_viewport", missingViewportDesc);
    const auto missingViewportLayout = missingViewport.resolveLayout();
    assert(!missingViewportLayout.ok());
    assert(missingViewportLayout.diagnostic().code == UiDiagnosticCode::MissingViewportMetadata);

    CanvasDesc missingSafeAreaDesc = hudCanvasDesc();
    missingSafeAreaDesc.safeArea = {};
    Canvas missingSafeArea("missing_safe_area", missingSafeAreaDesc);
    const auto missingSafeAreaLayout = missingSafeArea.resolveLayout();
    assert(!missingSafeAreaLayout.ok());
    assert(missingSafeAreaLayout.diagnostic().code == UiDiagnosticCode::MissingSafeAreaMetadata);

    InputRouter router;
    assert(router.beginCapture(canvas, child.id()).ok());
    const auto duplicateCapture = router.beginCapture(canvas, parent.id());
    assert(!duplicateCapture.ok());
    assert(duplicateCapture.diagnostic().code == UiDiagnosticCode::DuplicateFocusCapture);
}

void world_space_ui_anchor_projection_is_an_explicit_bridge()
{
    Scene scene("ui_anchor_scene");
    auto& target = scene.createObject("target");
    auto& cameraObject = scene.createObject("camera");
    assert(scene.reparent(target.id(), scene.rootId()).ok());
    assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());
    target.transform().setLocalTranslation({4.0f, 2.0f, 0.0f});
    cameraObject.transform().setLocalTranslation({0.0f, 0.0f, -10.0f});

    auto cameraDesc = CameraComponentDesc::orthographic(20.0f, 0.1f, 100.0f);
    cameraDesc.aspectPolicy = CameraAspectPolicy::fixed(16.0f / 9.0f);
    cameraDesc.visibleQueues = RenderQueueMask::world2DAnd3D();
    cameraObject.addComponent<CameraComponent>(cameraDesc);

    Canvas canvas("hud", hudCanvasDesc());
    auto& nameplate = canvas.createWidget(
        widgetDesc("nameplate", LayoutRect::fromOriginSize(0.0f, 0.0f, 64.0f, 12.0f), 40));
    assert(canvas.reparent(nameplate.id(), canvas.rootId()).ok());

    WorldAnchorComponentDesc anchorDesc;
    anchorDesc.widget = nameplate.id();
    anchorDesc.pixelSize = {64.0f, 12.0f};
    anchorDesc.viewportOffset = {0.0f, -8.0f};
    target.addComponent<WorldAnchorComponent>(anchorDesc);

    const auto snapshot = scene.extractTraversalSnapshot(SceneTraversalOptions{8});
    assert(snapshot.ok());
    const auto resolved = CameraResolver().resolve(scene, snapshot);
    assert(resolved.ok());
    assert(resolved.cameras().size() == 1);

    const auto anchors =
        WorldAnchorProjector().project(scene, snapshot, resolved.cameras()[0], canvas);
    assert(anchors.ok());
    assert(anchors.anchors().size() == 1);
    assert(anchors.anchors()[0].objectId == target.id());
    assert(anchors.anchors()[0].widgetId == nameplate.id());
    assert(anchors.anchors()[0].screenRect.width == 64.0f);
    assert(!anchors.usesGameObjectHierarchyForUiTree());
    assert(!anchors.exposesNativeBackendHandles());

    const auto missing = WorldAnchorProjector().projectMissingTargetForTest(
        WidgetId::fromValue(777), scene, snapshot, resolved.cameras()[0], canvas);
    assert(!missing.ok());
    assert(missing.diagnostic().code == UiDiagnosticCode::MissingWorldAnchorTarget);

    auto unsupportedCamera = resolved.cameras()[0];
    unsupportedCamera.projection = CameraProjection::Perspective;
    const auto unsupported =
        WorldAnchorProjector().project(scene, snapshot, unsupportedCamera, canvas);
    assert(!unsupported.ok());
    assert(unsupported.diagnostic().code == UiDiagnosticCode::UnsupportedWorldAnchorProjection);
}

} // namespace

int main()
{
    canvas_widgets_layout_hit_test_focus_and_render_queue_are_deterministic();
    diagnostics_are_deterministic_for_invalid_ui_contracts();
    world_space_ui_anchor_projection_is_an_explicit_bridge();
    return 0;
}
