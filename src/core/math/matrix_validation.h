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
    const auto product = multiply(value, inverse);
    for (std::size_t i = 0; i < product.m.size(); ++i)
        if (!std::isfinite(product.m[i])
            || std::fabs(product.m[i] - (i % 5u == 0u ? 1.0f : 0.0f)) > tolerance)
            return false;
    return true;
}
} // namespace arp::core
