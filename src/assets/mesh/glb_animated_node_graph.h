#pragma once

#include "assets/mesh/glb_accessor_table.h"
#include "core/math/math.h"

namespace arp::assets::detail {

// Cold glTF topology preparation, independent of file acquisition and pose
// decoding. Indices preserve source identity; a mesh may have several instances
// with different skins. No mesh-to-skin overwrite or implicit skin0 binding.
class GlbAnimatedNodeGraph final : private GlbGeometrySchema {
public:
    static constexpr std::uint32_t absent = UINT32_MAX;

    struct Node final {
        std::uint32_t parent = absent, mesh = absent, skin = absent;
        bool selected = false;
    };

    struct Skin final {
        std::uint32_t firstJoint = 0, jointCount = 0;
        std::uint32_t skeleton = absent, inverseBindMatrices = absent;
        std::uint32_t commonRoot = absent;
    };

    struct Limits final {
        std::size_t maxNodes = 0, maxEdges = 0, maxScenes = 0, maxSceneRoots = 0;
        std::size_t maxSkins = 0, maxJoints = 0, maxMeshInstances = 0;
        std::size_t maxWorkingBytes = 0;
    };

    const core::pmr::Vector<Node>& nodes() const noexcept { return nodes_; }

    const core::pmr::Vector<std::uint32_t>& order() const noexcept { return order_; }

    const core::pmr::Vector<Skin>& skins() const noexcept { return skins_; }

    const core::pmr::Vector<std::uint32_t>& joints() const noexcept { return joints_; }

    std::size_t ownedBytes() const noexcept
    {
        return nodes_.capacity() * sizeof(Node) + order_.capacity() * sizeof(std::uint32_t)
            + skins_.capacity() * sizeof(Skin) + joints_.capacity() * sizeof(std::uint32_t);
    }

    // selectedScene is explicit even when the document has no default scene.
    // JSON and accessor storage are borrowed only for this call. The working
    // budget includes retained descriptors, the candidate and temporary markers.
    bool prepare(const Json& root,
        const GlbAccessorTable& accessors,
        std::uint32_t selectedScene,
        Limits limits,
        const char*& diagnostic)
    {
        diagnostic = nullptr;
        try {
            for (auto limit : {limits.maxNodes,
                     limits.maxEdges,
                     limits.maxScenes,
                     limits.maxSceneRoots,
                     limits.maxSkins,
                     limits.maxJoints,
                     limits.maxMeshInstances})
                require(limit && limit < UINT32_MAX, "ASSET-GLB-NODE-LIMITS");
            require(limits.maxWorkingBytes && limits.maxWorkingBytes != SIZE_MAX,
                "ASSET-GLB-NODE-LIMITS");
            const auto& nodes = array(root, "nodes");
            const auto& scenes = array(root, "scenes");
            const auto& skins = array(root, "skins");
            const auto& meshes = array(root, "meshes");
            require(!nodes.empty() && !scenes.empty() && !skins.empty() && !meshes.empty(),
                "ASSET-GLB-NODE-SCHEMA");
            require(selectedScene < scenes.size(), "ASSET-GLB-NODE-SCENE");
            if (root.find("scene"))
                require(number(root, "scene") < scenes.size(), "ASSET-GLB-NODE-SCENE");
            require(nodes.size() <= limits.maxNodes && scenes.size() <= limits.maxScenes
                    && skins.size() <= limits.maxSkins,
                "ASSET-GLB-NODE-BUDGET");
            std::size_t edges = 0, jointCount = 0, roots = 0, instances = 0;
            for (const auto& node : nodes) {
                if (node.find("children"))
                    addBounded(edges, array(node, "children").size(), limits.maxEdges);
                if (node.find("mesh"))
                    addBounded(instances, 1, limits.maxMeshInstances);
            }
            for (const auto& skin : skins)
                addBounded(jointCount, array(skin, "joints").size(), limits.maxJoints);
            for (const auto& scene : scenes)
                if (scene.find("nodes"))
                    addBounded(roots, array(scene, "nodes").size(), limits.maxSceneRoots);

            auto remaining = limits.maxWorkingBytes;
            charge(remaining, ownedBytes(), 1);
            charge(remaining, nodes.size(), sizeof(Node) + 2 * sizeof(std::uint32_t));
            charge(remaining, skins.size(), sizeof(Skin));
            charge(remaining, jointCount, sizeof(std::uint32_t));
            GlbAnimatedNodeGraph candidate;
            candidate.nodes_.resize(nodes.size());
            candidate.order_.reserve(nodes.size());
            candidate.skins_.reserve(skins.size());
            candidate.joints_.reserve(jointCount);
            core::pmr::Vector<std::uint32_t> markers(
                nodes.size(), absent, &core::defaultCpuMemoryResource());
            remaining = limits.maxWorkingBytes;
            charge(remaining, ownedBytes(), 1);
            charge(remaining, candidate.ownedBytes(), 1);
            charge(remaining, markers.capacity(), sizeof(std::uint32_t));

            for (std::size_t i = 0; i < nodes.size(); ++i) {
                const auto& node = nodes[i];
                keys(node,
                    {"name",
                        "children",
                        "mesh",
                        "skin",
                        "matrix",
                        "translation",
                        "rotation",
                        "scale",
                        "extras"});
                validateNodeTransform(node, true, "ASSET-GLB-NODE-TRANSFORM");
                auto& output = candidate.nodes_[i];
                if (node.find("mesh")) {
                    output.mesh = number(node, "mesh");
                    require(output.mesh < meshes.size(), "ASSET-GLB-NODE-MESH");
                }
                if (node.find("skin")) {
                    output.skin = number(node, "skin");
                    require(
                        output.skin < skins.size() && output.mesh != absent, "ASSET-GLB-NODE-SKIN");
                }
                if (node.find("children"))
                    for (const auto& child : array(node, "children")) {
                        const auto index = integer(child);
                        require(index < nodes.size() && index != i, "ASSET-GLB-NODE-CHILD");
                        require(candidate.nodes_[index].parent == absent,
                            "ASSET-GLB-NODE-MULTIPLE-PARENTS");
                        candidate.nodes_[index].parent = static_cast<std::uint32_t>(i);
                    }
            }
            // Forest traversal in source order. Single-parent validation means
            // every reachable child is enqueued once; leftover nodes are cycles.
            for (std::uint32_t i = 0; i < nodes.size(); ++i)
                if (candidate.nodes_[i].parent == absent)
                    candidate.order_.push_back(i);
            for (std::size_t head = 0; head < candidate.order_.size(); ++head) {
                const auto& node = nodes[candidate.order_[head]];
                if (node.find("children"))
                    for (const auto& child : array(node, "children"))
                        candidate.order_.push_back(integer(child));
            }
            require(candidate.order_.size() == nodes.size(), "ASSET-GLB-NODE-CYCLE");
            // A scene lists roots, never descendants. Reusing a root across
            // scenes is legal; duplicating it within one scene is not.
            for (std::uint32_t s = 0; s < scenes.size(); ++s) {
                keys(scenes[s], {"name", "nodes", "extras"});
                if (!scenes[s].find("nodes"))
                    continue;
                for (const auto& value : array(scenes[s], "nodes")) {
                    const auto index = integer(value);
                    require(index < nodes.size() && candidate.nodes_[index].parent == absent
                            && markers[index] != s,
                        "ASSET-GLB-NODE-SCENE-ROOT");
                    markers[index] = s;
                    if (s == selectedScene)
                        candidate.nodes_[index].selected = true;
                }
            }
            for (const auto index : candidate.order_) {
                auto& node = candidate.nodes_[index];
                if (node.parent != absent)
                    node.selected = candidate.nodes_[node.parent].selected;
            }
            std::fill(markers.begin(), markers.end(), absent);
            for (std::uint32_t index = 0; index < skins.size(); ++index) {
                const auto& skin = skins[index];
                keys(skin, {"name", "joints", "skeleton", "inverseBindMatrices", "extras"});
                const auto& joints = array(skin, "joints");
                require(!joints.empty(), "ASSET-GLB-NODE-JOINTS");
                Skin output;
                output.firstJoint = static_cast<std::uint32_t>(candidate.joints_.size());
                output.jointCount = static_cast<std::uint32_t>(joints.size());
                if (skin.find("skeleton")) {
                    output.skeleton = number(skin, "skeleton");
                    require(output.skeleton < nodes.size(), "ASSET-GLB-NODE-SKELETON");
                }
                if (skin.find("inverseBindMatrices")) {
                    output.inverseBindMatrices = number(skin, "inverseBindMatrices");
                    const auto* matrices = accessors.find(output.inverseBindMatrices);
                    require(matrices && matrices->componentType() == 5126
                            && matrices->components() == 16 && !matrices->normalized()
                            && matrices->count() >= joints.size(),
                        "ASSET-GLB-NODE-INVERSE-BIND");
                }
                for (const auto& value : joints) {
                    const auto joint = integer(value);
                    require(
                        joint < nodes.size() && markers[joint] != index, "ASSET-GLB-NODE-JOINTS");
                    markers[joint] = index;
                    candidate.joints_.push_back(joint);
                    auto treeRoot = joint;
                    while (candidate.nodes_[treeRoot].parent != absent)
                        treeRoot = candidate.nodes_[treeRoot].parent;
                    require(output.commonRoot == absent || output.commonRoot == treeRoot,
                        "ASSET-GLB-NODE-JOINT-ROOT");
                    output.commonRoot = treeRoot;
                    if (output.skeleton != absent) {
                        auto ancestor = joint;
                        while (ancestor != absent && ancestor != output.skeleton)
                            ancestor = candidate.nodes_[ancestor].parent;
                        require(ancestor == output.skeleton, "ASSET-GLB-NODE-SKELETON");
                    }
                }
                candidate.skins_.push_back(output);
            }
            for (const auto& node : candidate.nodes_) {
                if (!node.selected || node.skin == absent)
                    continue;
                const auto& skin = candidate.skins_[node.skin];
                for (std::uint32_t offset = 0; offset < skin.jointCount; ++offset)
                    require(candidate.nodes_[candidate.joints_[skin.firstJoint + offset]].selected,
                        "ASSET-GLB-NODE-SKIN-SCENE");
            }
            nodes_.swap(candidate.nodes_);
            order_.swap(candidate.order_);
            skins_.swap(candidate.skins_);
            joints_.swap(candidate.joints_);
            return true;
        } catch (const Failure& failure) {
            diagnostic = failure.diagnostic;
        } catch (const std::bad_alloc&) {
            diagnostic = "ASSET-GLB-NODE-ALLOCATION";
        }
        return false;
    }

private:
    static void charge(std::size_t& remaining, std::size_t count, std::size_t width)
    {
        chargeBudget(remaining, count, width, "ASSET-GLB-NODE-BUDGET");
    }

    static void addBounded(std::size_t& used, std::size_t count, std::size_t limit)
    {
        require(count <= limit - used, "ASSET-GLB-NODE-BUDGET");
        used += count;
    }

    core::pmr::Vector<Node> nodes_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint32_t> order_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Skin> skins_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint32_t> joints_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::assets::detail
