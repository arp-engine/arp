#pragma once

// Concrete BvhQuery and BvhQuerySink implementations.
//
// These are the built-in query types for the BVH:
//   - FrustumQuery: camera frustum culling (uses core::Frustum)
//   - SphereQuery:  spherical region query (audio, particle pickup, etc.)
//   - CollectSink:  collects all visible leaves into a vector
//
// Custom queries can be created by subclassing BvhQuery directly; the
// BVH traversal logic is agnostic to the query type.

#include "3d/bvh.h"
#include "core/math/frustum.h"

#include <vector>

namespace arp::scene3d {

// ── Frustum query ───────────────────────────────────────
// Queries the BVH for all leaves that intersect the camera frustum.
// Node-level pruning uses the fast p-vertex AABB test; leaf-level
// testing defaults to the same AABB test (sufficient for chunk-sized
// leaves). Override intersectsLeaf for tighter per-leaf tests.
class FrustumQuery final : public BvhQuery {
public:
    explicit FrustumQuery(const core::Frustum& frustum) : frustum_(frustum) {}

    bool intersectsAabb(const Aabb& aabb) const override
    {
        return frustum_.intersectsAabbMinMax(aabb.min, aabb.max);
    }

private:
    const core::Frustum& frustum_;
};

// ── Sphere query ────────────────────────────────────────
// Queries the BVH for all leaves whose AABB intersects a sphere.
// Useful for audio sources, particle pickup ranges, area-of-effect
// spells, etc.
class SphereQuery final : public BvhQuery {
public:
    SphereQuery(arp::core::Vec3 center, float radius) : center_(center), radiusSq_(radius * radius)
    {}

    bool intersectsAabb(const Aabb& aabb) const override
    {
        // Compute the closest point on the AABB to the sphere center.
        // If the squared distance is <= radiusSq, they intersect.
        float d2 = 0.0f;
        d2 += squaredDistanceRange(aabb.min.x, aabb.max.x, center_.x);
        d2 += squaredDistanceRange(aabb.min.y, aabb.max.y, center_.y);
        d2 += squaredDistanceRange(aabb.min.z, aabb.max.z, center_.z);
        return d2 <= radiusSq_;
    }

private:
    arp::core::Vec3 center_;
    float radiusSq_;

    static float squaredDistanceRange(float lo, float hi, float v)
    {
        if (v < lo)
            return (lo - v) * (lo - v);
        if (v > hi)
            return (v - hi) * (v - hi);
        return 0.0f; // v is inside [lo, hi]
    }
};

// ── Collect sink ────────────────────────────────────────
// Collects all accepted leaves into a vector. The most common sink
// for render-payload construction: after query(), iterate leaves()
// to fill the draw payload with only visible items.
class CollectSink final : public BvhQuerySink {
public:
    void accept(const BvhLeaf& leaf) override { leaves_.push_back(leaf); }

    const std::vector<BvhLeaf>& leaves() const { return leaves_; }

    std::vector<BvhLeaf>& leaves() { return leaves_; }

    void clear() { leaves_.clear(); }

    bool empty() const { return leaves_.empty(); }

    std::size_t size() const { return leaves_.size(); }

private:
    std::vector<BvhLeaf> leaves_;
};

} // namespace arp::scene3d
