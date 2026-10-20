#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace arp::core {
// ScreenDiameterLod/1: descending viewport-height fractions, with relative
// hysteresis. Independent of resource ownership and graphics backend.
struct ScreenDiameterLod {
    std::array<float, 4> thresholds{};
    int count = 1;
    float hysteresis = 0.1f;

    int resolve(float diameter, int current) const noexcept
    {
        int ideal = count - 1;
        for (int i = 0; i < count - 1; ++i)
            if (diameter >= thresholds[i]) {
                ideal = i;
                break;
            }
        if (current < 0 || current >= count)
            return ideal;
        if (ideal > current && diameter > thresholds[current] * (1 - hysteresis))
            return current;
        if (ideal < current && diameter < thresholds[ideal] * (1 + hysteresis))
            return current;
        return ideal;
    }

    static float diameter(float worldDiameter, float projectionY, float depth) noexcept
    {
        return worldDiameter * projectionY / (2 * std::max(depth, 0.001f));
    }
};

struct LodTransition {
    int current = -1, target = -1;
    float started = 0;

    bool active() const noexcept { return target >= 0; }

    void begin(int desired, float now) noexcept
    {
        target = current + (desired > current ? 1 : -1);
        started = now;
    }

    float coverage(float now, float duration) const noexcept
    {
        return std::clamp((now - started) / duration, 0.f, 1.f);
    }

    bool finish(float now, float duration) noexcept
    {
        if (!active() || coverage(now, duration) < 1)
            return false;
        current = target;
        target = -1;
        return true;
    }
};

struct LodTransitionBudget {
    std::uint32_t maxActive = 0, maxStarts = 0;
    std::uint64_t maxTriangles = 0;
    std::uint32_t active = 0, starts = 0;
    std::uint64_t triangles = 0;

    bool admit(std::uint64_t cost) noexcept
    {
        if (active >= maxActive || starts >= maxStarts
            || cost > maxTriangles - std::min(triangles, maxTriangles))
            return false;
        ++active;
        ++starts;
        triangles += cost;
        return true;
    }
};
} // namespace arp::core
