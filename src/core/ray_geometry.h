#pragma once
#include "core/math.h"

#include <algorithm>
#include <cmath>

namespace arp::core {
struct RayTriangleHit {
    float distance = 0.f;
    float u = 0.f;
    float v = 0.f;
};

inline bool intersectRayTriangle(Vec3 origin,
    Vec3 direction,
    Vec3 a,
    Vec3 b,
    Vec3 c,
    float minimumDistance,
    float maximumDistance,
    RayTriangleHit& hit) noexcept
{
    const auto e1 = b - a, e2 = c - a, p = cross(direction, e2);
    const float determinant = dot(e1, p);
    if (std::abs(determinant) < 1e-8f)
        return false;
    const auto t = origin - a;
    const float inv = 1.f / determinant;
    const float u = dot(t, p) * inv;
    if (u < 0.f || u > 1.f)
        return false;
    const auto q = cross(t, e1);
    const float v = dot(direction, q) * inv;
    if (v < 0.f || u + v > 1.f)
        return false;
    const float distance = dot(e2, q) * inv;
    if (distance < minimumDistance || distance > maximumDistance)
        return false;
    hit = {distance, u, v};
    return true;
}

inline bool intersectRayBox(
    Vec3 origin, Vec3 direction, Vec3 minimum, Vec3 maximum, float maximumDistance) noexcept
{
    float enter = 0.f, exit = maximumDistance;
    const float o[] = {origin.x, origin.y, origin.z}, d[] = {direction.x, direction.y, direction.z};
    const float lo[] = {minimum.x, minimum.y, minimum.z}, hi[] = {maximum.x, maximum.y, maximum.z};
    for (unsigned i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-12f) {
            if (o[i] < lo[i] || o[i] > hi[i])
                return false;
        } else {
            const float a = (lo[i] - o[i]) / d[i], b = (hi[i] - o[i]) / d[i];
            enter = std::max(enter, std::min(a, b));
            exit = std::min(exit, std::max(a, b));
        }
        if (exit < enter)
            return false;
    }
    return true;
}
} // namespace arp::core
