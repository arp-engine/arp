#pragma once

#include "core/math/conservative_float_interval.h"

#include <array>
#include <cstdint>

namespace arp::renderer {
// PerspectiveSurfaceErrorProjection/1: CPU geometric error of clip=M*p with
// z in [0,w]. Input bounds contain BOTH baked world-space surfaces; deviation
// bounds matching surface points per world axis. Local QEM residuals alone are
// not such a bound. This does not certify material/UV/normal/alpha appearance,
// GPU arithmetic, rasterization, temporal stability, or an asset's provenance.
class PerspectiveSurfaceErrorProjection final {
public:
    struct Envelope {
        std::array<float, 3> minimum{}, maximum{};
        std::array<double, 3> deviation{};
    };

    struct Error {
        bool bounded = false;
        double pixelsX = 0, pixelsY = 0, normalizedDepth = 0;

        bool fits(double pixels, double depth) const
        {
            if (!std::isfinite(pixels) || !std::isfinite(depth) || pixels <= 0 || depth <= 0)
                throw core::DiagnosticError("PERSPECTIVE-ERROR-BUDGET-INVALID");
            if (!std::isfinite(pixelsX) || !std::isfinite(pixelsY)
                || !std::isfinite(normalizedDepth) || pixelsX < 0 || pixelsY < 0
                || normalizedDepth < 0)
                throw core::DiagnosticError("PERSPECTIVE-ERROR-RESULT-INVALID");
            return bounded && pixelsX <= pixels && pixelsY <= pixels && normalizedDepth <= depth;
        }
    };

    static Error project(const float* viewProjection,
        const Envelope& world,
        std::uint32_t width,
        std::uint32_t height)
    {
        if (!viewProjection || !width || !height)
            throw core::DiagnosticError("PERSPECTIVE-ERROR-VIEW-INVALID");
        for (unsigned i = 0; i < 16; ++i)
            if (!std::isfinite(viewProjection[i]))
                throw core::DiagnosticError("PERSPECTIVE-ERROR-MATRIX-NONFINITE");
        using Interval = core::ConservativeFloatInterval;
        std::array<Interval, 3> positions;
        for (unsigned axis = 0; axis < 3; ++axis) {
            positions[axis] = Interval::range(world.minimum[axis], world.maximum[axis]);
            if (!std::isfinite(world.deviation[axis]) || world.deviation[axis] < 0)
                throw core::DiagnosticError("PERSPECTIVE-ERROR-DEVIATION-INVALID");
        }
        std::array<Interval, 4> clip;
        std::array<double, 4> deviation{};
        for (unsigned row = 0; row < 4; ++row) {
            clip[row] = Interval::point(viewProjection[row * 4 + 3]);
            for (unsigned axis = 0; axis < 3; ++axis) {
                const auto coefficient = viewProjection[row * 4 + axis];
                clip[row] = clip[row] + Interval::point(coefficient) * positions[axis];
                deviation[row] = add(
                    deviation[row], multiply(std::abs(double(coefficient)), world.deviation[axis]));
            }
        }
        // A ratio bound cannot cross the eye plane. Near-plane clipping changes
        // surface correspondence, so retain the original there as well. No
        // arbitrary epsilon/distance threshold and no silent clamped denominator.
        if (clip[3].low <= 0 || clip[2].low <= 0)
            return {};
        const double minimumW = clip[3].low;
        std::array<double, 3> projected{};
        for (unsigned row = 0; row < 3; ++row) {
            // |a'/w' - a/w| <= (|da| + max|a|*|dw|/minW)/minW.
            // The joint bounds cover both surfaces, including off-axis points.
            projected[row] =
                divide(add(deviation[row],
                           divide(multiply(clip[row].absoluteMaximum(), deviation[3]), minimumW)),
                    minimumW);
        }
        return {true,
            multiply(projected[0], double(width) * .5),
            multiply(projected[1], double(height) * .5),
            projected[2]};
    }

    // Explicit original fallback; callers preserve independent candidate
    // eligibility and must not infer monotonicity from level numbers.
    static unsigned select(const std::array<Error, 3>& levels, double pixels, double depth)
    {
        std::array<bool, 3> eligible{};
        for (unsigned level = 0; level < levels.size(); ++level)
            eligible[level] = levels[level].fits(pixels, depth);
        for (unsigned level = 2; level > 0; --level)
            if (eligible[level])
                return level;
        return 0;
    }

private:
    static double up(double value)
    {
        if (!std::isfinite(value) || value < 0)
            throw core::DiagnosticError("PERSPECTIVE-ERROR-ARITHMETIC-OVERFLOW");
        value = std::nextafter(value, std::numeric_limits<double>::infinity());
        if (!std::isfinite(value))
            throw core::DiagnosticError("PERSPECTIVE-ERROR-ARITHMETIC-OVERFLOW");
        return value;
    }

    static double add(double a, double b) { return a == 0 && b == 0 ? 0 : up(a + b); }

    static double multiply(double a, double b) { return a == 0 || b == 0 ? 0 : up(a * b); }

    static double divide(double a, double b) { return a == 0 ? 0 : up(a / b); }
};
} // namespace arp::renderer
