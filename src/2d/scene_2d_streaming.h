#pragma once

#include "2d/scene_2d_render_queue.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "scene/scene_traversal_snapshot.h"

namespace arp::scene2d {

struct Viewport2D {
    arp::core::Vec3 center;
    float width = 0.0f;
    float height = 0.0f;

    static Viewport2D fromCenterSize(arp::core::Vec3 center, float width, float height);
    bool valid() const;

    float minX() const { return center.x - width * 0.5f; }

    float maxX() const { return center.x + width * 0.5f; }

    float minY() const { return center.y - height * 0.5f; }

    float maxY() const { return center.y + height * 0.5f; }
};

struct Scene2DStreamingPolicy {
    int preloadMarginChunks = 0;
    size_t residentChunkBudget = 0;
    bool hasExplicitMetadata = false;

    static Scene2DStreamingPolicy explicitPolicy(
        int preloadMarginChunks, size_t residentChunkBudget);
    bool valid() const;
};

struct Scene2DStreamingRequest {
    arp::scene::ResolvedCamera camera;
    Scene2DStreamingPolicy policy;
    arp::core::pmr::Vector<ChunkCoord> residentChunks;
};

struct Scene2DChunkPlan {
    arp::core::pmr::Vector<ChunkCoord> load;
    arp::core::pmr::Vector<ChunkCoord> keep;
    arp::core::pmr::Vector<ChunkCoord> unload;
    arp::core::pmr::Vector<ChunkCoord> allWantedChunks;
};

class Scene2DStreamingResult {
public:
    static Scene2DStreamingResult success(Scene2DRenderQueue queue, Scene2DChunkPlan chunkPlan);
    static Scene2DStreamingResult failure(
        Scene2DDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == Scene2DDiagnosticCode::None; }

    const Scene2DDiagnostic& diagnostic() const { return diagnostic_; }

    const Scene2DRenderQueue& queue() const { return queue_; }

    const Scene2DChunkPlan& chunkPlan() const { return chunkPlan_; }

private:
    Scene2DStreamingResult(
        Scene2DDiagnostic diagnostic, Scene2DRenderQueue queue, Scene2DChunkPlan chunkPlan);

    Scene2DDiagnostic diagnostic_;
    Scene2DRenderQueue queue_;
    Scene2DChunkPlan chunkPlan_;
};

// Scene2DStreamingPlanner walks a SceneTraversalSnapshot and dispatches
// Component::extract() on every component. Each renderable 2D component
// (SpriteRendererComponent / TileMapComponent) self-describes its render
// payload via extract(), pushing a Custom payload with a scene2d.* typeId
// contract that the planner's internal sink interprets. The planner performs
// NO dynamic_cast and NO component-type predicate checks — type-specific
// logic (viewport cull, chunk planning, handle/bounds validation) lives in
// the component + sink pair.
class Scene2DStreamingPlanner {
public:
    Scene2DStreamingResult plan(const arp::scene::Scene& scene,
        const arp::scene::SceneTraversalSnapshot& snapshot,
        const Scene2DStreamingRequest& request) const;
};

} // namespace arp::scene2d
