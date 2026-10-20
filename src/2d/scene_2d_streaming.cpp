#include "2d/scene_2d_streaming.h"

#include "core/geometry.h"

#include <algorithm>
#include <utility>

namespace arp::scene2d {
namespace {

bool containsChunk(const arp::core::pmr::Vector<ChunkCoord>& chunks, ChunkCoord chunk)
{
    return std::find(chunks.begin(), chunks.end(), chunk) != chunks.end();
}

arp::core::Rect2<float> rectFromMinMax(float minX, float minY, float maxX, float maxY)
{
    return arp::core::Rect2<float>{minX, minY, maxX - minX, maxY - minY};
}

bool spriteVisible(const Bounds2D& bounds, const Viewport2D& viewport)
{
    return arp::core::rectsIntersect(rectFromMinMax(bounds.center.x - bounds.extents.x,
                                         bounds.center.y - bounds.extents.y,
                                         bounds.center.x + bounds.extents.x,
                                         bounds.center.y + bounds.extents.y),
        rectFromMinMax(viewport.minX(), viewport.minY(), viewport.maxX(), viewport.maxY()));
}

arp::core::pmr::Vector<ChunkCoord> wantedChunksFor(const Scene2DChunkGrid& grid,
    const arp::scene::Transform& worldTransform,
    const Viewport2D& viewport,
    int preloadMarginChunks)
{
    const auto margin = static_cast<float>(preloadMarginChunks) * grid.chunkWorldSize;
    const auto minX = viewport.minX() - margin;
    const auto maxX = viewport.maxX() + margin;
    const auto minY = viewport.minY() - margin;
    const auto maxY = viewport.maxY() + margin;
    const auto wantedRect = rectFromMinMax(minX, minY, maxX, maxY);
    const auto originX = worldTransform.translation.x + grid.origin.x;
    const auto originY = worldTransform.translation.y + grid.origin.y;

    arp::core::pmr::Vector<ChunkCoord> chunks;
    for (int y = 0; y < grid.chunkCountY; ++y) {
        for (int x = 0; x < grid.chunkCountX; ++x) {
            const auto chunkMinX = originX + static_cast<float>(x) * grid.chunkWorldSize;
            const auto chunkMinY = originY + static_cast<float>(y) * grid.chunkWorldSize;
            const auto chunkMaxX = chunkMinX + grid.chunkWorldSize;
            const auto chunkMaxY = chunkMinY + grid.chunkWorldSize;
            if (arp::core::rectsIntersect(
                    wantedRect, rectFromMinMax(chunkMinX, chunkMinY, chunkMaxX, chunkMaxY))) {
                chunks.push_back(ChunkCoord{x, y});
            }
        }
    }
    return chunks;
}

Viewport2D viewportFromCamera(const arp::scene::ResolvedCamera& camera)
{
    return Viewport2D::fromCenterSize({camera.camera.position.x, camera.camera.position.y, 0.0f},
        camera.orthographicWidth,
        camera.orthographicHeight);
}

// ---------------------------------------------------------------------------
// Scene2DStreamingSink — interprets scene2d.* typeId contracts pushed by
// SpriteRendererComponent / TileMapComponent via Component::extract().
//
// Carries the viewport, streaming policy and resident chunks as context, and
// emits both the Scene2DRenderQueue and the Scene2DChunkPlan. Records the first
// failure diagnostic deterministically (no guessing, no silent fallback).
// ---------------------------------------------------------------------------
class Scene2DStreamingSink final : public arp::scene::RenderExtractSink {
public:
    Scene2DStreamingSink(const Viewport2D& viewport,
        const Scene2DStreamingPolicy& policy,
        const arp::core::pmr::Vector<ChunkCoord>& residentChunks,
        Scene2DRenderQueue& queue,
        Scene2DChunkPlan& chunkPlan,
        Scene2DDiagnostic& failure)
        : viewport_(viewport), policy_(policy), residentChunks_(residentChunks), queue_(queue),
          chunkPlan_(chunkPlan), failure_(failure)
    {}

    void accept(const arp::scene::RenderExtractContext& ctx,
        const arp::scene::RenderExtractPayload& payload) override
    {
        if (failure_.code != Scene2DDiagnosticCode::None) {
            return;
        }

        if (payload.typeId == "scene2d.sprite") {
            acceptSprite(ctx, payload);
        } else if (payload.typeId == "scene2d.tilemap") {
            acceptTileMap(ctx, payload);
        } else if (payload.typeId == "scene2d.disabled") {
            // 2D renderable self-reported disabled/inactive. The 2D queue does
            // not track a disabled-component counter (unlike 3D); hidden
            // sprites are tracked via recordHiddenSprite() in acceptSprite.
            // No-op here, mirroring the original planner which simply skipped
            // disabled 2D components without counting.
        } else {
            failure_ = Scene2DDiagnostic{
                Scene2DDiagnosticCode::MissingRendererHandle,
                arp::core::pmr::String("Scene2DStreamingSink received unknown extract typeId: "
                        + payload.typeId + ". Contract violation — no fallback.",
                    &arp::core::defaultCpuMemoryResource()),
            };
        }
    }

private:
    void acceptSprite(const arp::scene::RenderExtractContext& ctx,
        const arp::scene::RenderExtractPayload& payload)
    {
        const auto* domain = static_cast<const Scene2DSpriteExtractPayload*>(payload.data);
        if (domain == nullptr || !domain->sprite.valid() || !domain->material.valid()
            || !domain->texture.valid()) {
            failure_ = Scene2DDiagnostic{
                Scene2DDiagnosticCode::MissingRendererHandle,
                arp::core::pmr::String(
                    "SpriteRendererComponent requires explicit sprite, material, and texture handles.",
                    &arp::core::defaultCpuMemoryResource()),
            };
            return;
        }
        if (!domain->localBounds.valid()) {
            failure_ = Scene2DDiagnostic{
                Scene2DDiagnosticCode::MissingBounds,
                arp::core::pmr::String("SpriteRendererComponent requires explicit local bounds.",
                    &arp::core::defaultCpuMemoryResource()),
            };
            return;
        }

        const auto worldTransform =
            ctx.worldTransform ? *ctx.worldTransform : arp::scene::Transform{};
        const auto worldBounds = transformBounds(domain->localBounds, worldTransform);
        if (!spriteVisible(worldBounds, viewport_)) {
            queue_.recordHiddenSprite();
            return;
        }

        Scene2DRenderItem item;
        item.kind = Scene2DRenderItemKind::Sprite;
        item.objectId = ctx.owner ? ctx.owner->id() : arp::scene::GameObjectId{};
        item.sprite = domain->sprite;
        item.material = domain->material;
        item.texture = domain->texture;
        item.worldBounds = worldBounds;
        item.sortKey = nextSortKey_++;
        queue_.addSprite(std::move(item));
    }

    void acceptTileMap(const arp::scene::RenderExtractContext& ctx,
        const arp::scene::RenderExtractPayload& payload)
    {
        const auto* domain = static_cast<const Scene2DTileMapExtractPayload*>(payload.data);
        if (domain == nullptr || !domain->tileMap.valid() || !domain->material.valid()
            || !domain->texture.valid()) {
            failure_ = Scene2DDiagnostic{
                Scene2DDiagnosticCode::MissingRendererHandle,
                arp::core::pmr::String(
                    "TileMapComponent requires explicit tilemap, material, and texture handles.",
                    &arp::core::defaultCpuMemoryResource()),
            };
            return;
        }
        if (!domain->grid.valid()) {
            failure_ = Scene2DDiagnostic{
                Scene2DDiagnosticCode::MissingChunkGrid,
                arp::core::pmr::String("TileMapComponent requires an explicit chunk grid.",
                    &arp::core::defaultCpuMemoryResource()),
            };
            return;
        }
        if (!domain->metadata.valid()) {
            failure_ = Scene2DDiagnostic{
                Scene2DDiagnosticCode::MissingTileMetadata,
                arp::core::pmr::String("TileMapComponent requires explicit tile metadata.",
                    &arp::core::defaultCpuMemoryResource()),
            };
            return;
        }

        const auto worldTransform =
            ctx.worldTransform ? *ctx.worldTransform : arp::scene::Transform{};
        auto wanted =
            wantedChunksFor(domain->grid, worldTransform, viewport_, policy_.preloadMarginChunks);
        if (wanted.size() > policy_.residentChunkBudget) {
            failure_ = Scene2DDiagnostic{
                Scene2DDiagnosticCode::ResidentChunkBudgetExceeded,
                arp::core::pmr::String(
                    "Viewport chunk demand exceeds the explicit resident chunk budget.",
                    &arp::core::defaultCpuMemoryResource()),
            };
            return;
        }

        chunkPlan_.allWantedChunks = wanted;
        for (const auto chunk : wanted) {
            if (containsChunk(residentChunks_, chunk)) {
                chunkPlan_.keep.push_back(chunk);
            } else {
                chunkPlan_.load.push_back(chunk);
            }

            const auto chunkMinX = worldTransform.translation.x + domain->grid.origin.x
                + static_cast<float>(chunk.x) * domain->grid.chunkWorldSize;
            const auto chunkMinY = worldTransform.translation.y + domain->grid.origin.y
                + static_cast<float>(chunk.y) * domain->grid.chunkWorldSize;
            Scene2DRenderItem item;
            item.kind = Scene2DRenderItemKind::TileMapChunk;
            item.objectId = ctx.owner ? ctx.owner->id() : arp::scene::GameObjectId{};
            item.chunk = chunk;
            item.tileMap = domain->tileMap;
            item.material = domain->material;
            item.texture = domain->texture;
            item.worldBounds =
                Bounds2D::explicitBounds({chunkMinX + domain->grid.chunkWorldSize * 0.5f,
                                             chunkMinY + domain->grid.chunkWorldSize * 0.5f,
                                             0.0f},
                    {domain->grid.chunkWorldSize * 0.5f, domain->grid.chunkWorldSize * 0.5f, 0.0f});
            item.sortKey = nextSortKey_++;
            queue_.addTileMapChunk(std::move(item));
        }

        for (const auto resident : residentChunks_) {
            if (!containsChunk(wanted, resident)) {
                chunkPlan_.unload.push_back(resident);
            }
        }
    }

    const Viewport2D& viewport_;
    const Scene2DStreamingPolicy& policy_;
    const arp::core::pmr::Vector<ChunkCoord>& residentChunks_;
    Scene2DRenderQueue& queue_;
    Scene2DChunkPlan& chunkPlan_;
    Scene2DDiagnostic& failure_;
    uint64_t nextSortKey_ = 0;
};

} // namespace

Viewport2D Viewport2D::fromCenterSize(arp::core::Vec3 center, float width, float height)
{
    Viewport2D viewport;
    viewport.center = center;
    viewport.width = width;
    viewport.height = height;
    return viewport;
}

bool Viewport2D::valid() const
{
    return width > 0.0f && height > 0.0f;
}

Scene2DStreamingPolicy Scene2DStreamingPolicy::explicitPolicy(
    int preloadMarginChunks, size_t residentChunkBudget)
{
    Scene2DStreamingPolicy policy;
    policy.preloadMarginChunks = preloadMarginChunks;
    policy.residentChunkBudget = residentChunkBudget;
    policy.hasExplicitMetadata = true;
    return policy;
}

bool Scene2DStreamingPolicy::valid() const
{
    return hasExplicitMetadata && preloadMarginChunks >= 0 && residentChunkBudget > 0;
}

Scene2DStreamingResult::Scene2DStreamingResult(
    Scene2DDiagnostic diagnostic, Scene2DRenderQueue queue, Scene2DChunkPlan chunkPlan)
    : diagnostic_(std::move(diagnostic)), queue_(std::move(queue)), chunkPlan_(std::move(chunkPlan))
{}

Scene2DStreamingResult Scene2DStreamingResult::success(
    Scene2DRenderQueue queue, Scene2DChunkPlan chunkPlan)
{
    return Scene2DStreamingResult(Scene2DDiagnostic{}, std::move(queue), std::move(chunkPlan));
}

Scene2DStreamingResult Scene2DStreamingResult::failure(
    Scene2DDiagnosticCode code, arp::core::pmr::String message)
{
    return Scene2DStreamingResult(
        Scene2DDiagnostic{code, std::move(message)}, Scene2DRenderQueue{}, Scene2DChunkPlan{});
}

Scene2DStreamingResult Scene2DStreamingPlanner::plan(const arp::scene::Scene& scene,
    const arp::scene::SceneTraversalSnapshot& snapshot,
    const Scene2DStreamingRequest& request) const
{
    if (!request.policy.valid()) {
        return Scene2DStreamingResult::failure(Scene2DDiagnosticCode::InvalidResidentChunkBudget,
            "Scene2DStreamingPolicy requires an explicit positive resident chunk budget.");
    }
    if (!request.camera.visibleQueues.includes2D()) {
        return Scene2DStreamingResult::failure(Scene2DDiagnosticCode::CameraDoesNotSee2D,
            "ResolvedCamera visible queue mask does not include the 2D queue.");
    }
    if (request.camera.projection == arp::scene::CameraProjection::Perspective) {
        return Scene2DStreamingResult::failure(
            Scene2DDiagnosticCode::Perspective2DProjectionUnsupported,
            "Perspective cameras require explicit 2D plane, pixel-density, sort/depth, and projection metadata before observing 2D queues.");
    }

    const auto viewport = viewportFromCamera(request.camera);
    Scene2DRenderQueue queue;
    Scene2DChunkPlan chunkPlan;
    Scene2DDiagnostic failure;
    Scene2DStreamingSink sink(
        viewport, request.policy, request.residentChunks, queue, chunkPlan, failure);

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
            // Polymorphic dispatch — each 2D renderable component
            // (SpriteRendererComponent / TileMapComponent) self-describes via
            // extract(), pushing a scene2d.* Custom payload (or a disabled
            // marker). Non-renderable components have the default no-op
            // extract() and contribute nothing. No dynamic_cast, no
            // component-type predicate.
            component->extract(ctx, sink);
            if (failure.code != Scene2DDiagnosticCode::None) {
                return Scene2DStreamingResult::failure(failure.code, std::move(failure.message));
            }
        }
    }

    return Scene2DStreamingResult::success(std::move(queue), std::move(chunkPlan));
}

} // namespace arp::scene2d
