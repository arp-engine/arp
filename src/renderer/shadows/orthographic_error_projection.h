#pragma once

#include "core/diagnostics/diagnostic_error.h"
#include "renderer/shadows/detail/projection_rounding.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace arp::core {
// OrthographicErrorProjection/1. Canonical row-major M*p, clip.w=1, depth [0,1].
// No correspondence between source/candidate normals is assumed. A certified
// two-sided local SURFACE distance bounds geometry; both baked normal envelopes
// bound the independently biased surfaces. QEM residuals are not valid inputs.
class OrthographicErrorProjection final {
public:
    struct WorldEnvelope {
        double deviation[3]{};
        double absoluteSupport[3]{};
    };

    struct ProjectedError {
        double pixelsX = 0, pixelsY = 0, normalizedDepth = 0;

        bool fits(double texels, double depth) const
        {
            if (!std::isfinite(texels) || !std::isfinite(depth) || texels <= 0 || depth <= 0)
                throw DiagnosticError("ORTHO-ERROR-BUDGET-INVALID");
            requireNonnegative(pixelsX);
            requireNonnegative(pixelsY);
            requireNonnegative(normalizedDepth);
            return pixelsX <= texels && pixelsY <= texels && normalizedDepth <= depth;
        }
    };

    // localAbsoluteSupport must contain BOTH surfaces. Normal envelopes describe
    // actual imported float normals, including the importer's normalization.
    static WorldEnvelope worldEnvelope(const float* affine,
        double certifiedLocalSurfaceDistance,
        const double* localAbsoluteSupport,
        const double* originalBakedNormalMaximum,
        const double* candidateBakedNormalMaximum,
        float normalBias)
    {
        requireAffine(affine);
        requireNonnegative(certifiedLocalSurfaceDistance);
        if (!std::isfinite(normalBias) || !localAbsoluteSupport || !originalBakedNormalMaximum
            || !candidateBakedNormalMaximum)
            throw DiagnosticError("ORTHO-ERROR-ENVELOPE-INVALID");
        for (unsigned a = 0; a < 3; ++a) {
            requireNonnegative(localAbsoluteSupport[a]);
            requireNonnegative(originalBakedNormalMaximum[a]);
            requireNonnegative(candidateBakedNormalMaximum[a]);
            requireFloatRange(localAbsoluteSupport[a]);
            requireFloatRange(originalBakedNormalMaximum[a]);
            requireFloatRange(candidateBakedNormalMaximum[a]);
        }
        WorldEnvelope result;
        for (unsigned row = 0; row < 3; ++row) {
            double squaredNorm = 0, magnitude = std::abs(double(affine[row * 4 + 3]));
            double coefficients = magnitude, inputs = 1;
            for (unsigned a = 0; a < 3; ++a) {
                const double coefficient = std::abs(double(affine[row * 4 + a]));
                squaredNorm = add(squaredNorm, multiply(coefficient, coefficient));
                magnitude = add(magnitude, multiply(coefficient, localAbsoluteSupport[a]));
                coefficients = add(coefficients, coefficient);
                inputs = add(inputs, localAbsoluteSupport[a]);
            }
            const double transformError = rounding(magnitude, coefficients, inputs);
            const double originalBias =
                multiply(std::abs(double(normalBias)), originalBakedNormalMaximum[row]);
            const double candidateBias =
                multiply(std::abs(double(normalBias)), candidateBakedNormalMaximum[row]);
            const double biasMaximum = std::max(originalBias, candidateBias);
            const double supportBeforeBias = add(magnitude, transformError);
            // Two more shader float operations: normal*bias and position+offset.
            // gamma(10) also encloses fused evaluation and FTZ of these inputs.
            const double biasError = rounding(add(supportBeforeBias, biasMaximum),
                add(1, std::abs(double(normalBias))),
                add(supportBeforeBias,
                    std::max(originalBakedNormalMaximum[row], candidateBakedNormalMaximum[row])));
            result.absoluteSupport[row] = add(add(supportBeforeBias, biasMaximum), biasError);
            result.deviation[row] =
                add(add(multiply(certifiedLocalSurfaceDistance, upward(std::sqrt(squaredNorm))),
                        multiply(2, transformError)),
                    add(add(originalBias, candidateBias), multiply(2, biasError)));
            requireFloatRange(result.absoluteSupport[row]);
        }
        return result;
    }

    static ProjectedError project(const float* lightViewProjection,
        const WorldEnvelope& world,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight)
    {
        requireAffine(lightViewProjection);
        if (!viewportWidth || !viewportHeight)
            throw DiagnosticError("ORTHO-ERROR-VIEWPORT-INVALID");
        for (unsigned a = 0; a < 3; ++a) {
            requireNonnegative(world.deviation[a]);
            requireNonnegative(world.absoluteSupport[a]);
            requireFloatRange(world.absoluteSupport[a]);
        }
        double projected[3]{};
        for (unsigned row = 0; row < 3; ++row) {
            double magnitude = std::abs(double(lightViewProjection[row * 4 + 3]));
            double coefficients = magnitude, inputs = 1, norm = 0;
            for (unsigned a = 0; a < 3; ++a) {
                const double coefficient = std::abs(double(lightViewProjection[row * 4 + a]));
                norm = add(norm, coefficient);
                projected[row] = add(projected[row], multiply(coefficient, world.deviation[a]));
                magnitude = add(magnitude, multiply(coefficient, world.absoluteSupport[a]));
                coefficients = add(coefficients, coefficient);
                inputs = add(inputs, world.absoluteSupport[a]);
            }
            if (norm == 0)
                throw DiagnosticError("ORTHO-ERROR-PROJECTION-DEGENERATE");
            projected[row] =
                add(projected[row], multiply(2, rounding(magnitude, coefficients, inputs)));
        }
        return {multiply(projected[0], double(viewportWidth) * .5),
            multiply(projected[1], double(viewportHeight) * .5),
            projected[2]};
    }

private:
    static double upward(double value)
    {
        return renderer::detail::ProjectionRounding::upward(value);
    }

    static double add(double a, double b) { return a == 0 && b == 0 ? 0 : upward(a + b); }

    static double multiply(double a, double b) { return a == 0 || b == 0 ? 0 : upward(a * b); }

    static void requireNonnegative(double value)
    {
        if (!std::isfinite(value) || value < 0)
            throw DiagnosticError("ORTHO-ERROR-ENVELOPE-INVALID");
    }

    static void requireFloatRange(double value)
    {
        if (value > std::numeric_limits<float>::max())
            throw DiagnosticError("ORTHO-ERROR-FLOAT-EVALUATION-OVERFLOW");
    }

    static void requireAffine(const float* matrix)
    {
        if (!matrix)
            throw DiagnosticError("ORTHO-ERROR-MATRIX-MISSING");
        for (unsigned i = 0; i < 16; ++i)
            if (!std::isfinite(matrix[i]))
                throw DiagnosticError("ORTHO-ERROR-MATRIX-NONFINITE");
        if (matrix[12] != 0 || matrix[13] != 0 || matrix[14] != 0 || matrix[15] != 1)
            throw DiagnosticError("ORTHO-ERROR-AFFINE-W-ONE-REQUIRED");
    }

    static double rounding(double magnitude, double coefficientMagnitude, double inputMagnitude)
    {
        // At most four float products, three additions and bias operations.
        // More than the actual scalar/FMA operation count; includes input and
        // operation FTZ error, like ConservativeClipVolume/1. Outward double
        // operations protect the proof computation from its own roundoff.
        constexpr double u = std::numeric_limits<float>::epsilon() * .5;
        const double gamma = upward(10 * u / (1 - 10 * u));
        const double flush = multiply(16.0 * std::numeric_limits<float>::min(),
            add(add(1, coefficientMagnitude), multiply(2, inputMagnitude)));
        requireFloatRange(add(magnitude, multiply(gamma, magnitude)));
        return add(multiply(gamma, magnitude), flush);
    }
};
} // namespace arp::core
