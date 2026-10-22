#pragma once

#include "2d/sprite_draw_plan.h"
#include "core/math/math.h"

#include <cmath>

namespace arp::editor {

inline bool projectEditorSceneViewportSpriteInstance(const scene2d::SpriteInstance& source,
    const core::Mat4& viewProjection,
    scene2d::SpriteInstance& projected) noexcept
{
    projected = source;
    projected.origin = core::transformHomogeneous(
        viewProjection, {source.origin.x, source.origin.y, source.origin.z, 1.0f});
    projected.axisX = core::transformHomogeneous(
        viewProjection, {source.axisX.x, source.axisX.y, source.axisX.z, 0.0f});
    projected.axisY = core::transformHomogeneous(
        viewProjection, {source.axisY.x, source.axisY.y, source.axisY.z, 0.0f});

    const auto finite = [](const core::Vec4& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z)
            && std::isfinite(value.w);
    };
    return finite(projected.origin) && finite(projected.axisX) && finite(projected.axisY);
}

} // namespace arp::editor
