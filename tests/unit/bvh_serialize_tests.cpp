// BVH GPU serialization unit tests (DDGI Phase 4).
//
// Validates the Bvh::serializeForGpu() method that flattens the CPU-side AABB
// tree into the 32-byte node array consumed by the DDGI compute shader's
// software ray tracer (traceSceneBvh in ddgi.slang). The GPU traversal
// protocol is:
//   - Internal node: leftChild/rightChild are direct child indices into the
//     node array.
//   - Leaf node: leftChild == kInvalidLeafMarker (0xFFFFFFFF), rightChild is
//     the leafStart index into the parallel leaf array.
//   - A parallel leafCounts[] array (one entry per node) carries the leaf
//     count for leaf nodes (0 for internal nodes).
//
// The DDGI renderer builds one leaf per triangle (handle = triangle index),
// so the builder's median-split produces leafCount==1 at every leaf. These
// tests cover: empty tree, single leaf, two leaves, and a larger tree (5
// leaves) to exercise multi-level internal nodes. They also assert the
// std430 size invariant (32 bytes per node) and the kInvalidLeafMarker
// value, which the shader mirrors as a literal constant.

#include "3d/bvh.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using arp::scene3d::Aabb;
using arp::scene3d::Bvh;

// Helper: make a small AABB at a given center so leaves are distinguishable.
Aabb makeAabb(float cx, float cy, float cz, float halfExtent = 0.5f)
{
    return Aabb{
        arp::core::Vec3{cx - halfExtent, cy - halfExtent, cz - halfExtent},
        arp::core::Vec3{cx + halfExtent, cy + halfExtent, cz + halfExtent},
    };
}

// Validate that a serialized node array is internally consistent: every
// internal node's child indices are in range, every leaf node carries the
// marker, and leafCounts parallels the node array.
void validateConsistency(const std::vector<Bvh::GpuBvhNodeData>& nodes,
    const std::vector<std::uint32_t>& leafCounts,
    std::size_t expectedLeafTotal)
{
    assert(nodes.size() == leafCounts.size());
    std::size_t leafNodes = 0;
    std::uint32_t maxLeafStart = 0;
    for (const auto& node : nodes) {
        if (node.leftChild == Bvh::kInvalidLeafMarker) {
            // Leaf node.
            ++leafNodes;
            assert(node.rightChild < expectedLeafTotal); // leafStart in range
            if (node.rightChild > maxLeafStart)
                maxLeafStart = node.rightChild;
        } else {
            // Internal node: both children must be valid indices into nodes[].
            assert(node.leftChild < nodes.size());
            assert(node.rightChild < nodes.size());
            assert(node.leftChild != Bvh::kInvalidLeafMarker);
            assert(node.rightChild != Bvh::kInvalidLeafMarker);
        }
    }
    // The root (index 0) is always present for a non-empty tree.
    assert(!nodes.empty());
    (void)maxLeafStart;
}

void testEmptyTree()
{
    Bvh bvh;
    bvh.build();
    std::vector<std::uint32_t> leafCounts;
    const auto nodes = bvh.serializeForGpu(&leafCounts);
    assert(nodes.empty());
    assert(leafCounts.empty());
    std::cerr << "[ok] bvh_serialize: empty tree -> 0 nodes\n";
}

void testSingleLeaf()
{
    Bvh bvh;
    bvh.insert(makeAabb(0, 0, 0), /*handle=*/42u, /*type=*/0u);
    bvh.build();
    assert(bvh.leafCount() == 1);
    assert(bvh.nodeCount() == 1);

    std::vector<std::uint32_t> leafCounts;
    const auto nodes = bvh.serializeForGpu(&leafCounts);
    assert(nodes.size() == 1);
    assert(leafCounts.size() == 1);

    // The single node is both root and leaf.
    const auto& root = nodes[0];
    assert(root.leftChild == Bvh::kInvalidLeafMarker);
    assert(root.rightChild == 0u); // leafStart = 0
    assert(leafCounts[0] == 1u);

    // AABB must match the inserted leaf.
    const auto& leaves = bvh.leaves();
    assert(leaves.size() == 1);
    assert(leaves[0].handle == 42u);
    assert(root.aabbMin[0] == leaves[0].bounds.min.x);
    assert(root.aabbMax[0] == leaves[0].bounds.max.x);

    std::cerr << "[ok] bvh_serialize: single leaf -> 1 node (leaf), "
                 "leafStart=0, leafCount=1\n";
}

void testTwoLeaves()
{
    Bvh bvh;
    bvh.insert(makeAabb(-2, 0, 0), /*handle=*/0u, /*type=*/0u);
    bvh.insert(makeAabb(+2, 0, 0), /*handle=*/1u, /*type=*/0u);
    bvh.build();
    assert(bvh.leafCount() == 2);
    // Two leaves -> 1 internal root + 2 leaf children = 3 nodes.
    assert(bvh.nodeCount() == 3);

    std::vector<std::uint32_t> leafCounts;
    const auto nodes = bvh.serializeForGpu(&leafCounts);
    assert(nodes.size() == 3);
    assert(leafCounts.size() == 3);

    validateConsistency(nodes, leafCounts, /*expectedLeafTotal=*/2);

    // Root (index 0) is internal: two children, leafCount 0.
    assert(nodes[0].leftChild != Bvh::kInvalidLeafMarker);
    assert(nodes[0].rightChild != Bvh::kInvalidLeafMarker);
    assert(leafCounts[0] == 0u);

    // Both children are leaves with leafCount 1.
    const auto leftIdx = nodes[0].leftChild;
    const auto rightIdx = nodes[0].rightChild;
    assert(leftIdx < nodes.size());
    assert(rightIdx < nodes.size());
    assert(nodes[leftIdx].leftChild == Bvh::kInvalidLeafMarker);
    assert(nodes[rightIdx].leftChild == Bvh::kInvalidLeafMarker);
    assert(leafCounts[leftIdx] == 1u);
    assert(leafCounts[rightIdx] == 1u);

    // Root AABB must encompass both children.
    const auto& rootMin = nodes[0].aabbMin;
    const auto& rootMax = nodes[0].aabbMax;
    assert(rootMin[0] <= -2.5f); // left leaf min x
    assert(rootMax[0] >= +2.5f); // right leaf max x

    std::cerr << "[ok] bvh_serialize: two leaves -> 3 nodes (1 internal + 2 leaf)\n";
}

void testFiveLeavesMultiLevel()
{
    // Five leaves at distinct positions to force a multi-level tree.
    Bvh bvh;
    bvh.insert(makeAabb(-4, 0, 0), /*handle=*/10u, /*type=*/0u);
    bvh.insert(makeAabb(-2, 0, 0), /*handle=*/11u, /*type=*/0u);
    bvh.insert(makeAabb(0, 0, 0), /*handle=*/12u, /*type=*/0u);
    bvh.insert(makeAabb(+2, 0, 0), /*handle=*/13u, /*type=*/0u);
    bvh.insert(makeAabb(+4, 0, 0), /*handle=*/14u, /*type=*/0u);
    bvh.build();
    assert(bvh.leafCount() == 5);

    std::vector<std::uint32_t> leafCounts;
    const auto nodes = bvh.serializeForGpu(&leafCounts);
    // 5 leaves -> 4 internal nodes + 5 leaf nodes = 9 nodes (binary tree).
    assert(nodes.size() == 9);
    assert(leafCounts.size() == 9);

    validateConsistency(nodes, leafCounts, /*expectedLeafTotal=*/5);

    // Count leaf nodes vs internal nodes.
    std::size_t leafNodeCount = 0;
    std::size_t internalNodeCount = 0;
    std::uint32_t totalLeafCount = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].leftChild == Bvh::kInvalidLeafMarker) {
            ++leafNodeCount;
            assert(leafCounts[i] == 1u); // builder splits to count <= 1
            totalLeafCount += leafCounts[i];
        } else {
            ++internalNodeCount;
            assert(leafCounts[i] == 0u);
        }
    }
    assert(leafNodeCount == 5);
    assert(internalNodeCount == 4);
    assert(totalLeafCount == 5);

    // Root AABB must encompass all 5 leaves (-4.5 to +4.5 in x).
    assert(nodes[0].aabbMin[0] <= -4.5f);
    assert(nodes[0].aabbMax[0] >= +4.5f);

    std::cerr << "[ok] bvh_serialize: five leaves -> 9 nodes (4 internal + 5 leaf), "
                 "all leaves have leafCount=1\n";
}

void testSizeAndMarkerInvariants()
{
    // The shader hardcodes the 32-byte node size and the 0xFFFFFFFF marker
    // as literal constants. Assert the C++ side stays in sync.
    static_assert(
        sizeof(Bvh::GpuBvhNodeData) == 32, "GpuBvhNodeData must be 32 bytes (std430 layout)");
    assert(Bvh::kInvalidLeafMarker == 0xFFFFFFFFu);

    // AABB fields are packed as float[3] + uint (no padding in the struct,
    // but std430 on the GPU side aligns float3 to 16 bytes - the 32-byte
    // total holds either way because 2 * (12 + 4) = 32).
    Bvh::GpuBvhNodeData node{};
    node.aabbMin[0] = 1.0f;
    node.aabbMin[1] = 2.0f;
    node.aabbMin[2] = 3.0f;
    node.leftChild = Bvh::kInvalidLeafMarker;
    node.aabbMax[0] = 4.0f;
    node.aabbMax[1] = 5.0f;
    node.aabbMax[2] = 6.0f;
    node.rightChild = 7u;
    assert(node.leftChild == 0xFFFFFFFFu);

    std::cerr << "[ok] bvh_serialize: sizeof(GpuBvhNodeData)=32, "
                 "kInvalidLeafMarker=0xFFFFFFFF\n";
}

void testLeafAccessorPreservesHandles()
{
    // After build(), leaves() returns the reordered array. The DDGI renderer
    // packs triangle data into GpuBvhLeafData[] indexed by this reordered
    // position, so the handle->index mapping must be recoverable.
    Bvh bvh;
    bvh.insert(makeAabb(0, 0, 0), /*handle=*/100u, /*type=*/0u);
    bvh.insert(makeAabb(5, 0, 0), /*handle=*/200u, /*type=*/0u);
    bvh.insert(makeAabb(10, 0, 0), /*handle=*/300u, /*type=*/0u);
    bvh.build();

    const auto& leaves = bvh.leaves();
    assert(leaves.size() == 3);

    // All three handles must be present (order may differ from insertion).
    std::vector<std::uint32_t> handles;
    for (const auto& leaf : leaves)
        handles.push_back(leaf.handle);
    std::sort(handles.begin(), handles.end());
    assert(handles[0] == 100u);
    assert(handles[1] == 200u);
    assert(handles[2] == 300u);

    std::cerr << "[ok] bvh_serialize: leaves() preserves all handles after reorder\n";
}

void testRefitPreservesTopologyAndUpdatesBounds()
{
    Bvh bvh;
    bvh.insert(makeAabb(-2, 0, 0), 0u, 0u);
    bvh.insert(makeAabb(+2, 0, 0), 1u, 0u);
    bvh.build();
    const auto before = bvh.serializeForGpu();

    const std::array<Aabb, 2> moved{
        makeAabb(10, 1, 0),
        makeAabb(14, 1, 0),
    };
    assert(bvh.refit(moved.data(), moved.size()));
    const auto after = bvh.serializeForGpu();
    assert(after.size() == before.size());
    for (std::size_t i = 0; i < after.size(); ++i) {
        assert(after[i].leftChild == before[i].leftChild);
        assert(after[i].rightChild == before[i].rightChild);
    }
    assert(bvh.rootBounds().min.x >= 9.0f);
    assert(bvh.rootBounds().max.x <= 15.0f);
    std::cerr << "[ok] bvh_refit: topology preserved and bounds updated\n";
}

} // namespace

int main()
{
    testEmptyTree();
    testSingleLeaf();
    testTwoLeaves();
    testFiveLeavesMultiLevel();
    testSizeAndMarkerInvariants();
    testLeafAccessorPreservesHandles();
    testRefitPreservesTopologyAndUpdatesBounds();
    std::cerr << "[ok] bvh_serialize: all BVH GPU serialization tests passed\n";
    return 0;
}
