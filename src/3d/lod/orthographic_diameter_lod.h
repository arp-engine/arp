#pragma once

#include "core/diagnostics/diagnostic_error.h"
#include "core/math.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace arp::lod {
// OrthographicDiameterLod/1. Explicit descending PIXEL diameter thresholds.
// This is a size heuristic, not a certified surface/alpha-coverage error bound.
// Projection is a canonical row-major affine light matrix, independent of the
// observer camera. The caller supplies conservative, deformed world bounds.
class OrthographicDiameterLod final {
public:
    explicit OrthographicDiameterLod(std::array<double, 3> thresholds) : thresholds_(thresholds)
    {
        for (std::size_t i = 0; i < thresholds_.size(); ++i)
            if (!std::isfinite(thresholds_[i]) || thresholds_[i] <= 0
                || (i && thresholds_[i] >= thresholds_[i - 1]))
                throw core::DiagnosticError("LOD-ORTHO-DIAMETER-THRESHOLDS-INVALID");
    }

    unsigned select(double pixels, std::size_t modelCount) const
    {
        if (!std::isfinite(pixels) || pixels < 0 || !modelCount || modelCount > 4)
            throw core::DiagnosticError("LOD-ORTHO-DIAMETER-SELECTION-INVALID");
        for (unsigned level = 0; level + 1 < modelCount; ++level)
            if (pixels >= thresholds_[level])
                return level;
        return static_cast<unsigned>(modelCount - 1);
    }

    static double pixels(const float* lightViewProjection,
        core::Vec3 minimum,
        core::Vec3 maximum,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight)
    {
        if (!lightViewProjection || !viewportWidth || !viewportHeight)
            throw core::DiagnosticError("LOD-ORTHO-DIAMETER-VIEW-INVALID");
        for (unsigned i = 0; i < 16; ++i)
            if (!std::isfinite(lightViewProjection[i]))
                throw core::DiagnosticError("LOD-ORTHO-DIAMETER-MATRIX-INVALID");
        if (lightViewProjection[12] != 0 || lightViewProjection[13] != 0
            || lightViewProjection[14] != 0 || lightViewProjection[15] != 1)
            throw core::DiagnosticError("LOD-ORTHO-DIAMETER-AFFINE-REQUIRED");
        // The image plane needs two independent axes, even for a sheared
        // affine projection. Nonzero but parallel rows collapse it to a line.
        const double areaAxis[]{double(lightViewProjection[1]) * lightViewProjection[6]
                - double(lightViewProjection[2]) * lightViewProjection[5],
            double(lightViewProjection[2]) * lightViewProjection[4]
                - double(lightViewProjection[0]) * lightViewProjection[6],
            double(lightViewProjection[0]) * lightViewProjection[5]
                - double(lightViewProjection[1]) * lightViewProjection[4]};
        if (areaAxis[0] == 0 && areaAxis[1] == 0 && areaAxis[2] == 0)
            throw core::DiagnosticError("LOD-ORTHO-DIAMETER-PROJECTION-DEGENERATE");
        const double low[]{minimum.x, minimum.y, minimum.z};
        const double high[]{maximum.x, maximum.y, maximum.z};
        for (unsigned axis = 0; axis < 3; ++axis)
            if (!std::isfinite(low[axis]) || !std::isfinite(high[axis]) || low[axis] > high[axis])
                throw core::DiagnosticError("LOD-ORTHO-DIAMETER-BOUNDS-INVALID");
        double extent[2]{};
        for (unsigned row = 0; row < 2; ++row) {
            double norm = 0;
            for (unsigned axis = 0; axis < 3; ++axis) {
                const double coefficient = std::abs(double(lightViewProjection[row * 4 + axis]));
                extent[row] += coefficient * (high[axis] - low[axis]);
                norm += coefficient;
            }
            if (norm == 0)
                throw core::DiagnosticError("LOD-ORTHO-DIAMETER-PROJECTION-DEGENERATE");
        }
        const double diameter = std::hypot(
            extent[0] * (double(viewportWidth) * .5), extent[1] * (double(viewportHeight) * .5));
        if (!std::isfinite(diameter))
            throw core::DiagnosticError("LOD-ORTHO-DIAMETER-OVERFLOW");
        return diameter;
    }

private:
    std::array<double, 3> thresholds_;
};
} // namespace arp::lod
