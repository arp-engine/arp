#include "animation/skeleton.h"

#include "core/cpu_allocator.h"

#include <algorithm>
#include <cmath>

namespace animation {

namespace {

float clamp01(const float value)
{
    return std::max(0.0f, std::min(1.0f, value));
}

Quat normalize(const Quat value)
{
    const auto length =
        std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w);
    if (length <= 0.000001f) {
        return Quat::identity();
    }
    return Quat{
        value.x / length,
        value.y / length,
        value.z / length,
        value.w / length,
    };
}

} // namespace

Quat Quat::identity()
{
    return Quat{};
}

Mat4 Mat4::identity()
{
    Mat4 result;
    result.m[0] = 1.0f;
    result.m[5] = 1.0f;
    result.m[10] = 1.0f;
    result.m[15] = 1.0f;
    return result;
}

Mat4 Mat4::translation(const Vec3 value)
{
    auto result = identity();
    result.m[12] = value.x;
    result.m[13] = value.y;
    result.m[14] = value.z;
    return result;
}

Mat4 Mat4::scale(const Vec3 value)
{
    Mat4 result;
    result.m[0] = value.x;
    result.m[5] = value.y;
    result.m[10] = value.z;
    result.m[15] = 1.0f;
    return result;
}

Mat4 Mat4::rotation(const Quat raw)
{
    const auto q = normalize(raw);
    const auto xx = q.x * q.x;
    const auto yy = q.y * q.y;
    const auto zz = q.z * q.z;
    const auto xy = q.x * q.y;
    const auto xz = q.x * q.z;
    const auto yz = q.y * q.z;
    const auto wx = q.w * q.x;
    const auto wy = q.w * q.y;
    const auto wz = q.w * q.z;

    auto result = identity();
    result.m[0] = 1.0f - 2.0f * (yy + zz);
    result.m[1] = 2.0f * (xy + wz);
    result.m[2] = 2.0f * (xz - wy);
    result.m[4] = 2.0f * (xy - wz);
    result.m[5] = 1.0f - 2.0f * (xx + zz);
    result.m[6] = 2.0f * (yz + wx);
    result.m[8] = 2.0f * (xz + wy);
    result.m[9] = 2.0f * (yz - wx);
    result.m[10] = 1.0f - 2.0f * (xx + yy);
    return result;
}

Mat4 multiply(const Mat4& lhs, const Mat4& rhs)
{
    Mat4 result;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            float value = 0.0f;
            for (int k = 0; k < 4; ++k) {
                value += lhs.m[k * 4 + row] * rhs.m[column * 4 + k];
            }
            result.m[column * 4 + row] = value;
        }
    }
    return result;
}

Mat4 toMatrix(const LocalTransform& transform)
{
    return multiply(
        multiply(Mat4::translation(transform.translation), Mat4::rotation(transform.rotation)),
        Mat4::scale(transform.scale));
}

LocalTransform lerp(const LocalTransform& a, const LocalTransform& b, const float t)
{
    const auto amount = clamp01(t);
    const auto interpolate = [amount](const float left, const float right) {
        return left + (right - left) * amount;
    };
    return LocalTransform{
        {
            interpolate(a.translation.x, b.translation.x),
            interpolate(a.translation.y, b.translation.y),
            interpolate(a.translation.z, b.translation.z),
        },
        normalize(Quat{
            interpolate(a.rotation.x, b.rotation.x),
            interpolate(a.rotation.y, b.rotation.y),
            interpolate(a.rotation.z, b.rotation.z),
            interpolate(a.rotation.w, b.rotation.w),
        }),
        {
            interpolate(a.scale.x, b.scale.x),
            interpolate(a.scale.y, b.scale.y),
            interpolate(a.scale.z, b.scale.z),
        },
    };
}

bool Skeleton::valid() const
{
    for (std::size_t index = 0; index < joints.size(); ++index) {
        const auto parent = joints[index].parentIndex;
        if (parent >= static_cast<int>(index)) {
            return false;
        }
        if (parent < -1) {
            return false;
        }
    }
    return !joints.empty();
}

std::size_t Skeleton::jointCount() const
{
    return joints.size();
}

std::pmr::vector<Mat4> buildModelPose(
    const Skeleton& skeleton, const std::pmr::vector<LocalTransform>& localPose)
{
    std::pmr::vector<Mat4> modelPose;
    if (!skeleton.valid() || localPose.size() != skeleton.joints.size()) {
        return modelPose;
    }

    modelPose.resize(skeleton.joints.size());
    for (std::size_t index = 0; index < skeleton.joints.size(); ++index) {
        const auto localMatrix = toMatrix(localPose[index]);
        const auto parent = skeleton.joints[index].parentIndex;
        modelPose[index] = parent < 0
            ? localMatrix
            : multiply(modelPose[static_cast<std::size_t>(parent)], localMatrix);
    }
    return modelPose;
}

std::pmr::vector<Mat4> buildSkinMatrices(
    const Skeleton& skeleton, const std::pmr::vector<Mat4>& modelPose)
{
    std::pmr::vector<Mat4> skinMatrices;
    if (!skeleton.valid() || modelPose.size() != skeleton.joints.size()) {
        return skinMatrices;
    }

    skinMatrices.reserve(skeleton.joints.size());
    for (std::size_t index = 0; index < skeleton.joints.size(); ++index) {
        skinMatrices.push_back(
            multiply(modelPose[index], skeleton.joints[index].inverseBindMatrix));
    }
    return skinMatrices;
}

} // namespace animation

// Affine inverse and vector transforms are defined at file scope so they can be
// shared by the animation runtime and asset importers without pulling the
// cpu_skinning internal helpers into the public API surface.
namespace animation {

// Inverts a 4x4 matrix whose last row is [0, 0, 0, 1] (affine transform).
// Uses the block-inverse formula:  inverse([R t; 0 1]) = [R^-1  -R^-1 t; 0 1]
// where R is the upper-left 3x3 (column-major storage m[c*4+r]).
Mat4 affineInverse(const Mat4& matrix)
{
    const float a0 = matrix.m[0];
    const float a1 = matrix.m[1];
    const float a2 = matrix.m[2];
    const float b0 = matrix.m[4];
    const float b1 = matrix.m[5];
    const float b2 = matrix.m[6];
    const float c0 = matrix.m[8];
    const float c1 = matrix.m[9];
    const float c2 = matrix.m[10];

    const float det =
        a0 * (b1 * c2 - b2 * c1) - b0 * (a1 * c2 - a2 * c1) + c0 * (a1 * b2 - a2 * b1);

    Mat4 result;
    if (std::fabs(det) < 1e-12f) {
        return Mat4::identity();
    }
    const float invDet = 1.0f / det;

    // R^-1 (adjugate / det), column-major
    result.m[0] = (b1 * c2 - b2 * c1) * invDet;
    result.m[1] = (a2 * c1 - a1 * c2) * invDet;
    result.m[2] = (a1 * b2 - a2 * b1) * invDet;
    result.m[4] = (b2 * c0 - b0 * c2) * invDet;
    result.m[5] = (a0 * c2 - a2 * c0) * invDet;
    result.m[6] = (a2 * b0 - a0 * b2) * invDet;
    result.m[8] = (b0 * c1 - b1 * c0) * invDet;
    result.m[9] = (a1 * c0 - a0 * c1) * invDet;
    result.m[10] = (a0 * b1 - a1 * b0) * invDet;

    // -R^-1 * t
    const float tx = matrix.m[12];
    const float ty = matrix.m[13];
    const float tz = matrix.m[14];
    result.m[12] = -(result.m[0] * tx + result.m[4] * ty + result.m[8] * tz);
    result.m[13] = -(result.m[1] * tx + result.m[5] * ty + result.m[9] * tz);
    result.m[14] = -(result.m[2] * tx + result.m[6] * ty + result.m[10] * tz);

    result.m[3] = 0.0f;
    result.m[7] = 0.0f;
    result.m[11] = 0.0f;
    result.m[15] = 1.0f;
    return result;
}

Vec3 transformPoint(const Mat4& matrix, const Vec3 value)
{
    return Vec3{
        matrix.m[0] * value.x + matrix.m[4] * value.y + matrix.m[8] * value.z + matrix.m[12],
        matrix.m[1] * value.x + matrix.m[5] * value.y + matrix.m[9] * value.z + matrix.m[13],
        matrix.m[2] * value.x + matrix.m[6] * value.y + matrix.m[10] * value.z + matrix.m[14],
    };
}

Vec3 transformVector(const Mat4& matrix, const Vec3 value)
{
    return Vec3{
        matrix.m[0] * value.x + matrix.m[4] * value.y + matrix.m[8] * value.z,
        matrix.m[1] * value.x + matrix.m[5] * value.y + matrix.m[9] * value.z,
        matrix.m[2] * value.x + matrix.m[6] * value.y + matrix.m[10] * value.z,
    };
}

} // namespace animation
