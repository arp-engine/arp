#include "tools/cluster_previewer/cluster_preview_camera.h"

#include <algorithm>
#include <cmath>

namespace arp::tools::cluster_previewer {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float radians(float degrees)
{
    return degrees * kPi / 180.0f;
}

CameraVec3 operator+(CameraVec3 lhs, CameraVec3 rhs)
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

CameraVec3 operator*(CameraVec3 value, float scale)
{
    return {value.x * scale, value.y * scale, value.z * scale};
}

CameraVec3 normalize(CameraVec3 value)
{
    const auto length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 0.00001f) {
        return {1.0f, 0.0f, 0.0f};
    }
    return {value.x / length, value.y / length, value.z / length};
}

CameraVec3 cross(CameraVec3 lhs, CameraVec3 rhs)
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

CameraVec3 forwardVector(float yawDegrees, float pitchDegrees)
{
    const auto yaw = radians(yawDegrees);
    const auto pitch = radians(pitchDegrees);
    const auto cp = std::cos(pitch);
    return normalize({
        std::cos(yaw) * cp,
        std::sin(pitch),
        std::sin(yaw) * cp,
    });
}

} // namespace

void OrbitPreviewCamera::frameBounds(const PreviewBounds& bounds)
{
    if (bounds.valid) {
        orbitCenter_ = {bounds.center[0], bounds.center[1], bounds.center[2]};
        target_ = orbitCenter_;
        distance_ = std::max(bounds.radius * 2.6f, 1.0f);
        minDistance_ = std::max(bounds.radius * 0.05f, 0.05f);
        maxDistance_ = std::max(bounds.radius * 40.0f, 10.0f);
    }
}

void OrbitPreviewCamera::orbit(float deltaX, float deltaY)
{
    target_ = orbitCenter_;
    yawDegrees_ += deltaX * 0.25f;
    pitchDegrees_ = std::clamp(pitchDegrees_ - deltaY * 0.25f, -88.0f, 88.0f);
}

void OrbitPreviewCamera::zoom(float wheelDelta)
{
    const auto factor = wheelDelta > 0.0f ? 0.86f : 1.16f;
    distance_ = std::clamp(distance_ * factor, minDistance_, maxDistance_);
}

void OrbitPreviewCamera::pan(float deltaX, float deltaY, int viewportWidth, int viewportHeight)
{
    const auto viewport = static_cast<float>(std::max(1, std::min(viewportWidth, viewportHeight)));
    const auto panScale = distance_ / viewport;
    const auto forward = forwardVector(yawDegrees_, pitchDegrees_);
    const auto right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    const auto up = normalize(cross(right, forward));
    target_ = target_ + right * (-deltaX * panScale) + up * (deltaY * panScale);
}

float OrbitPreviewCamera::yawDegrees() const
{
    return yawDegrees_;
}

float OrbitPreviewCamera::pitchDegrees() const
{
    return pitchDegrees_;
}

float OrbitPreviewCamera::distance() const
{
    return distance_;
}

CameraVec3 OrbitPreviewCamera::target() const
{
    return target_;
}

CameraVec3 OrbitPreviewCamera::eye() const
{
    const auto forward = forwardVector(yawDegrees_, pitchDegrees_);
    return target_ + forward * (-distance_);
}

} // namespace arp::tools::cluster_previewer
