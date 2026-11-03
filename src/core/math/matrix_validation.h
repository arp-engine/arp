#pragma once

#include "core/math/math.h"

#include <cmath>
#include <limits>

namespace arp::core {
struct AffineTrs final {
    Vec3 translation;
    Quat rotation;
    Vec3 scale{1, 1, 1};
};

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

// Row-major affine matrix -> T*R*S, with a caller-owned absolute error bound
// on every reconstructed matrix element. Reflection is assigned to the X scale
// deterministically when all axes are nonzero. Null scale axes get an explicit
// orthonormal completion; they contribute zero to the matrix, so the choice
// changes no transformed point. The residual bound is the only admitted error.
inline bool tryDecomposeAffineTrs(
    const Mat4& matrix, float elementError, AffineTrs& output) noexcept
{
    if (!std::isfinite(elementError) || elementError < 0 || !isFinite(matrix) || matrix.m[12] != 0
        || matrix.m[13] != 0 || matrix.m[14] != 0 || matrix.m[15] != 1)
        return false;
    float scales[3];
    Vec3 columns[3];
    unsigned nonzero = 0;
    const auto unit = [](Vec3 value, Vec3& result) {
        const auto size = std::hypot(double(value.x), double(value.y), double(value.z));
        if (!std::isfinite(size) || size == 0)
            return false;
        result = {static_cast<float>(value.x / size),
            static_cast<float>(value.y / size),
            static_cast<float>(value.z / size)};
        return true;
    };
    for (unsigned c = 0; c < 3; ++c) {
        const auto size =
            std::hypot(double(matrix.m[c]), double(matrix.m[4 + c]), double(matrix.m[8 + c]));
        if (!std::isfinite(size) || size > std::numeric_limits<float>::max())
            return false;
        scales[c] = static_cast<float>(size);
        if (size == 0) {
            columns[c] = {};
            continue;
        }
        ++nonzero;
        columns[c] = {static_cast<float>(matrix.m[c] / size),
            static_cast<float>(matrix.m[4 + c] / size),
            static_cast<float>(matrix.m[8 + c] / size)};
    }
    if (nonzero == 0) {
        columns[0] = {1, 0, 0};
        columns[1] = {0, 1, 0};
        columns[2] = {0, 0, 1};
    } else if (nonzero == 1) {
        const unsigned known = scales[0] != 0 ? 0 : scales[1] != 0 ? 1 : 2;
        const auto v = columns[known];
        // Least-aligned canonical axis guarantees a nonzero cross product.
        const auto axis = std::abs(v.x) <= std::abs(v.y) && std::abs(v.x) <= std::abs(v.z)
            ? Vec3{1, 0, 0}
            : std::abs(v.y) <= std::abs(v.z) ? Vec3{0, 1, 0}
                                             : Vec3{0, 0, 1};
        if (!unit(cross(axis, v), columns[(known + 1) % 3]))
            return false;
        if (!unit(cross(v, columns[(known + 1) % 3]), columns[(known + 2) % 3]))
            return false;
    } else if (nonzero == 2) {
        const unsigned missing = scales[0] == 0 ? 0 : scales[1] == 0 ? 1 : 2;
        if (!unit(cross(columns[(missing + 1) % 3], columns[(missing + 2) % 3]), columns[missing]))
            return false;
    }
    const auto handedness = dot(columns[0], cross(columns[1], columns[2]));
    if (!std::isfinite(handedness) || handedness == 0)
        return false;
    if (handedness < 0) {
        scales[0] = -scales[0];
        columns[0] = scale(columns[0], -1);
    }
    AffineTrs candidate;
    candidate.translation = {matrix.m[3], matrix.m[7], matrix.m[11]};
    candidate.scale = {scales[0], scales[1], scales[2]};
    if (!tryNormalize(detail::quatFromBasisColumnsUnchecked(columns[0], columns[1], columns[2]),
            candidate.rotation))
        return false;
    const auto rebuilt = multiply(
        multiply(Mat4::translation(candidate.translation), Mat4::rotation(candidate.rotation)),
        Mat4::scale(candidate.scale));
    if (!isFinite(rebuilt))
        return false;
    for (std::size_t i = 0; i < matrix.m.size(); ++i)
        if (std::abs(double(matrix.m[i]) - double(rebuilt.m[i])) > double(elementError))
            return false;
    output = candidate;
    return true;
}
} // namespace arp::core
