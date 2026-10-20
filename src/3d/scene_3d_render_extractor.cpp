#include "3d/scene_3d_render_extractor.h"

#include <utility>

namespace arp::scene3d {
namespace {

// ---------------------------------------------------------------------------
// Scene3DExtractSink — interprets scene3d.* typeId contracts pushed by
// MeshComponent / TerrainComponent / AnimatorComponent via Component::extract().
//
// The sink owns no dynamic allocation; it fills the Scene3DRenderQueue and
// records the first failure diagnostic (deterministic — no guessing, no silent
// fallback). Unknown typeIds are a contract violation and fail deterministically.
//
// sortKey policy: the extractor owns a monotonic counter and assigns it to
// each added mesh/terrain item (animation bindings take no sortKey, mirroring
// the original implementation). The payload's sortKey field is not used by the
// 3D sink — components do not know their draw order.
// ---------------------------------------------------------------------------
class Scene3DExtractSink final : public arp::scene::RenderExtractSink {
public:
    Scene3DExtractSink(Scene3DRenderQueue& queue,
        Scene3DDiagnostic& failure,
        bool ignoreForeignPayloads,
        Scene3DExtractionPolicy policy)
        : queue_(queue), failure_(failure), ignoreForeignPayloads_(ignoreForeignPayloads),
          policy_(policy)
    {}

    void accept(const arp::scene::RenderExtractContext& ctx,
        const arp::scene::RenderExtractPayload& payload) override
    {
        // Once a failure has been recorded, stop mutating the queue.
        if (failure_.code != Scene3DDiagnosticCode::None) {
            return;
        }

        if (ignoreForeignPayloads_ && payload.typeId != "scene3d.mesh"
            && payload.typeId != "scene3d.terrain" && payload.typeId != "scene3d.animation"
            && payload.typeId != "scene3d.light" && payload.typeId != "scene3d.disabled") {
            return;
        }

        if (payload.typeId == "scene3d.mesh") {
            acceptMesh(ctx, payload);
        } else if (payload.typeId == "scene3d.terrain") {
            acceptTerrain(ctx, payload);
        } else if (payload.typeId == "scene3d.animation") {
            acceptAnimation(ctx, payload);
        } else if (payload.typeId == "scene3d.light") {
            acceptLight(ctx, payload);
        } else if (payload.typeId == "scene3d.disabled") {
            // A renderable 3D component self-reported as disabled/inactive.
            // Mirror the original is3DComponent()-filtered disabled telemetry.
            queue_.recordDisabledComponent();
        } else {
            // Deterministic diagnostic for unknown typeId — never guess.
            failure_ = Scene3DDiagnostic{
                Scene3DDiagnosticCode::MissingRendererHandle,
                arp::core::pmr::String("Scene3DExtractSink received unknown extract typeId: "
                        + payload.typeId + ". Contract violation — no fallback.",
                    &arp::core::defaultCpuMemoryResource()),
            };
        }
    }

private:
    void reportGeometryFailure(Scene3DDiagnosticCode code, const char* message)
    {
        if (policy_ == Scene3DExtractionPolicy::SkipInvalidGeometry) {
            // Script-authored scenes may create a Mesh/Terrain component before
            // the native resource preparation stage binds renderer handles.
            // Keep the component out of the queue, but continue extracting
            // independent lights, cameras and animation bindings.
            queue_.recordSkippedInvalidComponent();
            return;
        }
        failure_ = Scene3DDiagnostic{
            code,
            arp::core::pmr::String(message, &arp::core::defaultCpuMemoryResource()),
        };
    }

    void acceptMesh(const arp::scene::RenderExtractContext& ctx,
        const arp::scene::RenderExtractPayload& payload)
    {
        const auto* domain = static_cast<const Scene3DMeshExtractPayload*>(payload.data);
        if (domain == nullptr || !domain->mesh.valid() || !domain->material.valid()) {
            reportGeometryFailure(Scene3DDiagnosticCode::MissingRendererHandle,
                "MeshComponent requires explicit mesh and material handles.");
            return;
        }
        if (!domain->localBounds.valid()) {
            reportGeometryFailure(Scene3DDiagnosticCode::MissingBounds,
                "MeshComponent requires explicit local bounds metadata.");
            return;
        }

        Scene3DRenderItem item;
        item.kind = Scene3DRenderItemKind::Mesh;
        item.objectId = ctx.owner ? ctx.owner->id() : arp::scene::GameObjectId{};
        item.mesh = domain->mesh;
        item.material = domain->material;
        item.worldTransform = ctx.worldTransform ? *ctx.worldTransform : arp::scene::Transform{};
        item.worldBounds = transformBounds(domain->localBounds, item.worldTransform);
        item.baseColorTint = domain->baseColorTint;
        item.sortKey = nextSortKey_++;
        queue_.addItem(std::move(item));
    }

    void acceptTerrain(const arp::scene::RenderExtractContext& ctx,
        const arp::scene::RenderExtractPayload& payload)
    {
        const auto* domain = static_cast<const Scene3DTerrainExtractPayload*>(payload.data);
        if (domain == nullptr || !domain->terrain.valid() || !domain->material.valid()) {
            reportGeometryFailure(Scene3DDiagnosticCode::MissingRendererHandle,
                "TerrainComponent requires explicit terrain and material handles.");
            return;
        }
        if (!domain->metadata.valid()) {
            reportGeometryFailure(Scene3DDiagnosticCode::MissingTerrainMetadata,
                "TerrainComponent requires explicit terrain metadata.");
            return;
        }
        if (!domain->localBounds.valid()) {
            reportGeometryFailure(Scene3DDiagnosticCode::MissingBounds,
                "TerrainComponent requires explicit local bounds metadata.");
            return;
        }

        Scene3DRenderItem item;
        item.kind = Scene3DRenderItemKind::Terrain;
        item.objectId = ctx.owner ? ctx.owner->id() : arp::scene::GameObjectId{};
        item.terrain = domain->terrain;
        item.material = domain->material;
        item.worldTransform = ctx.worldTransform ? *ctx.worldTransform : arp::scene::Transform{};
        item.worldBounds = transformBounds(domain->localBounds, item.worldTransform);
        item.sortKey = nextSortKey_++;
        queue_.addItem(std::move(item));
    }

    void acceptAnimation(const arp::scene::RenderExtractContext& ctx,
        const arp::scene::RenderExtractPayload& payload)
    {
        const auto* domain = static_cast<const Scene3DAnimationExtractPayload*>(payload.data);
        Scene3DAnimationBinding binding;
        binding.objectId = ctx.owner ? ctx.owner->id() : arp::scene::GameObjectId{};
        binding.clip = domain != nullptr ? domain->clip : nullptr;
        binding.controller = domain != nullptr ? domain->controller : nullptr;
        queue_.addAnimation(std::move(binding));
    }

    void acceptLight(const arp::scene::RenderExtractContext& ctx,
        const arp::scene::RenderExtractPayload& payload)
    {
        const auto* domain = static_cast<const Scene3DLightExtractPayload*>(payload.data);
        if (domain == nullptr) {
            failure_ = Scene3DDiagnostic{
                Scene3DDiagnosticCode::MissingRendererHandle,
                arp::core::pmr::String("LightComponent produced an empty extract payload.",
                    &arp::core::defaultCpuMemoryResource()),
            };
            return;
        }
        Scene3DLightBinding binding;
        binding.objectId = ctx.owner ? ctx.owner->id() : arp::scene::GameObjectId{};
        binding.type = domain->type;
        binding.worldPosition =
            ctx.worldTransform ? ctx.worldTransform->translation : arp::core::Vec3{};
        binding.worldDirection = domain->direction;
        binding.color = domain->color;
        binding.intensity = domain->intensity;
        binding.range = domain->range;
        binding.decay = domain->decay;
        binding.spotConeAngle = domain->spotConeAngle;
        binding.spotPenumbra = domain->spotPenumbra;
        binding.castShadow = domain->castShadow;
        queue_.addLight(std::move(binding));
    }

    Scene3DRenderQueue& queue_;
    Scene3DDiagnostic& failure_;
    bool ignoreForeignPayloads_ = false;
    Scene3DExtractionPolicy policy_ = Scene3DExtractionPolicy::Strict;
    uint64_t nextSortKey_ = 0;
};

} // namespace

Scene3DExtractionResult Scene3DRenderExtractor::extract(
    const arp::scene::Scene& scene, const arp::scene::SceneTraversalSnapshot& snapshot) const
{
    return extract(scene, snapshot, {}, false, Scene3DExtractionPolicy::Strict);
}

Scene3DExtractionResult Scene3DRenderExtractor::extract(const arp::scene::Scene& scene,
    const arp::scene::SceneTraversalSnapshot& snapshot,
    const arp::core::pmr::Vector<arp::scene::ResolvedCamera>& cameras) const
{
    return extract(scene, snapshot, cameras, false, Scene3DExtractionPolicy::Strict);
}

Scene3DExtractionResult Scene3DRenderExtractor::extract(const arp::scene::Scene& scene,
    const arp::scene::SceneTraversalSnapshot& snapshot,
    const arp::core::pmr::Vector<arp::scene::ResolvedCamera>& cameras,
    const bool ignoreForeignPayloads) const
{
    return extract(
        scene, snapshot, cameras, ignoreForeignPayloads, Scene3DExtractionPolicy::Strict);
}

Scene3DExtractionResult Scene3DRenderExtractor::extract(const arp::scene::Scene& scene,
    const arp::scene::SceneTraversalSnapshot& snapshot,
    const arp::core::pmr::Vector<arp::scene::ResolvedCamera>& cameras,
    const bool ignoreForeignPayloads,
    const Scene3DExtractionPolicy policy) const
{
    if (!snapshot.ok()) {
        return Scene3DExtractionResult::failure(Scene3DDiagnosticCode::MissingBounds,
            "Cannot extract 3D render queue from an invalid traversal snapshot.");
    }

    Scene3DRenderQueue queue;
    for (const auto& camera : cameras) {
        if (camera.visibleQueues.includes3D()) {
            queue.addCamera(camera);
        }
    }

    Scene3DDiagnostic failure;
    Scene3DExtractSink sink(queue, failure, ignoreForeignPayloads, policy);

    for (const auto& node : snapshot.nodes()) {
        const auto* object = scene.findObject(node.objectId);
        if (object == nullptr) {
            continue;
        }

        arp::scene::RenderExtractContext ctx;
        ctx.owner = object;
        ctx.worldTransform = &node.worldTransform;
        ctx.activeInHierarchy = node.activeInHierarchy;

        for (const auto& component : object->components()) {
            // Polymorphic dispatch — the component self-describes its render
            // payload via extract(); the sink routes by typeId. No
            // dynamic_cast, no is3DComponent() predicate. Each renderable 3D
            // component self-gates on active/enabled and pushes either its
            // render payload or a "scene3d.disabled" marker; non-renderable
            // components (TransformComponent, etc.) have the default no-op
            // extract() and contribute nothing.
            component->extract(ctx, sink);
            if (failure.code != Scene3DDiagnosticCode::None) {
                return Scene3DExtractionResult::failure(failure.code, std::move(failure.message));
            }
        }
    }

    return Scene3DExtractionResult::success(std::move(queue));
}

} // namespace arp::scene3d
