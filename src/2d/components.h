#pragma once

#include "2d/bounds.h"
#include "reflection/reflection_macros.h"
#include "renderer/runtime_scene.h"
#include "scene/component.h"
#include "scene/render_extract.h"

#include <cstdint>

namespace arp::scene2d {

ASTRUCT(Construct = Default)

struct SpriteHandle {
    APROP(Serialize, Editor)
    uint32_t id = 0;

    bool valid() const { return id != 0; }

    friend bool operator==(SpriteHandle lhs, SpriteHandle rhs) { return lhs.id == rhs.id; }

    friend bool operator!=(SpriteHandle lhs, SpriteHandle rhs) { return !(lhs == rhs); }
};

ASTRUCT(Construct = Default)

struct TileMapHandle {
    APROP(Serialize, Editor)
    uint32_t id = 0;

    bool valid() const { return id != 0; }

    friend bool operator==(TileMapHandle lhs, TileMapHandle rhs) { return lhs.id == rhs.id; }

    friend bool operator!=(TileMapHandle lhs, TileMapHandle rhs) { return !(lhs == rhs); }
};

ASTRUCT(Construct = Default)

struct ChunkCoord {
    APROP(Serialize, Editor)
    int x = 0;
    APROP(Serialize, Editor)
    int y = 0;

    friend bool operator==(ChunkCoord lhs, ChunkCoord rhs)
    {
        return lhs.x == rhs.x && lhs.y == rhs.y;
    }

    friend bool operator!=(ChunkCoord lhs, ChunkCoord rhs) { return !(lhs == rhs); }
};

ASTRUCT(Construct = Default)

struct Scene2DChunkGrid {
    APROP(Serialize, Editor)
    arp::core::Vec3 origin;
    APROP(Serialize, Editor)
    float chunkWorldSize = 0.0f;
    APROP(Serialize, Editor)
    int chunkCountX = 0;
    APROP(Serialize, Editor)
    int chunkCountY = 0;
    APROP(Serialize, Editor)
    bool hasExplicitMetadata = false;

    static Scene2DChunkGrid explicitGrid(
        arp::core::Vec3 origin, float chunkWorldSize, int chunkCountX, int chunkCountY);
    bool valid() const;
};

ASTRUCT(Construct = Default)

struct TileMapMetadata {
    APROP(Serialize, Editor)
    uint32_t tileCountX = 0;
    APROP(Serialize, Editor)
    uint32_t tileCountY = 0;
    APROP(Serialize, Editor)
    float worldUnitsPerTile = 0.0f;
    APROP(Serialize, Editor)
    bool hasExplicitMetadata = false;

    static TileMapMetadata explicitTiles(
        uint32_t tileCountX, uint32_t tileCountY, float worldUnitsPerTile);
    bool valid() const;
};

ASTRUCT(Construct = Default)

struct SpriteRendererComponentDesc {
    APROP(Runtime)
    SpriteHandle sprite;
    APROP(Runtime)
    arp::renderer::MaterialHandle material;
    APROP(Runtime)
    arp::renderer::TextureHandle texture;
    APROP(Serialize, Editor)
    Bounds2D localBounds;
    // Resource-driven contract / 1. An explicit UUID selects this path;
    // numeric handles above retain their existing legacy semantics.
    APROP(Serialize,
        Editor,
        ResourceReference,
        ReferenceTarget = "SpriteResource",
        Doc = "resource:SpriteResource:required")
    core::pmr::String spriteResourceId{&core::defaultCpuMemoryResource()};
    APROP(Serialize, Editor, Doc = "color:rgba")
    core::Vec4 tint{1, 1, 1, 1};
    APROP(Serialize, Editor)
    std::int32_t sortingLayer = 0;
    APROP(Serialize, Editor)
    std::int32_t sortingOrder = 0;
    APROP(Serialize, Editor)
    bool ySort = false;
    APROP(Serialize, Editor)
    bool flipX = false;
    APROP(Serialize, Editor)
    bool flipY = false;
};

ASTRUCT(Construct = Default)

struct TileMapComponentDesc {
    APROP(Serialize, Editor)
    TileMapHandle tileMap;
    APROP(Serialize, Editor)
    arp::renderer::MaterialHandle material;
    APROP(Serialize, Editor)
    arp::renderer::TextureHandle texture;
    APROP(Serialize, Editor)
    Scene2DChunkGrid grid;
    APROP(Serialize, Editor)
    TileMapMetadata metadata;
};

ASTRUCT(Construct = Default)

struct TileMapComponentEditorProperties {
    APROP(Serialize, Editor)
    Scene2DChunkGrid grid;
    APROP(Serialize, Editor)
    TileMapMetadata metadata;
};

using SpriteRendererComponentEditorProperties = SpriteRendererComponentDesc;

ACLASS(Ownership = Engine, Lifecycle = Explicit)

class ARP_API SpriteRendererComponent final : public arp::scene::Component {
public:
    const char* scriptTypeName() const override { return "SpriteRendererComponent"; }

    explicit SpriteRendererComponent(SpriteRendererComponentDesc desc);

    const SpriteRendererComponentDesc& desc() const { return desc_; }
    AFUNC()

    void set_sprite_resource(const core::pmr::String& id) { setSpriteResource(id); }
    AFUNC()

    void set_tint(core::Vec4 value) { desc_.tint = value; }
    AFUNC()

    void set_sorting(int layer, int order, bool ySort)
    {
        desc_.sortingLayer = layer;
        desc_.sortingOrder = order;
        desc_.ySort = ySort;
    }
    AFUNC()

    void set_flip(bool x, bool y)
    {
        desc_.flipX = x;
        desc_.flipY = y;
    }

    void setSpriteResource(core::pmr::String id) { desc_.spriteResourceId = std::move(id); }

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(desc_);
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override;

    /// Self-describing render extraction: pushes a `scene2d.sprite` Custom
    /// payload carrying sprite/material/texture handles and local bounds.
    void extract(const arp::scene::RenderExtractContext& ctx,
        arp::scene::RenderExtractSink& sink) const override;

private:
    SpriteRendererComponentDesc desc_;
};

class ARP_API TileMapComponent final : public arp::scene::Component {
public:
    const char* scriptTypeName() const override { return "TileMapComponent"; }

    explicit TileMapComponent(TileMapComponentDesc desc);

    const TileMapComponentDesc& desc() const { return desc_; }

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(
            TileMapComponentEditorProperties{desc_.grid, desc_.metadata});
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override;

    /// Self-describing render extraction: pushes a `scene2d.tilemap` Custom
    /// payload carrying tilemap/material/texture handles, grid and metadata.
    void extract(const arp::scene::RenderExtractContext& ctx,
        arp::scene::RenderExtractSink& sink) const override;

private:
    TileMapComponentDesc desc_;
};

// ---------------------------------------------------------------------------
// Domain-specific extract payloads (carried via RenderExtractPayload::Custom).
// Contract between the 2D components' extract() override and the
// Scene2DStreamingPlanner's sink. typeId strings are the deterministic
// contract names; the sink MUST reject unknown typeIds with a diagnostic.
// ---------------------------------------------------------------------------

/// typeId contract: "scene2d.sprite"
struct Scene2DSpriteExtractPayload {
    SpriteHandle sprite;
    arp::renderer::MaterialHandle material;
    arp::renderer::TextureHandle texture;
    Bounds2D localBounds;
};

/// typeId contract: "scene2d.tilemap"
struct Scene2DTileMapExtractPayload {
    TileMapHandle tileMap;
    arp::renderer::MaterialHandle material;
    arp::renderer::TextureHandle texture;
    Scene2DChunkGrid grid;
    TileMapMetadata metadata;
};

} // namespace arp::scene2d
