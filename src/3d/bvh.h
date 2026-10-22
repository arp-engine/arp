#pragma once

// Bounding Volume Hierarchy (BVH) with a pluggable query abstraction.
//
// This header provides a lightweight AABB tree for spatial-accelerated
// visibility queries. The design follows the engine's established
// abstract-base-class + accept() convention (mirroring RenderExtractSink):
//
//   - `BvhQuery` is an abstract query. The BVH calls `intersectsAabb()`
//     to prune whole subtrees and `intersectsLeaf()` for per-leaf tests.
//     Concrete implementations (`FrustumQuery`, `SphereQuery`, ...) live
//     in `3d/bvh_queries.h`.
//   - `BvhQuerySink` is an abstract result collector. The BVH calls
//     `accept()` for each leaf that passes the query test. Concrete
//     sinks (`CollectSink`, ...) collect results however the caller wants.
//   - `Bvh` is the tree itself. It is built via `insert()` + `build()`
//     (median-split, bottom-up) and queried via `query()`.
//
// Layering: this header depends only on `arp_core` (Vec3). Query
// implementations that need Frustum / Aabb interop include additional
// headers, keeping the base layer minimal.

#include "core/math/math.h"

#include <cstdint>
#include <vector>

namespace arp::scene3d {

// ── AABB primitive ──────────────────────────────────────
// Axis-aligned bounding box stored as min/max corners.
struct Aabb {
    arp::core::Vec3 min;
    arp::core::Vec3 max;

    static Aabb fromCenterExtents(arp::core::Vec3 center, arp::core::Vec3 extents)
    {
        return Aabb{
            arp::core::Vec3{center.x - extents.x, center.y - extents.y, center.z - extents.z},
            arp::core::Vec3{center.x + extents.x, center.y + extents.y, center.z + extents.z},
        };
    }

    static Aabb merge(const Aabb& a, const Aabb& b)
    {
        return Aabb{
            arp::core::Vec3{
                (a.min.x < b.min.x) ? a.min.x : b.min.x,
                (a.min.y < b.min.y) ? a.min.y : b.min.y,
                (a.min.z < b.min.z) ? a.min.z : b.min.z,
            },
            arp::core::Vec3{
                (a.max.x > b.max.x) ? a.max.x : b.max.x,
                (a.max.y > b.max.y) ? a.max.y : b.max.y,
                (a.max.z > b.max.z) ? a.max.z : b.max.z,
            },
        };
    }

    arp::core::Vec3 center() const
    {
        return arp::core::Vec3{
            (min.x + max.x) * 0.5f,
            (min.y + max.y) * 0.5f,
            (min.z + max.z) * 0.5f,
        };
    }

    arp::core::Vec3 halfExtents() const
    {
        return arp::core::Vec3{
            (max.x - min.x) * 0.5f,
            (max.y - min.y) * 0.5f,
            (max.z - min.z) * 0.5f,
        };
    }
};

// ── BVH leaf item ──────────────────────────────────────
// A leaf carries its world-space AABB plus an opaque user handle and a
// user-defined type tag. The BVH does not interpret handle/type; the
// caller uses them to route results (e.g. 0=terrain chunk, 1=scene object).
struct BvhLeaf {
    Aabb bounds;
    std::uint32_t handle = 0;
    std::uint32_t type = 0;
};

// ── Query abstraction ──────────────────────────────────
// Abstract base for spatial queries against the BVH. The tree traversal
// calls `intersectsAabb()` on each internal node to decide whether to
// descend; if the node's AABB is entirely outside the query region, the
// whole subtree is skipped (O(log n + k) instead of O(n)).
//
// `intersectsLeaf()` defaults to the same AABB test. Subclasses may
// override it for a more precise per-leaf test (e.g. bounding-sphere
// test against a frustum) without changing the node-level pruning.
class BvhQuery {
public:
    virtual ~BvhQuery() = default;

    // Node-level test: return false if the AABB is entirely outside the
    // query region, causing the entire subtree to be pruned.
    virtual bool intersectsAabb(const Aabb& aabb) const = 0;

    // Leaf-level test: return false if the leaf should not be reported.
    // Default delegates to the AABB test.
    virtual bool intersectsLeaf(const BvhLeaf& leaf) const { return intersectsAabb(leaf.bounds); }
};

// ── Query result sink ──────────────────────────────────
// Abstract base for collecting query results. Called once per visible
// leaf during traversal. Implementations may collect to a vector, count,
// early-exit, etc.
class BvhQuerySink {
public:
    virtual ~BvhQuerySink() = default;
    virtual void accept(const BvhLeaf& leaf) = 0;
};

// ── BVH tree ───────────────────────────────────────────
// A flat-array binary AABB tree. Nodes are stored contiguously for
// cache-friendly traversal. Leaves are reordered during build so that
// each leaf node references a contiguous range in the leaf array.
class Bvh {
public:
    /// Remove all leaves and nodes (return to empty state).
    void clear()
    {
        pendingLeaves_.clear();
        nodes_.clear();
        leaves_.clear();
    }

    /// Add a leaf to the pending set. Must be called before build().
    void insert(Aabb bounds, std::uint32_t handle, std::uint32_t type)
    {
        pendingLeaves_.push_back(BvhLeaf{bounds, handle, type});
    }

    /// Build the tree from all inserted leaves (median-split, bottom-up).
    /// After this call, pendingLeaves_ is consumed into leaves_ (reordered
    /// by tree traversal order) and nodes_ holds the internal structure.
    void build();

    /// Update leaf bounds without changing the existing tree topology.
    /// `boundsByHandle[leaf.handle]` supplies the new bound for each leaf.
    /// Returns false when the tree is empty or a handle is out of range.
    bool refit(const Aabb* boundsByHandle, std::size_t boundCount);

    /// Query the tree: traverse nodes, prune subtrees whose AABB fails
    /// query.intersectsAabb(), and call sink.accept() for each leaf that
    /// passes query.intersectsLeaf().
    void query(const BvhQuery& query, BvhQuerySink& sink) const;

    std::size_t leafCount() const { return leaves_.size(); }

    std::size_t nodeCount() const { return nodes_.size(); }

    /// Returns the root node's AABB, or a default (min=max=0) if empty.
    /// Useful for diagnostics and external culling-validation tests.
    Aabb rootBounds() const
    {
        if (nodes_.empty())
            return Aabb{};
        return nodes_.front().bounds;
    }

    // ── GPU serialization (DDGI Phase 4 software BVH ray query) ────────
    // The compute-shader traversal expects a flat array of 32-byte nodes in
    // std430 layout. Each node carries its AABB (min/max as float3 + a uint
    // tag) and either two child indices (internal) or a leaf range
    // (leaf: leftChild == kInvalidLeafMarker, rightChild encodes leafStart,
    // leafCount stored separately in a parallel array to keep the node at
    // 32 bytes). The marker is exposed as a public constant so callers (and
    // shader code generators) can stay in sync with the encoding.
    //
    // Layout per node (32 bytes, std430):
    //   float3 aabbMin; uint  leftChild;     // internal: left child index
    //                                          leaf:     kInvalidLeafMarker
    //   float3 aabbMax; uint  rightChild;    // internal: right child index
    //                                          leaf:     leafStart in leaves_
    // Parallel array (4 bytes per node):
    //   uint   leafCount;                    // internal: 0; leaf: #leaves
    struct GpuBvhNodeData {
        float aabbMin[3];
        std::uint32_t leftChild;
        float aabbMax[3];
        std::uint32_t rightChild;
    };

    static_assert(sizeof(GpuBvhNodeData) == 32, "GpuBvhNodeData must be 32 bytes (std430 layout)");

    /// Marker stored in GpuBvhNodeData::leftChild to distinguish leaf nodes
    /// from internal nodes during GPU traversal. Mirrors the internal
    /// `kInvalid` sentinel. Exposed publicly so shader code and tests can
    /// reference the same value.
    static constexpr std::uint32_t kInvalidLeafMarker = 0xFFFFFFFFu;

    /// Serialize the built tree into a flat node array suitable for upload
    /// to a GPU SSBO. Must be called only after build(). Returns an empty
    /// vector if the tree is empty. The parallel leaf-count array is
    /// returned via `leafCountsOut` (resized to nodeCount(); one entry per
    /// node, 0 for internal nodes).
    std::vector<GpuBvhNodeData> serializeForGpu(
        std::vector<std::uint32_t>* leafCountsOut = nullptr) const;

    /// Read-only access to the reordered leaf array. After build(), leaves
    /// are reordered so that each leaf node references a contiguous range;
    /// callers that need to match GPU leaf indices to original handles use
    /// this accessor. The returned reference is invalidated by clear(),
    /// build(), or destruction.
    const std::vector<BvhLeaf>& leaves() const { return leaves_; }

private:
    struct Node {
        Aabb bounds;
        std::uint32_t leftChild = kInvalid;  // internal: left child index; leaf: kInvalid
        std::uint32_t rightChild = kInvalid; // internal: right child index; leaf: kInvalid
        std::uint32_t leafStart = 0;         // leaf: start index in leaves_
        std::uint32_t leafCount = 0;         // leaf: number of leaves; internal: 0
    };

    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;

    std::vector<BvhLeaf> pendingLeaves_; // cleared by build()
    std::vector<Node> nodes_;            // built by build()
    std::vector<BvhLeaf> leaves_;        // built by build() (reordered)

    std::uint32_t buildRecursive(std::uint32_t begin, std::uint32_t end);
    Aabb refitRecursive(std::uint32_t nodeIndex);
    void queryRecursive(std::uint32_t nodeIndex, const BvhQuery& query, BvhQuerySink& sink) const;
};

} // namespace arp::scene3d
