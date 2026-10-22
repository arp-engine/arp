#pragma once

// Render Extract — domain-neutral extraction contract.
//
// This header lives in the base `arp::scene` layer and must NOT depend on the
// 3d/2d/ui/renderer modules. It defines the polymorphic extraction surface used
// by render extractors to collect renderable data from components without the
// extractor performing `dynamic_cast` type checks on concrete component types.
//
// Architecture:
//   - `Component::extract(ctx, sink)` is overridden by each renderable component
//     to self-describe its render payload.
//   - `RenderExtractSink` is an abstract visitor (dual of the visitor pattern)
//     implemented by each extractor; `accept()` is called per component.
//   - `RenderExtractPayload` is a tagged-union carrying domain-neutral fields
//     (rect, color, text, world transform, sort key, objectId) plus an opaque
//     `Custom` channel for domain-specific extension (engine 3d handles, WoCC
//     resourceId strings, etc.). The Custom channel is interpreted by the sink
//     that owns the corresponding typeId contract — no guessing, no fallback.
//
// Layering: `arp_scene` only links `arp_core`. Handle-bearing domains (3d/2d/
// renderer) extend the payload via Custom + a domain-owned POD struct, keeping
// the base layer free of higher-module includes.

#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/reflection_macros.h"
#include "scene/ids.h"
#include "scene/transform.h"

#include <cstdint>

namespace arp::scene {

class GameObject; // forward declaration (full definition in game_object.h)

AENUM()
enum class RenderExtractKind {
    // Domain-neutral UI primitives.
    Box,
    Text,
    Image,
    // 3D scene primitives (fields carried via Custom; kind tag for sink routing).
    Mesh,
    Terrain,
    Animation,
    // 2D scene primitives (fields carried via Custom; kind tag for sink routing).
    Sprite,
    TileMapChunk,
    // Opaque extension point for domains that do not map onto the above kinds
    // (e.g. WoCC resourceId-based meshes, sky domes, HUD bars).
    Custom,
};

// 2D rectangle in layout/screen space. Kept domain-neutral (not gui::Rect) so
// the base layer does not depend on the gui module. Sinks that need gui::Rect
// convert deterministically at the boundary.
ASTRUCT(Construct = Default)

struct RenderExtractRect {
    APROP(Runtime)
    float x = 0.0f;
    APROP(Runtime)
    float y = 0.0f;
    APROP(Runtime)
    float width = 0.0f;
    APROP(Runtime)
    float height = 0.0f;
};

ASTRUCT(Construct = Default)

struct RenderExtractPayload {
    APROP(Runtime)
    RenderExtractKind kind = RenderExtractKind::Custom;

    // Owner identity (always populated by the extractor before calling extract).
    APROP(Runtime)
    GameObjectId objectId;

    // Common transform / sort fields.
    APROP(Runtime)
    Transform worldTransform;
    APROP(Runtime)
    uint64_t sortKey = 0;

    // UI / 2D spatial fields (used by Box/Text/Image/Sprite kinds).
    APROP(Runtime)
    RenderExtractRect rect;
    APROP(Runtime)
    RenderExtractRect clipRect;
    APROP(Runtime)
    bool hasClipRect = false;

    // Color (RGBA). Used by Box/Image/HUD primitives; defaults to opaque white.
    APROP(Runtime)
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    // Text content (Text kind).
    APROP(Runtime)
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};

    // Opaque Custom channel. `typeId` names the contract the sink must
    // implement to interpret `data` (e.g. "scene3d.mesh", "scene3d.terrain",
    // "scene3d.animation", "scene2d.sprite", "scene2d.tilemap",
    // "wocc.mesh", "wocc.sky", "wocc.hudbar", "wocc.hudtext"). `data` points
    // to a domain-owned POD struct whose lifetime encloses the sink call.
    // Sinks MUST reject unknown typeIds with a deterministic diagnostic
    // (never guess, never silently fall back).
    APROP(Runtime)
    arp::core::pmr::String typeId{&arp::core::defaultCpuMemoryResource()};
    APROP(Runtime)
    const void* data = nullptr;
};

// Context handed to `Component::extract`. Populated by the extractor from the
// traversal snapshot / scene graph before dispatching to each component.
struct RenderExtractContext {
    const GameObject* owner = nullptr;
    const Transform* worldTransform = nullptr;
    bool activeInHierarchy = true;
};

// Abstract sink. Each extractor implements `accept()` to route a payload into
// its domain-specific queue, applying validation/diagnostics per its contract.
class RenderExtractSink {
public:
    virtual ~RenderExtractSink() = default;

    virtual void accept(const RenderExtractContext& ctx, const RenderExtractPayload& payload) = 0;
};

} // namespace arp::scene
