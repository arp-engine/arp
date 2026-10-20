#pragma once

#include "animation/skeleton.h"
#include "core/array_view.h"

#include <array>
#include <cmath>

namespace animation {

// Explicit CPU vertex preparation. Uses the animation module's existing matrix
// boundary and the same normalized four-influence blend as GPU skinning.
class LinearBlendSkinning final {
public:
    struct Result {
        Vec3 position;
        Vec3 normal;
    };

    static bool evaluate(arp::core::ArrayView<Mat4> matrices,
        const std::array<std::uint32_t, 4>& joints,
        const std::array<float, 4>& weights,
        Vec3 position,
        Vec3 normal,
        Result& result)
    {
        Result blended{};
        float total = 0.0f;
        for (std::size_t lane = 0; lane < weights.size(); ++lane) {
            const float weight = weights[lane];
            if (!std::isfinite(weight) || weight < 0.0f)
                return false;
            if (weight == 0.0f)
                continue;
            if (joints[lane] >= matrices.size())
                return false;
            const auto p = transformPoint(matrices[joints[lane]], position);
            const auto n = transformVector(matrices[joints[lane]], normal);
            blended.position.x += p.x * weight;
            blended.position.y += p.y * weight;
            blended.position.z += p.z * weight;
            blended.normal.x += n.x * weight;
            blended.normal.y += n.y * weight;
            blended.normal.z += n.z * weight;
            total += weight;
        }
        const float length = std::sqrt(blended.normal.x * blended.normal.x
            + blended.normal.y * blended.normal.y + blended.normal.z * blended.normal.z);
        if (!std::isfinite(total) || total <= 0.0f || !std::isfinite(length) || length <= 0.0f)
            return false;
        result = {
            {blended.position.x / total, blended.position.y / total, blended.position.z / total},
            {blended.normal.x / length, blended.normal.y / length, blended.normal.z / length}};
        return std::isfinite(result.position.x) && std::isfinite(result.position.y)
            && std::isfinite(result.position.z);
    }
};
} // namespace animation
