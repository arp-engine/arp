#pragma once

#include "core/cpu_allocator.h"
#include "reflection/reflection_macros.h"
#include "scene/diagnostics.h"
#include "scene/ids.h"
#include "scene/transform.h"

#include <cassert>

namespace arp::scene {

ASTRUCT(Construct = Default)

struct SceneTraversalOptions {
    APROP(Runtime)
    size_t maxNodeCount = 1024;
};

ASTRUCT(Construct = Default)

struct SceneTraversalNode {
    APROP(Runtime)
    GameObjectId objectId;
    APROP(Runtime)
    GameObjectId parentId;
    APROP(Runtime)
    ComponentId transformComponentId;
    APROP(Runtime)
    Transform localTransform;
    APROP(Runtime)
    Transform worldTransform;
    APROP(Runtime)
    bool active = true;
    APROP(Runtime)
    bool activeInHierarchy = true;
    APROP(Runtime)
    arp::core::pmr::Vector<ComponentId> componentIds;
};

class SceneTraversalSnapshot {
public:
    static SceneTraversalSnapshot success(arp::core::pmr::Vector<SceneTraversalNode> nodes);
    static SceneTraversalSnapshot failure(SceneDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == SceneDiagnosticCode::None; }

    const SceneDiagnostic& diagnostic() const { return diagnostic_; }

    const arp::core::pmr::Vector<SceneTraversalNode>& nodes() const { return nodes_; }

    const SceneTraversalNode* findNode(GameObjectId objectId) const;
    const SceneTraversalNode& requireNode(GameObjectId objectId) const;

    bool exposesNativeBackendHandles() const { return false; }

    bool exposesRawVertexBuffers() const { return false; }

    size_t nativeBackendHandleCount() const { return 0; }

    size_t rawVertexBufferCount() const { return 0; }

private:
    SceneTraversalSnapshot(
        SceneDiagnostic diagnostic, arp::core::pmr::Vector<SceneTraversalNode> nodes);

    SceneDiagnostic diagnostic_;
    arp::core::pmr::Vector<SceneTraversalNode> nodes_;
};

} // namespace arp::scene
