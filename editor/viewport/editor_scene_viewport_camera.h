#pragma once

#include "core/math/math.h"

namespace arp::editor {

class EditorSceneViewportCamera {
public:
    void orbit(float deltaX, float deltaY) noexcept;
    void orbitAround(float deltaX, float deltaY, core::Vec3 pivot) noexcept;
    void look(float deltaX, float deltaY) noexcept;
    void pan(float deltaX, float deltaY, float viewportHeight) noexcept;
    void move(float horizontal, float forward) noexcept;
    void zoom(float wheelDelta) noexcept;
    core::Vec3 position() const noexcept;
    core::Mat4 viewMatrix() const noexcept;
    core::Mat4 projectionMatrix(float aspectRatio) const noexcept;
    core::Mat4 viewProjectionMatrix(float aspectRatio) const noexcept;
    bool project(core::Vec3 worldPosition,
        float viewportWidth,
        float viewportHeight,
        float& screenX,
        float& screenY,
        float* depth = nullptr) const noexcept;

    float yawDegrees() const noexcept { return yawDegrees_; }

    float pitchDegrees() const noexcept { return pitchDegrees_; }

    float distance() const noexcept { return distance_; }

    core::Vec3 target() const noexcept { return target_; }

private:
    core::Vec3 eye() const noexcept;
    core::Vec3 target_{};
    float yawDegrees_ = -35.0f;
    float pitchDegrees_ = -25.0f;
    float distance_ = 6.0f;
};

} // namespace arp::editor
