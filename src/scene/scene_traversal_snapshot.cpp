#include "scene/scene_traversal_snapshot.h"

#include <utility>

namespace arp::scene {

SceneTraversalSnapshot::SceneTraversalSnapshot(
    SceneDiagnostic diagnostic, arp::core::pmr::Vector<SceneTraversalNode> nodes)
    : diagnostic_(std::move(diagnostic)), nodes_(std::move(nodes))
{}

SceneTraversalSnapshot SceneTraversalSnapshot::success(
    arp::core::pmr::Vector<SceneTraversalNode> nodes)
{
    return SceneTraversalSnapshot(SceneDiagnostic{}, std::move(nodes));
}

SceneTraversalSnapshot SceneTraversalSnapshot::failure(
    SceneDiagnosticCode code, arp::core::pmr::String message)
{
    return SceneTraversalSnapshot(SceneDiagnostic{code, std::move(message)}, {});
}

const SceneTraversalNode* SceneTraversalSnapshot::findNode(GameObjectId objectId) const
{
    for (const auto& node : nodes_) {
        if (node.objectId == objectId) {
            return &node;
        }
    }
    return nullptr;
}

const SceneTraversalNode& SceneTraversalSnapshot::requireNode(GameObjectId objectId) const
{
    const auto* node = findNode(objectId);
    assert(node != nullptr);
    return *node;
}

} // namespace arp::scene
