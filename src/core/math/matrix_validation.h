#pragma once

#include "core/math/math.h"

#include <cmath>

namespace arp::core {
inline bool isFinite(const Mat4& value) noexcept
{
    for (const auto element : value.m)
        if (!std::isfinite(element))
            return false;
    return true;
}

// Explicit absolute residual bound; callers own the tolerance policy. Reject
// inverse()'s identity result for singular inputs, and nonfinite residuals.
inline bool isApproximateInverse(const Mat4& value, const Mat4& inverse, float tolerance) noexcept
{
    if (!std::isfinite(tolerance) || tolerance < 0 || !isFinite(value) || !isFinite(inverse))
        return false;
    // Evaluate the residual of the supplied float matrices in double. A float
    // product can itself lose more than the caller's bound through cancellation
    // and reject a valid inverse. This does not relax the absolute tolerance.
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            double product = 0;
            for (std::size_t k = 0; k < 4; ++k)
                product += double(value.m[row * 4 + k]) * double(inverse.m[k * 4 + col]);
            if (!std::isfinite(product)
                || std::fabs(product - (row == col ? 1.0 : 0.0)) > double(tolerance))
                return false;
        }
    }
    return true;
}
} // namespace arp::core
