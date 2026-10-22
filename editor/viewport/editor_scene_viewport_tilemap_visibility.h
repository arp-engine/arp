#pragma once

#include "core/math/geometry.h"
#include "core/math/math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace arp::editor {

enum class EditorSceneViewportTileMapVisibilityDiagnostic {
    None,
    InvalidInput,
    FrustumDoesNotBoundPlane,
    InvalidIntersection,
};

struct EditorSceneViewportTileMapVisibilityResult {
    core::Rect2<float> worldBounds{};
    EditorSceneViewportTileMapVisibilityDiagnostic diagnostic =
        EditorSceneViewportTileMapVisibilityDiagnostic::InvalidInput;

    bool valid() const noexcept
    {
        return diagnostic == EditorSceneViewportTileMapVisibilityDiagnostic::None;
    }
};

inline EditorSceneViewportTileMapVisibilityResult editorSceneViewportTileMapVisibleWorld(
    const core::Mat4& viewProjection,
    std::uint32_t width,
    std::uint32_t height,
    float planeZ) noexcept
{
    if (width == 0u || height == 0u || !std::isfinite(planeZ))
        return {};

    constexpr std::array<core::Vec2, 4> corners{{
        {-1.0f, -1.0f},
        {1.0f, -1.0f},
        {-1.0f, 1.0f},
        {1.0f, 1.0f},
    }};
    constexpr std::array<std::array<std::uint32_t, 2>, 12> frustumEdges{{
        {0u, 1u},
        {1u, 3u},
        {3u, 2u},
        {2u, 0u},
        {4u, 5u},
        {5u, 7u},
        {7u, 6u},
        {6u, 4u},
        {0u, 4u},
        {1u, 5u},
        {2u, 6u},
        {3u, 7u},
    }};
    constexpr float homogeneousEpsilon = 1.0e-6f;
    constexpr float clipTolerance = 2.0e-3f;
    const auto inverseViewProjection = core::inverse(viewProjection);
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    bool hasIntersection = false;

    std::array<core::Vec3, 8> frustumCorners{};
    for (std::size_t depthIndex = 0u; depthIndex < 2u; ++depthIndex) {
        const float depth = depthIndex == 0u ? 0.0f : 1.0f;
        for (std::size_t cornerIndex = 0u; cornerIndex < corners.size(); ++cornerIndex) {
            const auto corner = corners[cornerIndex];
            const auto homogeneous = core::transformHomogeneous(
                inverseViewProjection, {corner.x, corner.y, depth, 1.0f});
            if (!std::isfinite(homogeneous.x) || !std::isfinite(homogeneous.y)
                || !std::isfinite(homogeneous.z) || !std::isfinite(homogeneous.w)
                || std::abs(homogeneous.w) <= homogeneousEpsilon)
                return {{}, EditorSceneViewportTileMapVisibilityDiagnostic::InvalidInput};
            auto& point = frustumCorners[depthIndex * corners.size() + cornerIndex];
            point = {homogeneous.x / homogeneous.w,
                homogeneous.y / homogeneous.w,
                homogeneous.z / homogeneous.w};
            const auto projected =
                core::transformHomogeneous(viewProjection, {point.x, point.y, point.z, 1.0f});
            if (!std::isfinite(projected.x) || !std::isfinite(projected.y)
                || !std::isfinite(projected.z) || !std::isfinite(projected.w)
                || projected.w <= homogeneousEpsilon
                || std::abs(projected.x / projected.w - corner.x) > clipTolerance
                || std::abs(projected.y / projected.w - corner.y) > clipTolerance
                || std::abs(projected.z / projected.w - depth) > clipTolerance)
                return {{}, EditorSceneViewportTileMapVisibilityDiagnostic::InvalidInput};
        }
    }

    const auto include = [&](float x, float y) {
        if (!hasIntersection) {
            minX = maxX = x;
            minY = maxY = y;
            hasIntersection = true;
        } else {
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
    };
    for (const auto edge : frustumEdges) {
        const auto& a = frustumCorners[edge[0]];
        const auto& b = frustumCorners[edge[1]];
        const float da = a.z - planeZ;
        const float db = b.z - planeZ;
        if (std::abs(da) <= homogeneousEpsilon)
            include(a.x, a.y);
        if (std::abs(db) <= homogeneousEpsilon)
            include(b.x, b.y);
        if ((da < 0.0f && db > 0.0f) || (da > 0.0f && db < 0.0f)) {
            const float t = da / (da - db);
            const float x = a.x + (b.x - a.x) * t;
            const float y = a.y + (b.y - a.y) * t;
            if (!std::isfinite(x) || !std::isfinite(y))
                return {{}, EditorSceneViewportTileMapVisibilityDiagnostic::InvalidIntersection};
            include(x, y);
        }
    }

    if (!hasIntersection || maxX <= minX || maxY <= minY)
        return {{}, EditorSceneViewportTileMapVisibilityDiagnostic::InvalidIntersection};
    return {{minX, minY, maxX - minX, maxY - minY},
        EditorSceneViewportTileMapVisibilityDiagnostic::None};
}

} // namespace arp::editor
