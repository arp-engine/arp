#include "3d/dynamic_aabb_tree.h"

#include "core/diagnostics/diagnostic_error.h"

#include <algorithm>
#include <cmath>

namespace arp::scene3d {
namespace {
double area(const Aabb& b)
{
    const double x = double(b.max.x) - b.min.x, y = double(b.max.y) - b.min.y,
                 z = double(b.max.z) - b.min.z;
    return 2 * (x * y + x * z + y * z);
}

bool equal(const Aabb& a, const Aabb& b)
{
    return a.min.x == b.min.x && a.min.y == b.min.y && a.min.z == b.min.z && a.max.x == b.max.x
        && a.max.y == b.max.y && a.max.z == b.max.z;
}

void validate(const Aabb& b)
{
    if (!std::isfinite(b.min.x) || !std::isfinite(b.min.y) || !std::isfinite(b.min.z)
        || !std::isfinite(b.max.x) || !std::isfinite(b.max.y) || !std::isfinite(b.max.z)
        || b.min.x > b.max.x || b.min.y > b.max.y || b.min.z > b.max.z)
        throw core::DiagnosticError("SCENE3D-SPATIAL-BOUNDS-INVALID");
}
} // namespace

void DynamicAabbTree::reserve(std::size_t leaves)
{
    if (leaves > (UINT32_MAX - 1) / 2)
        throw core::DiagnosticError("SCENE3D-SPATIAL-CAPACITY");
    nodes_.reserve(leaves * 2);
}

std::uint32_t DynamicAabbTree::allocate()
{
    if (free_ != invalid) {
        const auto node = free_;
        free_ = nodes_[node].left;
        nodes_[node] = Node{};
        return node;
    }
    if (nodes_.size() >= invalid)
        throw core::DiagnosticError("SCENE3D-SPATIAL-CAPACITY");
    nodes_.push_back({});
    return std::uint32_t(nodes_.size() - 1);
}

void DynamicAabbTree::release(std::uint32_t node)
{
    nodes_[node].height = -1;
    nodes_[node].left = free_;
    free_ = node;
}

std::uint32_t DynamicAabbTree::insert(const BvhLeaf& leaf)
{
    validate(leaf.bounds);
    const auto proxy = allocate();
    nodes_[proxy].leaf = leaf;
    insertLeaf(proxy);
    ++leaves_;
    return proxy;
}

void DynamicAabbTree::remove(std::uint32_t proxy)
{
    if (proxy >= nodes_.size() || nodes_[proxy].height != 0)
        throw core::DiagnosticError("SCENE3D-SPATIAL-PROXY-INVALID");
    detachLeaf(proxy);
    release(proxy);
    --leaves_;
}

bool DynamicAabbTree::update(std::uint32_t proxy, const Aabb& bounds)
{
    validate(bounds);
    if (proxy >= nodes_.size() || nodes_[proxy].height != 0)
        throw core::DiagnosticError("SCENE3D-SPATIAL-PROXY-INVALID");
    if (equal(nodes_[proxy].leaf.bounds, bounds))
        return false;
    detachLeaf(proxy);
    nodes_[proxy].leaf.bounds = bounds;
    insertLeaf(proxy);
    return true;
}

void DynamicAabbTree::insertLeaf(std::uint32_t leaf)
{
    if (root_ == invalid) {
        root_ = leaf;
        nodes_[leaf].parent = invalid;
        return;
    }
    const auto box = nodes_[leaf].leaf.bounds;
    auto sibling = root_;
    while (nodes_[sibling].left != invalid) {
        const auto& n = nodes_[sibling];
        const auto merged = area(Aabb::merge(n.leaf.bounds, box));
        const auto inherited = 2 * (merged - area(n.leaf.bounds));
        const auto cost = [&](auto child) {
            const auto& c = nodes_[child];
            const auto total = area(Aabb::merge(c.leaf.bounds, box));
            return inherited + (c.left == invalid ? total : total - area(c.leaf.bounds));
        };
        const auto leftCost = cost(n.left), rightCost = cost(n.right);
        if (2 * merged < leftCost && 2 * merged < rightCost)
            break;
        sibling = leftCost < rightCost ? n.left : n.right;
    }
    const auto parent = nodes_[sibling].parent;
    const auto replacement = allocate();
    auto& n = nodes_[replacement];
    n.parent = parent;
    n.left = sibling;
    n.right = leaf;
    n.height = nodes_[sibling].height + 1;
    n.leaf.bounds = Aabb::merge(nodes_[sibling].leaf.bounds, box);
    nodes_[sibling].parent = replacement;
    nodes_[leaf].parent = replacement;
    if (parent == invalid)
        root_ = replacement;
    else if (nodes_[parent].left == sibling)
        nodes_[parent].left = replacement;
    else
        nodes_[parent].right = replacement;
    refit(replacement);
}

void DynamicAabbTree::detachLeaf(std::uint32_t leaf)
{
    if (root_ == leaf) {
        root_ = invalid;
        return;
    }
    const auto parent = nodes_[leaf].parent;
    const auto grandparent = nodes_[parent].parent;
    const auto sibling = nodes_[parent].left == leaf ? nodes_[parent].right : nodes_[parent].left;
    if (grandparent == invalid) {
        root_ = sibling;
        nodes_[sibling].parent = invalid;
    } else {
        if (nodes_[grandparent].left == parent)
            nodes_[grandparent].left = sibling;
        else
            nodes_[grandparent].right = sibling;
        nodes_[sibling].parent = grandparent;
    }
    release(parent);
    nodes_[leaf].parent = invalid;
    refit(grandparent);
}

void DynamicAabbTree::refit(std::uint32_t node)
{
    while (node != invalid) {
        node = balance(node);
        auto& n = nodes_[node];
        if (n.left != invalid) {
            n.leaf.bounds = Aabb::merge(nodes_[n.left].leaf.bounds, nodes_[n.right].leaf.bounds);
            n.height = 1 + std::max(nodes_[n.left].height, nodes_[n.right].height);
        }
        node = n.parent;
    }
}

std::uint32_t DynamicAabbTree::balance(std::uint32_t index)
{
    auto& a = nodes_[index];
    if (a.left == invalid || a.height < 2)
        return index;
    const int skew = nodes_[a.right].height - nodes_[a.left].height;
    if (std::abs(skew) <= 1)
        return index;
    const bool rightHeavy = skew > 1;
    const auto raised = rightHeavy ? a.right : a.left;
    auto& b = nodes_[raised];
    const auto oldParent = a.parent;
    b.parent = oldParent;
    if (oldParent == invalid)
        root_ = raised;
    else if (nodes_[oldParent].left == index)
        nodes_[oldParent].left = raised;
    else
        nodes_[oldParent].right = raised;
    a.parent = raised;
    const auto near = rightHeavy ? b.left : b.right;
    const auto far = rightHeavy ? b.right : b.left;
    const auto retained = nodes_[near].height > nodes_[far].height ? near : far;
    const auto transferred = retained == near ? far : near;
    if (rightHeavy) {
        b.left = index;
        b.right = retained;
        a.right = transferred;
    } else {
        b.right = index;
        b.left = retained;
        a.left = transferred;
    }
    nodes_[retained].parent = raised;
    nodes_[transferred].parent = index;
    a.leaf.bounds = Aabb::merge(nodes_[a.left].leaf.bounds, nodes_[a.right].leaf.bounds);
    a.height = 1 + std::max(nodes_[a.left].height, nodes_[a.right].height);
    b.leaf.bounds = Aabb::merge(nodes_[b.left].leaf.bounds, nodes_[b.right].leaf.bounds);
    b.height = 1 + std::max(nodes_[b.left].height, nodes_[b.right].height);
    return raised;
}

void DynamicAabbTree::query(const BvhQuery& query, BvhQuerySink& sink) const
{
    if (root_ != invalid)
        queryNode(root_, query, sink);
}

void DynamicAabbTree::queryNode(
    std::uint32_t index, const BvhQuery& query, BvhQuerySink& sink) const
{
    const auto& n = nodes_[index];
    if (!query.intersectsAabb(n.leaf.bounds))
        return;
    if (n.left == invalid) {
        if (query.intersectsLeaf(n.leaf))
            sink.accept(n.leaf);
    } else {
        queryNode(n.left, query, sink);
        queryNode(n.right, query, sink);
    }
}
} // namespace arp::scene3d
