#pragma once

#include "scene/camera.h"
#include "scene/component_factory.h"

namespace arp::scene {

// Complete authored camera value. The derived core::Camera cache is excluded;
// queue membership and RGBA are explicit rather than private/array reflection.
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct CameraPersistenceState {
    APROP(Serialize)
    CameraProjection projection = CameraProjection::Perspective;
    APROP(Serialize)
    CameraViewHandedness viewHandedness = CameraViewHandedness::LeftHanded;
    APROP(Serialize)
    float verticalFovDegrees = 60.0f;
    APROP(Serialize)
    float orthographicHeight = 16.0f;
    APROP(Serialize)
    float nearPlane = 0.1f;
    APROP(Serialize)
    float farPlane = 1000.0f;
    APROP(Serialize)
    CameraAspectPolicy aspectPolicy;
    APROP(Serialize)
    ViewportRect viewport;
    APROP(Serialize)
    bool visible2D = false;
    APROP(Serialize)
    bool visible3D = true;
    APROP(Serialize)
    int priority = 0;
    APROP(Serialize)
    CameraLoadOp colorLoadOp = CameraLoadOp::Load;
    APROP(Serialize)
    core::Vec4 clearColor{0, 0, 0, 1};

    static CameraPersistenceState from(const CameraComponentDesc&);
    CameraComponentDesc descriptor() const;
    bool valid() const;
};

ARP_API bool registerScenePersistence(SceneComponentFactory&);

} // namespace arp::scene
