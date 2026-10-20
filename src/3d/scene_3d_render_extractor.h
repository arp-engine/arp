#pragma once

#include "3d/scene_3d_render_queue.h"
#include "core/cpu_allocator.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "scene/scene_traversal_snapshot.h"

namespace arp::scene3d {

enum class Scene3DExtractionPolicy {
    Strict,
    SkipInvalidGeometry,
};

// Scene3DRenderExtractor walks a SceneTraversalSnapshot and dispatches
// Component::extract() on every active+enabled component. Each renderable
// 3D component (MeshComponent / TerrainComponent / AnimatorComponent)
// self-describes its render payload via extract(), pushing a Custom payload
// with a scene3d.* typeId contract that the extractor's internal sink
// interprets. The extractor performs NO dynamic_cast and NO component-type
// predicate checks — type-specific logic lives in the component + sink pair.
class Scene3DRenderExtractor {
public:
    Scene3DExtractionResult extract(
        const arp::scene::Scene& scene, const arp::scene::SceneTraversalSnapshot& snapshot) const;
    Scene3DExtractionResult extract(const arp::scene::Scene& scene,
        const arp::scene::SceneTraversalSnapshot& snapshot,
        const arp::core::pmr::Vector<arp::scene::ResolvedCamera>& cameras) const;
    Scene3DExtractionResult extract(const arp::scene::Scene& scene,
        const arp::scene::SceneTraversalSnapshot& snapshot,
        const arp::core::pmr::Vector<arp::scene::ResolvedCamera>& cameras,
        bool ignoreForeignPayloads) const;
    Scene3DExtractionResult extract(const arp::scene::Scene& scene,
        const arp::scene::SceneTraversalSnapshot& snapshot,
        const arp::core::pmr::Vector<arp::scene::ResolvedCamera>& cameras,
        bool ignoreForeignPayloads,
        Scene3DExtractionPolicy policy) const;
};

} // namespace arp::scene3d
