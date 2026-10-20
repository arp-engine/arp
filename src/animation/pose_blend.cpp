#include "animation/pose_blend.h"

#include "core/cpu_allocator.h"
#include "core/math.h"

#include <algorithm>
#include <cmath>

namespace animation {

namespace {

inline const arp::core::Quat& asCore(const Quat& q)
{
    return reinterpret_cast<const arp::core::Quat&>(q);
}

inline Quat asAnim(const arp::core::Quat& q)
{
    Quat r;
    r.x = q.x;
    r.y = q.y;
    r.z = q.z;
    r.w = q.w;
    return r;
}

float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// nlerp for animation::Quat via core::math (shortest-arc).
Quat nlerpQuat(const Quat& a, const Quat& b, float t)
{
    auto ca = asCore(a);
    auto cb = asCore(b);
    if (arp::core::dot(ca, cb) < 0.0f) {
        cb = arp::core::Quat{-cb.x, -cb.y, -cb.z, -cb.w};
    }
    const auto blended = arp::core::normalize(arp::core::Quat{
        ca.x + (cb.x - ca.x) * t,
        ca.y + (cb.y - ca.y) * t,
        ca.z + (cb.z - ca.z) * t,
        ca.w + (cb.w - ca.w) * t,
    });
    return asAnim(blended);
}

Vec3 lerpVec3(const Vec3& a, const Vec3& b, float t)
{
    return Vec3{
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

} // namespace

bool blendPoses(const std::pmr::vector<LocalTransform>& a,
    const std::pmr::vector<LocalTransform>& b,
    const float t,
    std::pmr::vector<LocalTransform>& out)
{
    if (a.size() != b.size()) {
        return false;
    }
    const auto amount = clamp01(t);
    if (out.size() != a.size()) {
        out.resize(a.size());
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        out[i].translation = lerpVec3(a[i].translation, b[i].translation, amount);
        out[i].rotation = nlerpQuat(a[i].rotation, b[i].rotation, amount);
        out[i].scale = lerpVec3(a[i].scale, b[i].scale, amount);
    }
    return true;
}

bool blendPosesWeighted(const std::pmr::vector<const std::pmr::vector<LocalTransform>*>& poses,
    const std::pmr::vector<float>& weights,
    std::pmr::vector<LocalTransform>& out)
{
    if (poses.empty() || poses.size() != weights.size()) {
        return false;
    }
    const auto jointCount = poses.front()->size();
    for (const auto* pose : poses) {
        if (pose->size() != jointCount) {
            return false;
        }
    }

    float weightSum = 0.0f;
    for (const auto w : weights) {
        if (w > 0.0f) {
            weightSum += w;
        }
    }
    if (weightSum <= 0.0f) {
        // No contribution: leave caller's out as-is.
        return true;
    }
    const float invSum = 1.0f / weightSum;

    if (out.size() != jointCount) {
        out.resize(jointCount);
    }
    for (std::size_t j = 0; j < jointCount; ++j) {
        Vec3 translation{};
        Vec3 scale{};
        // Accumulate rotations as weighted nlerp: start from the first
        // positive-weight pose, then nlerp toward each subsequent one with
        // a renormalised partial weight. This is the standard lightweight
        // multi-pose blend (accurate enough for BlendTree 1D where at most
        // two nodes have non-zero weight).
        bool first = true;
        Quat accRot = Quat::identity();
        float accWeight = 0.0f;
        for (std::size_t p = 0; p < poses.size(); ++p) {
            const float w = weights[p] > 0.0f ? weights[p] * invSum : 0.0f;
            if (w <= 0.0f) {
                continue;
            }
            const auto& joint = (*poses[p])[j];
            translation.x += joint.translation.x * w;
            translation.y += joint.translation.y * w;
            translation.z += joint.translation.z * w;
            scale.x += joint.scale.x * w;
            scale.y += joint.scale.y * w;
            scale.z += joint.scale.z * w;
            if (first) {
                accRot = joint.rotation;
                accWeight = w;
                first = false;
            } else {
                const float partial = w / (accWeight + w);
                accRot = nlerpQuat(accRot, joint.rotation, partial);
                accWeight += w;
            }
        }
        out[j].translation = translation;
        out[j].rotation = first ? Quat::identity() : accRot;
        out[j].scale = scale;
    }
    return true;
}

} // namespace animation
