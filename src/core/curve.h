#pragma once

#include "core/cpu_allocator.h"
#include "reflection/reflection_macros.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace arp::core {

ASTRUCT(Construct = Default)

struct CurveKey {
    APROP(Serialize)
    float time = 0.0f;
    APROP(Serialize)
    float value = 0.0f;
};

ASTRUCT(Construct = Default)

struct FloatCurve {
    core::pmr::Vector<CurveKey> keys{&core::defaultCpuMemoryResource()};

    bool valid() const
    {
        if (keys.empty()) {
            return false;
        }
        for (size_t i = 1; i < keys.size(); ++i) {
            if (keys[i].time < keys[i - 1].time) {
                return false;
            }
        }
        return true;
    }

    float sample(float t) const
    {
        if (keys.empty()) {
            return 0.0f;
        }
        if (keys.size() == 1u) {
            return keys[0].value;
        }
        if (t <= keys.front().time) {
            return keys.front().value;
        }
        if (t >= keys.back().time) {
            return keys.back().value;
        }
        size_t upper = 1;
        while (upper < keys.size() && keys[upper].time < t) {
            ++upper;
        }
        const auto& a = keys[upper - 1];
        const auto& b = keys[upper];
        const float denom = b.time - a.time;
        if (denom <= 0.0f) {
            return a.value;
        }
        const float localT = (t - a.time) / denom;
        return a.value + (b.value - a.value) * localT;
    }
};

} // namespace arp::core
