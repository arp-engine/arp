#pragma once

#include "3d/bvh.h"
#include "core/memory/cpu_allocator.h"

namespace arp::scene3d {
// Incremental, height-balanced AABB tree. Handles are internal node slots;
// scene lifetime/generation checks belong to Scene3DSpatialIndex.
class DynamicAabbTree final {
public:
    static constexpr std::uint32_t invalid = UINT32_MAX;
    void reserve(std::size_t leaves);
    std::uint32_t insert(const BvhLeaf& leaf);
    void remove(std::uint32_t proxy);
    bool update(std::uint32_t proxy, const Aabb& bounds);
    void query(const BvhQuery& query, BvhQuerySink& sink) const;

    std::size_t leafCount() const { return leaves_; }

    int height() const { return root_ == invalid ? 0 : nodes_[root_].height; }

private:
    struct Node {
        BvhLeaf leaf;
        std::uint32_t parent = invalid, left = invalid, right = invalid;
        int height = 0;
    };

    std::uint32_t allocate();
    void release(std::uint32_t node);
    void insertLeaf(std::uint32_t leaf);
    void detachLeaf(std::uint32_t leaf);
    std::uint32_t balance(std::uint32_t node);
    void refit(std::uint32_t node);
    void queryNode(std::uint32_t node, const BvhQuery& query, BvhQuerySink& sink) const;
    core::pmr::Vector<Node> nodes_{&core::defaultCpuMemoryResource()};
    std::uint32_t root_ = invalid, free_ = invalid;
    std::size_t leaves_ = 0;
};
} // namespace arp::scene3d
