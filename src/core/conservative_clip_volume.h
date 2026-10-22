#pragma once

#include "core/diagnostic_error.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace arp::core {
// ConservativeClipVolume/1. Canonical row-major clip=M*p; z in[0,w].
// A float dot4 has four products/three additions; combining two clip
// coordinates adds one operation. gamma(10) bounds that evaluation plus
// coefficient/evaluation rounding. FMA/reassociation uses no more operations.
// The min-normal error also encloses input and per-operation flush-to-zero.
// FP64 evaluation is only the rejection proof, never a replacement shader.
class ConservativeClipVolume final {
    struct Plane {
        double coefficient[4]{};
        double magnitude[4]{};
    };

    Plane planes_[6];

public:
    explicit ConservativeClipVolume(const float* matrix)
    {
        if (!matrix)
            throw DiagnosticError("CLIP-VOLUME-MATRIX-MISSING");
        for (unsigned i = 0; i < 16u; ++i)
            if (!std::isfinite(matrix[i]))
                throw DiagnosticError("CLIP-VOLUME-MATRIX-NONFINITE");
        for (unsigned p = 0; p < 6u; ++p) {
            const unsigned row = p < 4u ? p / 2u : 2u;
            const bool near = p == 4u;
            const double sign = p % 2u == 0u ? 1.0 : -1.0;
            for (unsigned a = 0; a < 4u; ++a) {
                const double primary = near ? 0.0 : matrix[12u + a];
                const double secondary = matrix[row * 4u + a];
                planes_[p].coefficient[a] = primary + sign * secondary;
                planes_[p].magnitude[a] = std::abs(primary) + std::abs(secondary);
            }
            const auto& plane = planes_[p];
            if (plane.coefficient[0] == 0 && plane.coefficient[1] == 0 && plane.coefficient[2] == 0)
                throw DiagnosticError("CLIP-VOLUME-PLANE-DEGENERATE");
        }
    }

    bool intersects(const float* minimum, const float* maximum) const
    {
        for (unsigned a = 0; a < 3u; ++a)
            if (!std::isfinite(minimum[a]) || !std::isfinite(maximum[a]) || minimum[a] > maximum[a])
                throw DiagnosticError("CLIP-VOLUME-BOUNDS-INVALID");
        constexpr double u = std::numeric_limits<float>::epsilon() * .5;
        constexpr double gamma = 10.0 * u / (1.0 - 10.0 * u);
        for (const auto& plane : planes_) {
            double distance = plane.coefficient[3], magnitude = plane.magnitude[3];
            double coefficientMagnitude = plane.magnitude[3], inputMagnitude = 1.0;
            for (unsigned a = 0; a < 3u; ++a) {
                distance +=
                    plane.coefficient[a] * (plane.coefficient[a] >= 0 ? maximum[a] : minimum[a]);
                magnitude += plane.magnitude[a]
                    * std::max(std::abs(double(minimum[a])), std::abs(double(maximum[a])));
                coefficientMagnitude += plane.magnitude[a];
                inputMagnitude +=
                    std::max(std::abs(double(minimum[a])), std::abs(double(maximum[a])));
            }
            if (magnitude > std::numeric_limits<float>::max())
                throw DiagnosticError("CLIP-VOLUME-FLOAT-EVALUATION-OVERFLOW");
            const double flushError = 16.0 * std::numeric_limits<float>::min()
                * (1.0 + coefficientMagnitude + 2.0 * inputMagnitude);
            const double error = std::nextafter(
                gamma * magnitude + flushError, std::numeric_limits<double>::infinity());
            if (distance < -error)
                return false;
        }
        return true;
    }
};
} // namespace arp::core
