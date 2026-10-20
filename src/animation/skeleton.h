#pragma once

#include "animation/animation.h"
#include "core/api.h"
#include "core/cpu_allocator.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace animation {

// LocalTransform is defined in animation.h (included above) to break the
// circular include dependency between this header and animation.h.

struct ARP_API Mat4 {
    std::array<float, 16> m{};

    static Mat4 identity();
    static Mat4 translation(Vec3 value);
    static Mat4 scale(Vec3 value);
    static Mat4 rotation(Quat value);
};

ARP_API Mat4 multiply(const Mat4& lhs, const Mat4& rhs);
ARP_API Mat4 toMatrix(const LocalTransform& transform);
ARP_API LocalTransform lerp(const LocalTransform& a, const LocalTransform& b, float t);

// Inverts a 4x4 affine matrix (last row must be [0,0,0,1]).
// Used by asset importers to reconcile per-mesh inverse bind matrices.
ARP_API Mat4 affineInverse(const Mat4& matrix);

// Transforms a point (with translation) and a direction (without translation)
// using the column-major Mat4 storage and the column-vector convention (M * v).
ARP_API Vec3 transformPoint(const Mat4& matrix, Vec3 value);
ARP_API Vec3 transformVector(const Mat4& matrix, Vec3 value);

struct Joint {
    std::pmr::string name;
    int parentIndex = -1;
    Mat4 inverseBindMatrix = Mat4::identity();
};

struct ARP_API Skeleton {
    std::pmr::vector<Joint> joints;

    bool valid() const;
    std::size_t jointCount() const;
};

ARP_API std::pmr::vector<Mat4> buildModelPose(
    const Skeleton& skeleton, const std::pmr::vector<LocalTransform>& localPose);

ARP_API std::pmr::vector<Mat4> buildSkinMatrices(
    const Skeleton& skeleton, const std::pmr::vector<Mat4>& modelPose);

} // namespace animation
