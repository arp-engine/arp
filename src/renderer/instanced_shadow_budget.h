#pragma once

#include "core/diagnostic_error.h"
#include "core/index_runs.h"
#include "core/math.h"

#include <cmath>
#include <utility>

namespace arp::renderer {

// Same placement-origin horizontal-distance contract as shadow_cull.slang.
// Camera visibility is deliberately absent: off-camera objects cast shadows.
class InstancedShadowBudget final {
public:
    explicit InstancedShadowBudget(float distance) : distanceSquared_(distance * distance)
    {
        if (!std::isfinite(distance) || distance < 0)
            throw core::DiagnosticError("RENDERER-INSTANCED-SHADOW-DISTANCE-INVALID");
    }

    bool contains(core::Vec3 camera, const float* position) const
    {
        const float dx = position[0] - camera.x, dz = position[2] - camera.z;
        return dx * dx + dz * dz <= distanceSquared_;
    }

    template <class Instances, class Sink>
    void forEachRange(const Instances& instances, core::Vec3 camera, Sink&& sink) const
    {
        core::forEachSelectedIndexRun(
            instances.size(),
            [&](std::size_t i) {
                return contains(camera, instances[i].positionScale);
            },
            std::forward<Sink>(sink));
    }

private:
    float distanceSquared_;
};

} // namespace arp::renderer
