#include "editor/viewport/editor_scene_viewport_camera.h"

#include <algorithm>
#include <cmath>

namespace arp::editor {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kVerticalFovDegrees = 60.0f;
constexpr float kNearPlane = 0.05f;
constexpr float kFarPlane = 10000.0f;
constexpr float kMaxNavigationDistance = 5000.0f;
constexpr float kFrameViewportCoverage = 0.9f;

float radians(float degrees) noexcept
{
    return degrees * (kPi / 180.0f);
}
} // namespace

void EditorSceneViewportCamera::orbit(float deltaX, float deltaY) noexcept
{
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY))
        return;
    yawDegrees_ = std::remainder(yawDegrees_ + deltaX * 0.25f, 360.0f);
    pitchDegrees_ = std::clamp(pitchDegrees_ - deltaY * 0.25f, -88.0f, 88.0f);
}

void EditorSceneViewportCamera::orbitAround(float deltaX, float deltaY, core::Vec3 pivot) noexcept
{
    if (!std::isfinite(pivot.x) || !std::isfinite(pivot.y) || !std::isfinite(pivot.z))
        return;
    target_ = pivot;
    orbit(deltaX, deltaY);
}

void EditorSceneViewportCamera::look(float deltaX, float deltaY) noexcept
{
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY))
        return;
    const auto oldEye = eye();
    yawDegrees_ = std::remainder(yawDegrees_ + deltaX * 0.25f, 360.0f);
    pitchDegrees_ = std::clamp(pitchDegrees_ - deltaY * 0.25f, -88.0f, 88.0f);
    target_ = oldEye
        + core::scale(core::normalize(core::Vec3{
                          std::cos(radians(yawDegrees_)) * std::cos(radians(pitchDegrees_)),
                          std::sin(radians(pitchDegrees_)),
                          std::sin(radians(yawDegrees_)) * std::cos(radians(pitchDegrees_))}),
            distance_);
}

void EditorSceneViewportCamera::pan(float deltaX, float deltaY, float viewportHeight) noexcept
{
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY) || !std::isfinite(viewportHeight)
        || viewportHeight <= 0.0f)
        return;
    const auto yaw = radians(yawDegrees_);
    const auto pitch = radians(pitchDegrees_);
    const core::Vec3 forward = core::normalize(core::Vec3{
        std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)});
    const core::Vec3 right = core::normalize(core::cross(core::Vec3{0.0f, 1.0f, 0.0f}, forward));
    const core::Vec3 up = core::normalize(core::cross(forward, right));
    const float worldPerPixel =
        2.0f * distance_ * std::tan(radians(kVerticalFovDegrees * 0.5f)) / viewportHeight;
    target_ = target_ + core::scale(right, -deltaX * worldPerPixel)
        + core::scale(up, deltaY * worldPerPixel);
}

void EditorSceneViewportCamera::move(float horizontal, float forwardAmount) noexcept
{
    if (!std::isfinite(horizontal) || !std::isfinite(forwardAmount))
        return;
    const auto yaw = radians(yawDegrees_);
    const auto pitch = radians(pitchDegrees_);
    const auto forward = core::normalize(core::Vec3{
        std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)});
    const auto right = core::normalize(core::cross(core::Vec3{0.0f, 1.0f, 0.0f}, forward));
    const auto amount = std::max(distance_ * 0.08f, 0.01f);
    const auto translation =
        core::scale(right, horizontal * amount) + core::scale(forward, forwardAmount * amount);
    target_ = target_ + translation;
}

void EditorSceneViewportCamera::zoom(float wheelDelta) noexcept
{
    if (!std::isfinite(wheelDelta))
        return;
    distance_ =
        std::clamp(distance_ * std::pow(0.86f, wheelDelta), kNearPlane, kMaxNavigationDistance);
}

bool EditorSceneViewportCamera::frameBounds(
    core::Vec3 minimum, core::Vec3 maximum, float aspectRatio) noexcept
{
    if (!std::isfinite(minimum.x) || !std::isfinite(minimum.y) || !std::isfinite(minimum.z)
        || !std::isfinite(maximum.x) || !std::isfinite(maximum.y) || !std::isfinite(maximum.z)
        || maximum.x < minimum.x || maximum.y < minimum.y || maximum.z < minimum.z
        || !std::isfinite(aspectRatio) || aspectRatio <= 0)
        return false;
    const double dx = double(maximum.x) - minimum.x;
    const double dy = double(maximum.y) - minimum.y;
    const double dz = double(maximum.z) - minimum.z;
    const double radius = std::hypot(dx, dy, dz) * 0.5;
    const double halfAngle = std::atan(std::tan(radians(kVerticalFovDegrees * 0.5f))
        * std::min(1.0, double(aspectRatio)) * kFrameViewportCoverage);
    const double distance = radius / std::sin(halfAngle);
    if (radius <= 0 || !std::isfinite(distance) || distance > kMaxNavigationDistance
        || distance - radius < kNearPlane || distance + radius >= kFarPlane)
        return false;
    target_ = {float(double(minimum.x) + dx * 0.5),
        float(double(minimum.y) + dy * 0.5),
        float(double(minimum.z) + dz * 0.5)};
    distance_ = float(distance);
    return true;
}

core::Vec3 EditorSceneViewportCamera::eye() const noexcept
{
    const auto yaw = radians(yawDegrees_);
    const auto pitch = radians(pitchDegrees_);
    const core::Vec3 forward = core::normalize(core::Vec3{
        std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)});
    return target_ - core::scale(forward, distance_);
}

core::Vec3 EditorSceneViewportCamera::position() const noexcept
{
    return eye();
}

core::Mat4 EditorSceneViewportCamera::viewMatrix() const noexcept
{
    return core::lookAt(eye(), target_);
}

core::Mat4 EditorSceneViewportCamera::projectionMatrix(float aspectRatio) const noexcept
{
    return core::perspective(kVerticalFovDegrees, aspectRatio, kNearPlane, kFarPlane);
}

core::Mat4 EditorSceneViewportCamera::viewProjectionMatrix(float aspectRatio) const noexcept
{
    return core::multiply(projectionMatrix(aspectRatio), viewMatrix());
}

bool EditorSceneViewportCamera::project(core::Vec3 worldPosition,
    float viewportWidth,
    float viewportHeight,
    float& screenX,
    float& screenY,
    float* depth) const noexcept
{
    if (!std::isfinite(worldPosition.x) || !std::isfinite(worldPosition.y)
        || !std::isfinite(worldPosition.z) || !std::isfinite(viewportWidth)
        || !std::isfinite(viewportHeight) || viewportWidth <= 0.0f || viewportHeight <= 0.0f)
        return false;
    const auto view = viewMatrix();
    const auto projection = projectionMatrix(viewportWidth / viewportHeight);
    const auto ndc = core::transformPoint(core::multiply(projection, view), worldPosition);
    const auto viewPosition = core::transformPoint(view, worldPosition);
    if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y) || !std::isfinite(ndc.z)
        || !std::isfinite(viewPosition.z) || viewPosition.z <= 0.0f)
        return false;
    screenX = (ndc.x * 0.5f + 0.5f) * viewportWidth;
    screenY = (ndc.y * 0.5f + 0.5f) * viewportHeight;
    if (depth)
        *depth = viewPosition.z;
    return ndc.x >= -1.0f && ndc.x <= 1.0f && ndc.y >= -1.0f && ndc.y <= 1.0f && ndc.z >= 0.0f
        && ndc.z <= 1.0f;
}

} // namespace arp::editor
