#pragma once

#include "core/meta/annotations.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace arp::core {

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct Vec2 {
    APROP(Serialize)
    float x = 0.0f;
    APROP(Serialize)
    float y = 0.0f;
};

inline Vec2 operator+(Vec2 lhs, Vec2 rhs)
{
    return Vec2{lhs.x + rhs.x, lhs.y + rhs.y};
}

inline Vec2 operator-(Vec2 lhs, Vec2 rhs)
{
    return Vec2{lhs.x - rhs.x, lhs.y - rhs.y};
}

inline Vec2 scale(Vec2 value, float amount)
{
    return Vec2{value.x * amount, value.y * amount};
}

inline float dot(Vec2 lhs, Vec2 rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y;
}

inline float length(Vec2 value)
{
    return std::sqrt(dot(value, value));
}

inline Vec2 normalizeOr(Vec2 value, Vec2 fallback)
{
    const auto valueLength = length(value);
    if (valueLength <= 0.000001f) {
        return fallback;
    }
    return scale(value, 1.0f / valueLength);
}

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct Vec3 {
    APROP(Serialize)
    float x = 0.0f;
    APROP(Serialize)
    float y = 0.0f;
    APROP(Serialize)
    float z = 0.0f;
};

inline Vec3 operator+(Vec3 lhs, Vec3 rhs)
{
    return Vec3{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

inline Vec3 operator-(Vec3 lhs, Vec3 rhs)
{
    return Vec3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

inline Vec3 scale(Vec3 value, float amount)
{
    return Vec3{value.x * amount, value.y * amount, value.z * amount};
}

inline float dot(Vec3 lhs, Vec3 rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

inline Vec3 cross(Vec3 lhs, Vec3 rhs)
{
    return Vec3{
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

inline float length(Vec3 value)
{
    return std::sqrt(dot(value, value));
}

inline Vec3 normalizeOr(Vec3 value, Vec3 fallback)
{
    const auto valueLength = length(value);
    if (valueLength <= 0.000001f) {
        return fallback;
    }
    return scale(value, 1.0f / valueLength);
}

inline Vec3 normalize(Vec3 value)
{
    return normalizeOr(value, Vec3{0.0f, 0.0f, 1.0f});
}

/// Component-wise linear interpolation between two vectors.
/// t is clamped to [0,1]: t=0 returns a, t=1 returns b.
inline Vec3 lerp(Vec3 a, Vec3 b, float t)
{
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    return Vec3{
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

/// Euclidean distance between two points.
inline float distance(Vec3 a, Vec3 b)
{
    return length(a - b);
}

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct Vec4 {
    APROP(Serialize)
    float x = 0.0f;
    APROP(Serialize)
    float y = 0.0f;
    APROP(Serialize)
    float z = 0.0f;
    APROP(Serialize)
    float w = 0.0f;
};

inline Vec4 operator+(Vec4 lhs, Vec4 rhs)
{
    return Vec4{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w};
}

inline Vec4 operator-(Vec4 lhs, Vec4 rhs)
{
    return Vec4{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z, lhs.w - rhs.w};
}

inline Vec4 scale(Vec4 value, float amount)
{
    return Vec4{value.x * amount, value.y * amount, value.z * amount, value.w * amount};
}

inline float dot(Vec4 lhs, Vec4 rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
}

inline float length(Vec4 value)
{
    return std::sqrt(dot(value, value));
}

inline Vec4 normalizeOr(Vec4 value, Vec4 fallback)
{
    const auto valueLength = length(value);
    if (valueLength <= 0.000001f) {
        return fallback;
    }
    return scale(value, 1.0f / valueLength);
}

// Forward-declared Quat operators needed by Quat::fromYawPitchRoll below.
struct Quat;
inline float dot(Quat lhs, Quat rhs);
inline Quat normalize(Quat value);
inline Quat operator*(Quat lhs, Quat rhs);
inline Quat conjugate(Quat value);

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct Quat {
    APROP(Serialize)
    float x = 0.0f;
    APROP(Serialize)
    float y = 0.0f;
    APROP(Serialize)
    float z = 0.0f;
    APROP(Serialize)
    float w = 1.0f;

    static Quat identity() { return Quat{}; }

    static Quat fromAxisAngle(Vec3 axis, float radians)
    {
        const auto normalizedAxis = normalizeOr(axis, Vec3{0.0f, 0.0f, 1.0f});
        const auto halfAngle = radians * 0.5f;
        const auto s = std::sin(halfAngle);
        return Quat{
            normalizedAxis.x * s,
            normalizedAxis.y * s,
            normalizedAxis.z * s,
            std::cos(halfAngle),
        };
    }

    /// Build an orientation from intrinsic Tait-Bryan angles (yaw-pitch-roll,
    /// applied in that order about the world Y, X, Z axes). Angles are in
    /// degrees to match the editor-facing Transform convention. Roll is
    /// typically 0 for cameras. The rotation is composed as
    ///   q = yaw * pitch * roll
    /// (Hamilton product), so a forward vector {0,0,-1} is first pitched about
    /// X, then yawed about Y -- matching the CameraController's yaw/pitch
    /// orbit semantics and Mat4::lookAt's right-handed view convention.
    static Quat fromYawPitchRoll(float yawDegrees, float pitchDegrees, float rollDegrees)
    {
        const float yawRad = yawDegrees * (3.14159265358979f / 180.0f);
        const float pitchRad = pitchDegrees * (3.14159265358979f / 180.0f);
        const float rollRad = rollDegrees * (3.14159265358979f / 180.0f);
        const Quat yaw = fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, yawRad);
        const Quat pitch = fromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, pitchRad);
        const Quat roll = fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, rollRad);
        return normalize(yaw * pitch * roll);
    }
};

inline float dot(Quat lhs, Quat rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
}

inline Quat normalize(Quat value)
{
    const auto valueLength = std::sqrt(dot(value, value));
    if (valueLength <= 0.000001f) {
        return Quat::identity();
    }
    return Quat{
        value.x / valueLength, value.y / valueLength, value.z / valueLength, value.w / valueLength};
}

/// Named alias for normalize() — same semantics, discoverable by callers
/// that prefer function-style over the overloaded name.
inline Quat quatNormalize(const Quat& q)
{
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len < 1e-8f)
        return Quat::identity();
    const float inv = 1.0f / len;
    return Quat{q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

inline Quat conjugate(Quat value)
{
    return Quat{-value.x, -value.y, -value.z, value.w};
}

inline Quat operator*(Quat lhs, Quat rhs)
{
    return Quat{
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z,
    };
}

/// Named alias for operator* — Hamilton product, same as quatMul(a,b) = a * b.
inline Quat quatMul(const Quat& a, const Quat& b)
{
    return a * b;
}

/// Axis-angle → quaternion from individual axis components.
/// Equivalent to Quat::fromAxisAngle({ax,ay,az}, angle) but avoids
/// constructing an intermediate Vec3.
inline Quat quatFromAxisAngle(float ax, float ay, float az, float angle)
{
    const float len = std::sqrt(ax * ax + ay * ay + az * az);
    if (len < 1e-6f)
        return Quat::identity();
    const float s = std::sin(angle * 0.5f) / len;
    return Quat{ax * s, ay * s, az * s, std::cos(angle * 0.5f)};
}

inline Vec3 rotate(Quat rotation, Vec3 value)
{
    const auto normalizedRotation = normalize(rotation);
    const auto rotated =
        normalizedRotation * Quat{value.x, value.y, value.z, 0.0f} * conjugate(normalizedRotation);
    return Vec3{rotated.x, rotated.y, rotated.z};
}

/// Decompose q = yaw(Y) * pitch(X) * roll(Z) into radians for instance ABIs
/// that store the three angles separately. Invalid quaternions fail rather
/// than being normalized to identity.
inline bool quaternionToYawPitchRollRadians(
    Quat value, float& yaw, float& pitch, float& roll) noexcept
{
    const float lengthSquared = dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0f) {
        return false;
    }
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    value.x *= inverseLength;
    value.y *= inverseLength;
    value.z *= inverseLength;
    value.w *= inverseLength;
    const float sinPitch = std::clamp(2.0f * (value.w * value.x - value.y * value.z), -1.0f, 1.0f);
    pitch = std::asin(sinPitch);
    if (std::fabs(sinPitch) >= 1.0f) {
        yaw = std::atan2(2.0f * (value.w * value.y - value.x * value.z),
            1.0f - 2.0f * (value.y * value.y + value.z * value.z));
        roll = 0.0f;
    } else {
        yaw = std::atan2(2.0f * (value.w * value.y + value.x * value.z),
            1.0f - 2.0f * (value.x * value.x + value.y * value.y));
        roll = std::atan2(2.0f * (value.w * value.z + value.x * value.y),
            1.0f - 2.0f * (value.x * value.x + value.z * value.z));
    }
    return std::isfinite(yaw) && std::isfinite(pitch) && std::isfinite(roll);
}

/// Spherical linear interpolation between two quaternions.
/// Takes the shortest path: if a.b < 0, b is negated before interpolating.
/// t is clamped to [0,1]: t=0 returns a, t=1 returns b.
/// For near-parallel inputs (|a.b| > 0.9995) falls back to nlerp to avoid
/// the degenerate sin(theta)~=0 division.
inline Quat quatSlerp(Quat a, Quat b, float t)
{
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    float d = dot(a, b);
    if (d < 0.0f) {
        b = Quat{-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    // Near-parallel: nlerp is accurate enough and avoids /sin(theta).
    if (d > 0.9995f) {
        return normalize(Quat{
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t,
        });
    }
    const float theta = std::acos(d);
    const float sinTheta = std::sin(theta);
    const float wA = std::sin((1.0f - t) * theta) / sinTheta;
    const float wB = std::sin(t * theta) / sinTheta;
    return Quat{
        a.x * wA + b.x * wB,
        a.y * wA + b.y * wB,
        a.z * wA + b.z * wB,
        a.w * wA + b.w * wB,
    };
}

/// Shortest rotation that maps direction `from` onto direction `to`.
/// Both vectors are normalized internally. Handles the parallel case
/// (returns identity) and the anti-parallel case (returns a 180-degree
/// rotation about any axis orthogonal to `from`, preferring the smallest
/// component of `from` to build a stable orthogonal).
inline Quat quatFromToRotation(Vec3 from, Vec3 to)
{
    const Vec3 a = normalizeOr(from, Vec3{0.0f, 0.0f, 1.0f});
    const Vec3 b = normalizeOr(to, Vec3{0.0f, 0.0f, 1.0f});
    const float d = dot(a, b);
    if (d > 0.999999f) {
        return Quat::identity();
    }
    if (d < -0.999999f) {
        // Anti-parallel: pick an axis orthogonal to `a`.
        Vec3 axis = cross(a, Vec3{1.0f, 0.0f, 0.0f});
        if (dot(axis, axis) < 1e-8f) {
            axis = cross(a, Vec3{0.0f, 1.0f, 0.0f});
        }
        axis = normalizeOr(axis, Vec3{0.0f, 0.0f, 1.0f});
        return Quat::fromAxisAngle(axis, 3.14159265358979f);
    }
    // General case: axis = cross(a,b), angle = acos(d). Build directly from
    // half-vector to avoid the acos/sin pair:
    //   q = normalize( (cross(a,b), 1 + dot(a,b)) )
    Vec3 axis = cross(a, b);
    Quat q{axis.x, axis.y, axis.z, 1.0f + d};
    return normalize(q);
}

/// Build a rotation whose local +Z (or a caller-chosen forward) maps to the
/// desired forward direction, with roll stabilized by `up`.
/// Returns identity when `forward` is parallel to `up` (gimbal lock guard).
/// Note: unlike Mat4::lookAt (which builds a view matrix with -Z forward),
/// this returns a rotation that orients an object's local forward axis
/// toward `forward`. Used by Look-at IK.
inline Quat quatLookAt(Vec3 forward, Vec3 up = Vec3{0.0f, 1.0f, 0.0f})
{
    const Vec3 f = normalizeOr(forward, Vec3{0.0f, 0.0f, 1.0f});
    Vec3 u = normalizeOr(up, Vec3{0.0f, 1.0f, 0.0f});
    // If forward is parallel to up, pick an alternate up.
    const float fd = std::fabs(dot(f, u));
    if (fd > 0.9999f) {
        u = (std::fabs(f.x) < 0.9f) ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 0.0f, 1.0f};
        u = normalizeOr(u, Vec3{0.0f, 1.0f, 0.0f});
    }
    const Vec3 right = normalize(cross(u, f));
    const Vec3 realUp = cross(f, right);
    // Build rotation matrix (row-major in core::Mat4 convention) whose
    // columns are right, up, forward, then convert to quaternion.
    // core::Mat4 is row-major: m[row*4+col]. We set the 3x3 basis.
    Quat q;
    const float r0 = right.x, r1 = right.y, r2 = right.z;
    const float u0 = realUp.x, u1 = realUp.y, u2 = realUp.z;
    const float f0 = f.x, f1 = f.y, f2 = f.z;
    const float trace = r0 + u1 + f2;
    if (trace > 0.0f) {
        const float s = 0.5f / std::sqrt(trace + 1.0f);
        q.w = 0.25f / s;
        q.x = (u2 - f1) * s;
        q.y = (f0 - r2) * s;
        q.z = (r1 - u0) * s;
    } else if (r0 > u1 && r0 > f2) {
        const float s = 2.0f * std::sqrt(1.0f + r0 - u1 - f2);
        q.w = (u2 - f1) / s;
        q.x = 0.25f * s;
        q.y = (u0 + r1) / s;
        q.z = (f0 + r2) / s;
    } else if (u1 > f2) {
        const float s = 2.0f * std::sqrt(1.0f + u1 - r0 - f2);
        q.w = (f0 - r2) / s;
        q.x = (u0 + r1) / s;
        q.y = 0.25f * s;
        q.z = (f1 + u2) / s;
    } else {
        const float s = 2.0f * std::sqrt(1.0f + f2 - r0 - u1);
        q.w = (r1 - u0) / s;
        q.x = (f0 + r2) / s;
        q.y = (f1 + u2) / s;
        q.z = 0.25f * s;
    }
    return normalize(q);
}

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct Mat4 {
    APROP(Serialize)
    std::array<float, 16> m{};

    static Mat4 identity()
    {
        Mat4 matrix;
        matrix.m[0] = 1.0f;
        matrix.m[5] = 1.0f;
        matrix.m[10] = 1.0f;
        matrix.m[15] = 1.0f;
        return matrix;
    }

    static Mat4 translation(Vec3 offset)
    {
        Mat4 matrix = identity();
        matrix.m[3] = offset.x;
        matrix.m[7] = offset.y;
        matrix.m[11] = offset.z;
        return matrix;
    }

    static Mat4 scale(Vec3 amount)
    {
        Mat4 matrix = identity();
        matrix.m[0] = amount.x;
        matrix.m[5] = amount.y;
        matrix.m[10] = amount.z;
        return matrix;
    }

    static Mat4 rotation(Quat value)
    {
        const auto q = normalize(value);
        const auto xx = q.x * q.x;
        const auto yy = q.y * q.y;
        const auto zz = q.z * q.z;
        const auto xy = q.x * q.y;
        const auto xz = q.x * q.z;
        const auto yz = q.y * q.z;
        const auto xw = q.x * q.w;
        const auto yw = q.y * q.w;
        const auto zw = q.z * q.w;

        Mat4 matrix = identity();
        matrix.m[0] = 1.0f - 2.0f * (yy + zz);
        matrix.m[1] = 2.0f * (xy - zw);
        matrix.m[2] = 2.0f * (xz + yw);
        matrix.m[4] = 2.0f * (xy + zw);
        matrix.m[5] = 1.0f - 2.0f * (xx + zz);
        matrix.m[6] = 2.0f * (yz - xw);
        matrix.m[8] = 2.0f * (xz - yw);
        matrix.m[9] = 2.0f * (yz + xw);
        matrix.m[10] = 1.0f - 2.0f * (xx + yy);
        return matrix;
    }
};

inline Mat4 multiply(const Mat4& lhs, const Mat4& rhs)
{
    Mat4 result;
    for (uint32_t row = 0; row < 4; ++row) {
        for (uint32_t column = 0; column < 4; ++column) {
            float value = 0.0f;
            for (uint32_t k = 0; k < 4; ++k) {
                value += lhs.m[row * 4 + k] * rhs.m[k * 4 + column];
            }
            result.m[row * 4 + column] = value;
        }
    }
    return result;
}

inline Mat4 lookAt(Vec3 position, Vec3 focus, Vec3 up = Vec3{0.0f, 1.0f, 0.0f})
{
    const auto forward = normalize(focus - position);
    const auto right = normalize(cross(up, forward));
    const auto realUp = cross(forward, right);

    Mat4 view = Mat4::identity();
    view.m[0] = right.x;
    view.m[1] = right.y;
    view.m[2] = right.z;
    view.m[3] = -dot(right, position);
    view.m[4] = realUp.x;
    view.m[5] = realUp.y;
    view.m[6] = realUp.z;
    view.m[7] = -dot(realUp, position);
    view.m[8] = forward.x;
    view.m[9] = forward.y;
    view.m[10] = forward.z;
    view.m[11] = -dot(forward, position);
    return view;
}

inline Mat4 perspective(
    float verticalFovDegrees, float aspectRatio, float nearPlane, float farPlane)
{
    const auto fovRadians = verticalFovDegrees * 3.1415926535f / 180.0f;
    const auto yScale = 1.0f / std::tan(fovRadians * 0.5f);
    const auto xScale = aspectRatio > 0.0f ? yScale / aspectRatio : yScale;
    const auto depthRange = std::max(farPlane - nearPlane, 0.0001f);

    Mat4 projection;
    projection.m[0] = xScale;
    projection.m[5] = -yScale;
    projection.m[10] = farPlane / depthRange;
    projection.m[11] = -(nearPlane * farPlane) / depthRange;
    projection.m[14] = 1.0f;
    return projection;
}

/// Build a symmetric orthographic projection matrix (row-major storage,
/// left-handed, z mapped to [0,1] — matching the perspective() convention
/// so the two can share the same lookAt() view matrix and clip-space
/// semantics).
///
/// @param halfWidth   Half the horizontal extent (left = -halfWidth, right = +halfWidth).
/// @param halfHeight  Half the vertical extent (bottom = -halfHeight, top = +halfHeight).
/// @param nearPlane   Near plane distance (positive, in view space).
/// @param farPlane    Far plane distance (positive, in view space).
inline Mat4 orthographic(float halfWidth, float halfHeight, float nearPlane, float farPlane)
{
    const auto safeHalfWidth = std::max(halfWidth, 0.0001f);
    const auto safeHalfHeight = std::max(halfHeight, 0.0001f);
    const auto depthRange = std::max(farPlane - nearPlane, 0.0001f);

    // Row-major storage: m[row * 4 + column].
    // Left-handed: view-space +z maps to [0,1] via z_clip = z_view/(f-n) - n/(f-n).
    // Y-flip (m[5] negative): Vulkan rasterizes with a Y-down viewport, and the
    // shadow caster writes depth in that same Y-down orientation. The receiver
    // path samples the shadow map with v=0 at top, v=1 at bottom (matching the
    // caster). Without this flip the light frustum's top/bottom planes are
    // swapped, which causes trees above the cascade center to be culled out and
    // cast no shadow. This mirrors the Y-flip already present in perspective().
    Mat4 projection;
    projection.m[0] = 1.0f / safeHalfWidth;     // row 0, col 0 - x scale
    projection.m[5] = -1.0f / safeHalfHeight;   // row 1, col 1 - y scale (Vulkan Y-flip)
    projection.m[10] = 1.0f / depthRange;       // row 2, col 2 - z scale
    projection.m[11] = -nearPlane / depthRange; // row 2, col 3 - z translation
    projection.m[15] = 1.0f;                    // row 3, col 3 - w unchanged
    return projection;
}

// Right-handed camera variants retain the same row-major, Y-down, [0,1]
// clip-space contract. Camera-local forward is -Z; legacy helpers use +Z.
inline Mat4 lookAtRightHanded(Vec3 position, Vec3 focus, Vec3 up = Vec3{0.0f, 1.0f, 0.0f})
{
    return lookAt(position, position + (position - focus), up);
}

inline Mat4 perspectiveRightHanded(
    float verticalFovDegrees, float aspectRatio, float nearPlane, float farPlane)
{
    auto result = perspective(verticalFovDegrees, aspectRatio, nearPlane, farPlane);
    result.m[10] = -result.m[10];
    result.m[14] = -1.0f;
    return result;
}

inline Mat4 orthographicRightHanded(
    float halfWidth, float halfHeight, float nearPlane, float farPlane)
{
    auto result = orthographic(halfWidth, halfHeight, nearPlane, farPlane);
    result.m[10] = -result.m[10];
    return result;
}

inline Vec4 transformHomogeneous(const Mat4& matrix, Vec4 value)
{
    return Vec4{
        matrix.m[0] * value.x + matrix.m[1] * value.y + matrix.m[2] * value.z
            + matrix.m[3] * value.w,
        matrix.m[4] * value.x + matrix.m[5] * value.y + matrix.m[6] * value.z
            + matrix.m[7] * value.w,
        matrix.m[8] * value.x + matrix.m[9] * value.y + matrix.m[10] * value.z
            + matrix.m[11] * value.w,
        matrix.m[12] * value.x + matrix.m[13] * value.y + matrix.m[14] * value.z
            + matrix.m[15] * value.w,
    };
}

inline Vec3 transformPoint(const Mat4& matrix, Vec3 point)
{
    const auto x =
        matrix.m[0] * point.x + matrix.m[1] * point.y + matrix.m[2] * point.z + matrix.m[3];
    const auto y =
        matrix.m[4] * point.x + matrix.m[5] * point.y + matrix.m[6] * point.z + matrix.m[7];
    const auto z =
        matrix.m[8] * point.x + matrix.m[9] * point.y + matrix.m[10] * point.z + matrix.m[11];
    const auto w =
        matrix.m[12] * point.x + matrix.m[13] * point.y + matrix.m[14] * point.z + matrix.m[15];
    const auto invW = std::fabs(w) > 0.000001f ? 1.0f / w : 1.0f;
    return Vec3{x * invW, y * invW, z * invW};
}

inline Vec3 transformVector(const Mat4& matrix, Vec3 vector)
{
    return Vec3{
        matrix.m[0] * vector.x + matrix.m[1] * vector.y + matrix.m[2] * vector.z,
        matrix.m[4] * vector.x + matrix.m[5] * vector.y + matrix.m[6] * vector.z,
        matrix.m[8] * vector.x + matrix.m[9] * vector.y + matrix.m[10] * vector.z,
    };
}

// Stable script-facing math entry points.  The implementation remains inline
// and allocation-free; the markers make these functions part of the generated
// arp_core reflection contract instead of requiring a hand-written plugin
// export.
AFUNC()

inline float vec3_dot(Vec3 lhs, Vec3 rhs)
{
    return dot(lhs, rhs);
}

AFUNC()

inline float vec3_length(Vec3 value)
{
    return length(value);
}

AFUNC()

inline Vec3 vec3_normalize(Vec3 value)
{
    return normalize(value);
}

AFUNC()

inline Vec3 vec3_lerp(Vec3 a, Vec3 b, float t)
{
    return lerp(a, b, t);
}

AFUNC()

inline float scalar_sin(float value)
{
    return std::sin(value);
}

AFUNC()

inline float scalar_cos(float value)
{
    return std::cos(value);
}

AFUNC()

inline Quat quat_mul(const Quat& a, const Quat& b)
{
    return a * b;
}

AFUNC()

inline Quat quat_slerp(Quat a, Quat b, float t)
{
    return arp::core::quatSlerp(a, b, t);
}

AFUNC()

inline Mat4 mat4_identity()
{
    return Mat4::identity();
}

AFUNC()

inline Mat4 mat4_multiply(const Mat4& lhs, const Mat4& rhs)
{
    return multiply(lhs, rhs);
}

AFUNC()

inline Vec3 mat4_transform_point(const Mat4& matrix, Vec3 point)
{
    return transformPoint(matrix, point);
}

// General 4x4 matrix inverse via Gauss-Jordan elimination with partial pivoting.
// Input and output are both row-major Mat4 (m[row*4+col], v_clip = v_local . M).
// Used for DDGI resolve: reconstructing world position from clip space requires
// the inverse of the row-major view-projection produced by core::multiply(proj, view).
// Returns identity if the matrix is singular (determinant ~0) so callers never
// observe NaN/Inf from a degenerate camera frustum.
inline Mat4 inverse(const Mat4& matrix)
{
    // Augmented [A | I] in row-major layout. Left half starts as the input,
    // right half starts as identity. Gauss-Jordan reduces the left to identity,
    // which transforms the right into A^-1.
    std::array<float, 32> aug{};
    for (std::uint32_t row = 0; row < 4; ++row) {
        for (std::uint32_t col = 0; col < 4; ++col) {
            aug[row * 8 + col] = matrix.m[row * 4 + col];
        }
        aug[row * 8 + 4 + row] = 1.0f;
    }

    for (std::uint32_t col = 0; col < 4; ++col) {
        // Partial pivot: find the row with the largest absolute value in this column
        // at or below the diagonal, to maximize numerical stability.
        std::uint32_t pivotRow = col;
        float pivotAbs = std::fabs(aug[col * 8 + col]);
        for (std::uint32_t row = col + 1; row < 4; ++row) {
            const float candidate = std::fabs(aug[row * 8 + col]);
            if (candidate > pivotAbs) {
                pivotAbs = candidate;
                pivotRow = row;
            }
        }

        // Singular matrix: return identity rather than dividing by ~0.
        if (pivotAbs < 1e-12f) {
            return Mat4::identity();
        }

        // Swap the pivot row into position.
        if (pivotRow != col) {
            for (std::uint32_t k = 0; k < 8; ++k) {
                std::swap(aug[col * 8 + k], aug[pivotRow * 8 + k]);
            }
        }

        // Scale the pivot row so the pivot element becomes 1.
        const float invPivot = 1.0f / aug[col * 8 + col];
        for (std::uint32_t k = 0; k < 8; ++k) {
            aug[col * 8 + k] *= invPivot;
        }

        // Eliminate all other rows (forward + backward) so the column becomes
        // a unit vector with a single 1 on the diagonal.
        for (std::uint32_t row = 0; row < 4; ++row) {
            if (row == col) {
                continue;
            }
            const float factor = aug[row * 8 + col];
            if (std::fabs(factor) < 1e-12f) {
                continue;
            }
            for (std::uint32_t k = 0; k < 8; ++k) {
                aug[row * 8 + k] -= factor * aug[col * 8 + k];
            }
        }
    }

    Mat4 result;
    for (std::uint32_t row = 0; row < 4; ++row) {
        for (std::uint32_t col = 0; col < 4; ++col) {
            result.m[row * 4 + col] = aug[row * 8 + 4 + col];
        }
    }
    return result;
}

} // namespace arp::core
