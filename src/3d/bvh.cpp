#include "3d/bvh.h"

#include <algorithm>
#include <cmath>

namespace arp::scene3d {
namespace {

// Select the axis (0=x, 1=y, 2=z) with the largest extent among leaves
// in the range [begin, end). This is the axis along which we split.
int longestAxis(const std::vector<BvhLeaf>& leaves, std::uint32_t begin, std::uint32_t end)
{
    Aabb box = leaves[begin].bounds;
    for (std::uint32_t i = begin + 1; i < end; ++i) {
        box = Aabb::merge(box, leaves[i].bounds);
    }
    const float dx = box.max.x - box.min.x;
    const float dy = box.max.y - box.min.y;
    const float dz = box.max.z - box.min.z;
    if (dx >= dy && dx >= dz)
        return 0;
    if (dy >= dz)
        return 1;
    return 2;
}

} // namespace

void Bvh::build()
{
    nodes_.clear();
    leaves_.clear();

    if (pendingLeaves_.empty())
        return;

    // Move pending leaves into the build array (will be reordered by sorting).
    leaves_.swap(pendingLeaves_);
    pendingLeaves_.clear();

    // Reserve node array (a binary tree with n leaves has n-1 internal nodes,
    // so n + n-1 = 2n-1 total nodes; reserve a bit extra to avoid realloc).
    nodes_.reserve(leaves_.size() * 2);

    buildRecursive(0, static_cast<std::uint32_t>(leaves_.size()));
}

bool Bvh::refit(const Aabb* boundsByHandle, std::size_t boundCount)
{
    if (nodes_.empty() || leaves_.empty() || boundsByHandle == nullptr)
        return false;
    for (const auto& leaf : leaves_) {
        if (leaf.handle >= boundCount)
            return false;
    }
    for (auto& leaf : leaves_) {
        leaf.bounds = boundsByHandle[leaf.handle];
    }
    refitRecursive(0u);
    return true;
}

Aabb Bvh::refitRecursive(std::uint32_t nodeIndex)
{
    auto& node = nodes_[nodeIndex];
    if (node.leftChild == kInvalid) {
        Aabb bounds = leaves_[node.leafStart].bounds;
        for (std::uint32_t i = 1u; i < node.leafCount; ++i) {
            bounds = Aabb::merge(bounds, leaves_[node.leafStart + i].bounds);
        }
        node.bounds = bounds;
        return bounds;
    }
    node.bounds = Aabb::merge(refitRecursive(node.leftChild), refitRecursive(node.rightChild));
    return node.bounds;
}

std::uint32_t Bvh::buildRecursive(std::uint32_t begin, std::uint32_t end)
{
    const std::uint32_t count = end - begin;

    // Compute bounds of all leaves in this range.
    Aabb bounds = leaves_[begin].bounds;
    for (std::uint32_t i = begin + 1; i < end; ++i) {
        bounds = Aabb::merge(bounds, leaves_[i].bounds);
    }

    const std::uint32_t nodeIndex = static_cast<std::uint32_t>(nodes_.size());
    nodes_.push_back(Node{});

    if (count <= 1) {
        // Leaf node: store the single leaf reference.
        nodes_[nodeIndex].bounds = bounds;
        nodes_[nodeIndex].leftChild = kInvalid;
        nodes_[nodeIndex].rightChild = kInvalid;
        nodes_[nodeIndex].leafStart = begin;
        nodes_[nodeIndex].leafCount = count;
        return nodeIndex;
    }

    // Internal node: split by median along the longest axis.
    const int axis = longestAxis(leaves_, begin, end);
    const std::uint32_t mid = begin + count / 2;

    // Partial sort: partition around the median so that leaves [begin, mid)
    // are all "left" and [mid, end) are all "right" along the chosen axis.
    std::nth_element(leaves_.begin() + begin,
        leaves_.begin() + mid,
        leaves_.begin() + end,
        [axis](const BvhLeaf& a, const BvhLeaf& b) {
            const auto ca = a.bounds.center();
            const auto cb = b.bounds.center();
            const float fa = (axis == 0) ? ca.x : (axis == 1) ? ca.y : ca.z;
            const float fb = (axis == 0) ? cb.x : (axis == 1) ? cb.y : cb.z;
            return fa < fb;
        });

    // Recursively build left and right children. The left child is built
    // first (pre-order) and gets the next node index; the right child
    // follows after the entire left subtree.
    const std::uint32_t leftIndex = buildRecursive(begin, mid);
    const std::uint32_t rightIndex = buildRecursive(mid, end);

    // Fill in the internal node. Both child indices are stored explicitly
    // because a pre-order build does NOT place the right child at
    // leftChild + 1 (the left subtree may contain many internal nodes).
    nodes_[nodeIndex].bounds = bounds;
    nodes_[nodeIndex].leftChild = leftIndex;
    nodes_[nodeIndex].rightChild = rightIndex;
    nodes_[nodeIndex].leafStart = 0;
    nodes_[nodeIndex].leafCount = 0;

    return nodeIndex;
}

void Bvh::query(const BvhQuery& query, BvhQuerySink& sink) const
{
    if (nodes_.empty())
        return;
    queryRecursive(0, query, sink);
}

void Bvh::queryRecursive(std::uint32_t nodeIndex, const BvhQuery& query, BvhQuerySink& sink) const
{
    const Node& node = nodes_[nodeIndex];

    // Prune: if the node's AABB is entirely outside the query region,
    // skip the entire subtree. This is the key O(log n) optimization.
    if (!query.intersectsAabb(node.bounds))
        return;

    if (node.leftChild == kInvalid) {
        // Leaf node: test each leaf individually.
        for (std::uint32_t i = 0; i < node.leafCount; ++i) {
            const BvhLeaf& leaf = leaves_[node.leafStart + i];
            if (query.intersectsLeaf(leaf)) {
                sink.accept(leaf);
            }
        }
    } else {
        // Internal node: recurse into both children.
        queryRecursive(node.leftChild, query, sink);
        queryRecursive(node.rightChild, query, sink);
    }
}

std::vector<Bvh::GpuBvhNodeData> Bvh::serializeForGpu(
    std::vector<std::uint32_t>* leafCountsOut) const
{
    std::vector<GpuBvhNodeData> out;
    out.reserve(nodes_.size());
    if (leafCountsOut) {
        leafCountsOut->clear();
        leafCountsOut->reserve(nodes_.size());
    }

    for (const Node& node : nodes_) {
        GpuBvhNodeData gpu{};
        gpu.aabbMin[0] = node.bounds.min.x;
        gpu.aabbMin[1] = node.bounds.min.y;
        gpu.aabbMin[2] = node.bounds.min.z;
        gpu.aabbMax[0] = node.bounds.max.x;
        gpu.aabbMax[1] = node.bounds.max.y;
        gpu.aabbMax[2] = node.bounds.max.z;

        if (node.leftChild == kInvalid) {
            // Leaf node: signal via the marker, and encode the leaf range in
            // rightChild (= leafStart). The leaf count goes into the parallel
            // array (kept separate to preserve the 32-byte node size).
            gpu.leftChild = kInvalidLeafMarker;
            gpu.rightChild = node.leafStart;
        } else {
            // Internal node: store both child indices directly.
            gpu.leftChild = node.leftChild;
            gpu.rightChild = node.rightChild;
        }
        out.push_back(gpu);
        if (leafCountsOut) {
            leafCountsOut->push_back(node.leafCount);
        }
    }
    return out;
}

} // namespace arp::scene3d
