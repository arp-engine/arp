#pragma once
#include "core/math/math.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace arp::vegetation {
// Serialized StaticVegetationV1 record: ownership is independent of terrain and PCG.
struct StaticVegetationInstance final {
    std::uint32_t prototypeId = 0u;
    core::Vec3 position{};
    std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
    core::Vec3 scale{1.0f, 1.0f, 1.0f};
    std::uint32_t variationSeed = 0u;

    // Existing terrain static-record contract: squared unit-quaternion tolerance.
    static constexpr float unitRotationSquaredLengthTolerance = 0.001f;

    bool hasValidTransform() const noexcept
    {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)
            || !std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z)
            || scale.x <= 0.0f || scale.y <= 0.0f || scale.z <= 0.0f)
            return false;
        float rotationLengthSquared = 0.0f;
        for (const float component : rotation) {
            if (!std::isfinite(component))
                return false;
            rotationLengthSquared += component * component;
        }
        return std::fabs(rotationLengthSquared - 1.0f) <= unitRotationSquaredLengthTolerance;
    }
};

} // namespace arp::vegetation
