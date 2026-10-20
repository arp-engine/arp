#include "scene/camera.h"
#include "scene/scene.h"

#include <cassert>
#include <cmath>

namespace {

using namespace arp::scene;

SceneTraversalSnapshot snapshotFor(Scene& scene)
{
    SceneTraversalOptions options;
    options.maxNodeCount = 16;
    auto snapshot = scene.extractTraversalSnapshot(options);
    assert(snapshot.ok());
    return snapshot;
}

void camera_is_an_ordinary_game_object_component()
{
    Scene scene("camera_contract_scene");
    auto& cameraObject = scene.createObject("main_camera");
    assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());
    cameraObject.transform().setLocalTranslation({3.0f, 4.0f, -10.0f});

    auto desc = CameraComponentDesc::orthographic(32.0f, 0.1f, 200.0f);
    desc.aspectPolicy = CameraAspectPolicy::fixed(16.0f / 9.0f);
    desc.viewport = ViewportRect::normalized(0.0f, 0.0f, 1.0f, 1.0f);
    desc.visibleQueues = RenderQueueMask::world2DAnd3D();
    desc.priority = 10;
    desc.clearIntent = CameraClearIntent::load();
    auto& camera = cameraObject.addComponent<CameraComponent>(desc);

    const auto resolved = CameraResolver().resolve(scene, snapshotFor(scene));
    assert(resolved.ok());
    assert(resolved.cameras().size() == 1);
    assert(!resolved.usesSpecialCameraObjectClass());

    const auto& resolvedCamera = resolved.cameras()[0];
    assert(resolvedCamera.objectId == cameraObject.id());
    assert(resolvedCamera.componentId == camera.id());
    assert(resolvedCamera.projection == CameraProjection::Orthographic);
    assert(resolvedCamera.visibleQueues.includes2D());
    assert(resolvedCamera.visibleQueues.includes3D());
    assert(resolvedCamera.priority == 10);
    assert(resolvedCamera.clearIntent.metadataOnly());
    assert(resolvedCamera.worldTransform.translation.x == 3.0f);
    assert(resolvedCamera.camera.position.z == -10.0f);
}

void resolver_orders_by_priority_then_stable_object_id()
{
    Scene scene("camera_order_scene");
    auto& lowPriority = scene.createObject("low_priority");
    auto& highPriority = scene.createObject("high_priority");
    auto& samePriority = scene.createObject("same_priority");
    assert(scene.reparent(lowPriority.id(), scene.rootId()).ok());
    assert(scene.reparent(highPriority.id(), scene.rootId()).ok());
    assert(scene.reparent(samePriority.id(), scene.rootId()).ok());

    auto lowDesc = CameraComponentDesc::perspective(60.0f, 1.0f, 0.1f, 100.0f);
    lowDesc.priority = 1;
    auto highDesc = CameraComponentDesc::perspective(60.0f, 1.0f, 0.1f, 100.0f);
    highDesc.priority = 20;
    auto sameDesc = CameraComponentDesc::perspective(60.0f, 1.0f, 0.1f, 100.0f);
    sameDesc.priority = 20;

    lowPriority.addComponent<CameraComponent>(lowDesc);
    highPriority.addComponent<CameraComponent>(highDesc);
    samePriority.addComponent<CameraComponent>(sameDesc);

    const auto resolved = CameraResolver().resolve(scene, snapshotFor(scene));
    assert(resolved.ok());
    assert(resolved.cameras().size() == 3);
    assert(resolved.cameras()[0].objectId == highPriority.id());
    assert(resolved.cameras()[1].objectId == samePriority.id());
    assert(resolved.cameras()[2].objectId == lowPriority.id());
}

void invalid_camera_metadata_reports_deterministic_diagnostics()
{
    {
        Scene scene("invalid_near_far");
        auto& cameraObject = scene.createObject("camera");
        assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());
        auto desc = CameraComponentDesc::perspective(60.0f, 1.0f, 1.0f, 0.5f);
        cameraObject.addComponent<CameraComponent>(desc);
        const auto resolved = CameraResolver().resolve(scene, snapshotFor(scene));
        assert(!resolved.ok());
        assert(resolved.diagnostic().code == CameraDiagnosticCode::InvalidNearFar);
    }

    {
        Scene scene("invalid_viewport");
        auto& cameraObject = scene.createObject("camera");
        assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());
        auto desc = CameraComponentDesc::orthographic(16.0f, 0.1f, 100.0f);
        desc.viewport = ViewportRect::normalized(0.0f, 0.0f, 0.0f, 1.0f);
        cameraObject.addComponent<CameraComponent>(desc);
        const auto resolved = CameraResolver().resolve(scene, snapshotFor(scene));
        assert(!resolved.ok());
        assert(resolved.diagnostic().code == CameraDiagnosticCode::InvalidViewport);
    }

    {
        Scene scene("invalid_aspect");
        auto& cameraObject = scene.createObject("camera");
        assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());
        auto desc = CameraComponentDesc::orthographic(16.0f, 0.1f, 100.0f);
        desc.aspectPolicy = CameraAspectPolicy::fixed(0.0f);
        cameraObject.addComponent<CameraComponent>(desc);
        const auto resolved = CameraResolver().resolve(scene, snapshotFor(scene));
        assert(!resolved.ok());
        assert(resolved.diagnostic().code == CameraDiagnosticCode::InvalidAspectPolicy);
    }

    {
        Scene scene("empty_queue_mask");
        auto& cameraObject = scene.createObject("camera");
        assert(scene.reparent(cameraObject.id(), scene.rootId()).ok());
        auto desc = CameraComponentDesc::orthographic(16.0f, 0.1f, 100.0f);
        desc.visibleQueues = RenderQueueMask::none();
        cameraObject.addComponent<CameraComponent>(desc);
        const auto resolved = CameraResolver().resolve(scene, snapshotFor(scene));
        assert(!resolved.ok());
        assert(resolved.diagnostic().code == CameraDiagnosticCode::EmptyRenderQueueMask);
    }
}

void authored_right_handed_camera_preserves_screen_orientation()
{
    Scene scene("authored_camera");
    auto& object = scene.createObject("camera");
    assert(scene.reparent(object.id(), scene.rootId()).ok());
    auto desc = CameraComponentDesc::perspective(60, 1.5f, 0.1f, 100);
    assert(desc.viewHandedness == CameraViewHandedness::LeftHanded);
    desc.viewHandedness = CameraViewHandedness::RightHanded;
    auto& camera = object.addComponent<CameraComponent>(desc);
    for (auto projection : {CameraProjection::Perspective, CameraProjection::Orthographic}) {
        camera.desc().projection = projection;
        const auto result = CameraResolver().resolve(scene, snapshotFor(scene));
        assert(result.ok());
        const auto& resolved = result.cameras().front();
        assert(resolved.viewHandedness == desc.viewHandedness);
        const auto vp = arp::core::multiply(resolved.projectionMatrix, resolved.viewMatrix);
        const auto pixel = arp::core::transformPoint(vp, {2, 1, -10});
        assert(pixel.x > 0 && pixel.y < 0 && pixel.z > 0 && pixel.z < 1);
        const auto cullPoint =
            arp::core::transformPoint(resolved.depthPositiveViewMatrix, {2, 1, -10});
        assert(cullPoint.x == 2 && cullPoint.y == 1 && cullPoint.z == 10);
        if (projection == CameraProjection::Perspective) {
            const float cullDepth = resolved.depthPositiveProjectionScale
                + resolved.projectionMatrix.m[11] / cullPoint.z;
            assert(std::abs(cullDepth - pixel.z) < 1e-6f);
        }
    }
    camera.desc().viewHandedness = static_cast<CameraViewHandedness>(99);
    const auto invalid = CameraResolver().resolve(scene, snapshotFor(scene));
    assert(!invalid.ok());
    assert(invalid.diagnostic().code == CameraDiagnosticCode::InvalidViewHandedness);
}

} // namespace

int main()
{
    camera_is_an_ordinary_game_object_component();
    resolver_orders_by_priority_then_stable_object_id();
    invalid_camera_metadata_reports_deterministic_diagnostics();
    authored_right_handed_camera_preserves_screen_orientation();
    return 0;
}
