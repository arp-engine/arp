#pragma once

#include "core/math.h"
#include "reflection/reflection_macros.h"
#include "scene/component.h"

#include <limits>

namespace arp::scene {

/// Scene-owned camera facts from the previous frame. GPU TAA textures and
/// history lifetime remain renderer-owned; this component only records the
/// camera state needed to detect projection changes and camera cuts.
ACLASS(Name = "CameraTemporalComponent", Ownership = Engine, Lifecycle = Explicit)

class CameraTemporalComponent final : public Component {
public:
    const char* scriptTypeName() const override { return "CameraTemporalComponent"; }
    AFUNC()

    std::uint64_t component_id() const { return id().value(); }
    AFUNC()

    std::uint64_t owner_id() const { return ownerId().value(); }
    AFUNC()

    bool enabled() const { return Component::enabled(); }
    AFUNC()

    void set_enabled(bool value) { Component::setEnabled(value); }
    AFUNC()

    float previousFovDegrees() const { return previousFovDegrees_; }
    AFUNC()

    float previousAspectRatio() const { return previousAspectRatio_; }
    AFUNC()

    float previousNearPlane() const { return previousNearPlane_; }
    AFUNC()

    float previousFarPlane() const { return previousFarPlane_; }

    AFUNC()

    void set_previous_projection(
        float fovDegrees, float aspectRatio, float nearPlane, float farPlane)
    {
        previousFovDegrees_ = fovDegrees;
        previousAspectRatio_ = aspectRatio;
        previousNearPlane_ = nearPlane;
        previousFarPlane_ = farPlane;
    }

    void setPreviousProjection(float fovDegrees, float aspectRatio, float nearPlane, float farPlane)
    {
        set_previous_projection(fovDegrees, aspectRatio, nearPlane, farPlane);
    }

    const arp::core::Vec3& previousPosition() const { return previousPosition_; }

    const arp::core::Vec3& previousTarget() const { return previousTarget_; }
    AFUNC()

    arp::core::Vec3 previous_position() const { return previousPosition_; }
    AFUNC()

    arp::core::Vec3 previous_target() const { return previousTarget_; }
    AFUNC()

    void setPreviousPose(arp::core::Vec3 position, arp::core::Vec3 target)
    {
        previousPosition_ = position;
        previousTarget_ = target;
    }

    AFUNC()

    bool poseValid() const { return poseValid_; }
    AFUNC()

    void setPoseValid(bool valid) { poseValid_ = valid; }

    APROP(Runtime)
    float previousFovDegrees_ = std::numeric_limits<float>::quiet_NaN();
    APROP(Runtime)
    float previousAspectRatio_ = std::numeric_limits<float>::quiet_NaN();
    APROP(Runtime)
    float previousNearPlane_ = std::numeric_limits<float>::quiet_NaN();
    APROP(Runtime)
    float previousFarPlane_ = std::numeric_limits<float>::quiet_NaN();
    APROP(Runtime)
    arp::core::Vec3 previousPosition_{};
    APROP(Runtime)
    arp::core::Vec3 previousTarget_{};
    APROP(Runtime)
    bool poseValid_ = false;
};

} // namespace arp::scene
